// Phone page: http://<board address>/ (or http://192.168.4.1 on the board's own WiFi "CamperHub").
// Login: user "camper", password shown and changeable in Settings (also the password of the own WiFi). Runs in its
// own task that only exists while the page is switched on (no RAM used when off). Everything that changes the UI
// state goes through a queue that the UI task drains (ui::web_drain).
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "internal.hpp"

namespace app {

static WebServer *srv = nullptr;
static QueueHandle_t q;
static char code[33] = "";
static volatile bool enabled = false;
const char *web_code() { return code; }
static void task(void *);
static bool started = false;                         // setup has finished: tasks may be made
void web_enable(bool on) {
  enabled = on;
  if (on && started && !task_web) xTaskCreatePinnedToCore(task, "web", 5632, nullptr, 1, &task_web, 0);
}
bool web_set_pass(const char *p) {                   // 8..32 characters (WiFi WPA2 needs at least 8)
  size_t n = strlen(p);
  if (n < 8 || n > 32) return false;
  for (const char *c = p; *c; c++) if (*c < 33 || *c > 126) return false;
  Preferences pr; pr.begin("camper", false); pr.putString("code", p); pr.end();
  strlcpy(code, p, sizeof code);
  log_event("phone page password changed");
  return true;
}
bool web_on() { return srv != nullptr; }
bool web_cmd_pop(WebCmd &c) { return q && xQueueReceive(q, &c, 0) == pdTRUE; }
static void push(const char *op, const char *key, int a, int b = 0, const char *text = "") {
  WebCmd c{}; strlcpy(c.op, op, sizeof c.op); strlcpy(c.key, key, sizeof c.key); c.a = a; c.b = b; strlcpy(c.text, text, sizeof c.text);
  if (q) xQueueSend(q, &c, 0);
}
void web_begin_code() {
  Preferences p; p.begin("camper", false);
  String c = p.getString("code", "");
  if (c.length() < 8) { char b[9]; snprintf(b, sizeof b, "%08u", (unsigned)(esp_random() % 100000000u)); c = b; p.putString("code", c); }
  strlcpy(code, c.c_str(), sizeof code);
  p.end();
  if (!q) q = xQueueCreate(8, sizeof(WebCmd));
}

extern const char PAGE[];
static bool auth() {
  if (ESP.getFreeHeap() < 16000 || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 6000) {   // never push the board over
    srv->send(503, "text/plain", "busy, try again");
    return false;
  }
  if (srv->authenticate("camper", code)) return true;
  srv->requestAuthentication(BASIC_AUTH, "Camper hub");
  return false;
}
static void ok() { srv->send(200, "application/json", "{\"ok\":true}"); }
static String arg(const char *n) { return srv->hasArg(n) ? srv->arg(n) : String(); }
static bool safe_name(const String &n) { return n.length() && n.length() < 64 && n.indexOf('/') < 0 && n.indexOf("..") < 0 && n[0] != '.'; }
static String dir_of(const String &d) { return d == "photos" ? "/camper/photos" : "/camper"; }

// JSON straight to the socket in 512 byte pieces (no big String in RAM)
struct Out {
  uint8_t b[512]; size_t n = 0;
  size_t write(uint8_t c) { b[n++] = c; if (n == sizeof b) flush(); return 1; }
  size_t write(const uint8_t *p, size_t l) { for (size_t i = 0; i < l; i++) write(p[i]); return l; }
  void flush() { if (n) srv->sendContent((const char *)b, n); n = 0; }
};
static void send_json(JsonDocument &d) {
  srv->setContentLength(CONTENT_LENGTH_UNKNOWN);
  srv->send(200, "application/json", "");
  Out *o = new (std::nothrow) Out;
  if (!o) { srv->sendContent("{}"); srv->sendContent(""); return; }
  serializeJson(d, *o); o->flush(); delete o;
  srv->sendContent("");
}
static void j_num(JsonObject o, const char *k, float v, int dec = 1) { if (!isnan(v)) o[k] = serialized(String(v, dec)); }

static void api_state() {
  JsonDocument d;
  {
    Lock l;
    time_t now = time(nullptr); struct tm t;
    if (now > 1700000000) { localtime_r(&now, &t); char b[20]; strftime(b, sizeof b, "%a %d %b %H:%M", &t); d["time"] = b; }
    d["demo"] = demo_on; d["heap"] = ESP.getFreeHeap();
    JsonObject w = d["wifi"].to<JsonObject>();
    w["ssid"] = S.ssid; w["on"] = S.wifi; w["net"] = S.online; w["portal"] = S.portal; w["rssi"] = S.rssi;
    const Victron &v = S.vic;
    JsonObject vo = d["vic"].to<JsonObject>();
    vo["key"] = v.have_key; vo["seen"] = v.seen; vo["bad"] = v.key_bad;
    if (v.decoded && millis() - v.at < 120000) {
      vo["ok"] = true; vo["state"] = vic_state(v.state);
      if (v.pv_w != 0xFFFF) vo["pv"] = v.pv_w;
      if (v.vbat_cv != 0x7FFF) j_num(vo, "v", v.vbat_cv / 100.0f, 2);
      if (v.ibat_da != 0x7FFF) j_num(vo, "a", v.ibat_da / 10.0f);
      if (v.load_da != 0x1FF) j_num(vo, "load", v.load_da / 10.0f);
      if (v.yield_10wh != 0xFFFF) j_num(vo, "kwh", v.yield_10wh / 100.0f, 2);
      vo["soc"] = vic_soc(v); vo["peak"] = v.peak_w;
      if (v.error) vo["err"] = vic_error(v.error);
    }
    const Bank &b = S.bank;
    JsonObject bo = d["bank"].to<JsonObject>();
    bo["status"] = b.status; bo["conn"] = b.connected; bo["name"] = b.name;
    if (b.data) {
      bo["soc"] = (int)(b.soc + 0.5f); bo["in"] = b.in_w; bo["out"] = b.out_w; bo["acw"] = b.ac_out_w;
      bo["usb"] = b.usb; bo["dc"] = b.dc; bo["ac"] = b.ac; bo["light"] = b.light_mode;
      bo["left"] = b.remain_min; bo["full"] = b.to_full_min;
      j_num(bo, "usba1", b.usb_a1); j_num(bo, "usba2", b.usb_a2); j_num(bo, "usbc1", b.usb_c1); j_num(bo, "usbc2", b.usb_c2); j_num(bo, "car", b.car_w);
      if (b.s_ok) {
        JsonObject s = bo["set"].to<JsonObject>();
        s["27"] = b.light_mode; s["56"] = b.s_key; s["57"] = b.s_silent; s["20"] = b.s_dc_amp; s["62"] = b.s_screen;
        s["60"] = b.s_ac_sb; s["61"] = b.s_dc_sb; s["59"] = b.s_usb_sb; s["68"] = b.s_idle; s["66"] = b.s_floor; s["67"] = b.s_ceil;
      }
    }
    JsonArray sa = d["sens"].to<JsonArray>();
    for (auto &s : S.sensors) {
      JsonObject o = sa.add<JsonObject>();
      o["a"] = s.addr; o["k"] = s.kind; o["r"] = s.role; j_num(o, "t", s.t); j_num(o, "h", s.h, 0); j_num(o, "p", s.hpa);
      j_num(o, "lo", s.tmin); j_num(o, "hi", s.tmax); o["b"] = s.batt; o["age"] = (millis() - s.seen) / 1000;
      float tr = hpa_trend(s); j_num(o, "tr", tr);
    }
    const Wx &x = S.wx;
    JsonObject wo = d["wx"].to<JsonObject>();
    wo["ok"] = x.ok;
    if (x.ok) {
      wo["city"] = x.city; j_num(wo, "t", x.t); wo["code"] = x.code; wo["wind"] = (int)x.wind; wo["gust"] = (int)x.gust; wo["hum"] = (int)x.hum;
      wo["warn"] = x.warn; wo["at"] = x.fetched;
      JsonArray da = wo["days"].to<JsonArray>();
      for (int i = 0; i < x.nd; i++) {
        JsonObject o = da.add<JsonObject>();
        o["n"] = x.d_name[i]; o["c"] = x.d_code[i]; o["hi"] = x.d_max[i]; o["lo"] = x.d_min[i]; o["r"] = x.d_rain[i]; o["g"] = x.d_gust[i];
        j_num(o, "mm", x.d_sum[i]); o["up"] = x.d_rise[i]; o["dn"] = x.d_set[i];
      }
    }
    JsonObject lv = d["lvl"].to<JsonObject>();
    lv["ok"] = S.lvl_present; j_num(lv, "p", S.pitch, 2); j_num(lv, "r", S.roll, 2);
    JsonObject st = d["set"].to<JsonObject>();
    st["bright"] = cfg.bright; st["beep"] = cfg.beep; st["saver"] = cfg.saver; st["scroff"] = cfg.scr_off; st["look"] = cfg.saver_look; st["thm"] = cfg.look_mode;
    st["wb"] = cfg.len_cm; st["trk"] = cfg.width_cm; st["rot"] = cfg.lvl_rot; st["batt"] = v.batt; st["alarm"] = v.alarm_cv;
    st["ap"] = cfg.ap; st["demo"] = demo_on; d["ver"] = FW_VERSION; d["upd"] = ota_msg;
    JsonObject pl = d["place"].to<JsonObject>();
    pl["pinned"] = P.pinned; pl["name"] = P.pinned ? P.cur.name : "auto"; pl["busy"] = P.searching; pl["err"] = P.err;
    JsonArray ph = pl["hits"].to<JsonArray>();
    for (int i = 0; i < P.n_hits; i++) { JsonObject o = ph.add<JsonObject>(); o["n"] = P.hits[i].name; o["a"] = P.hits[i].area; }
    JsonArray pr = pl["recent"].to<JsonArray>();
    for (int i = 0; i < P.n_recent; i++) { JsonObject o = pr.add<JsonObject>(); o["n"] = P.recent[i].name; o["a"] = P.recent[i].area; }
    JsonObject so = d["sd"].to<JsonObject>();
    so["ok"] = S.sd_ok; so["msg"] = S.sd_msg; so["backup"] = SDI.backup_at; so["upd"] = SDI.update ? SDI.update_kb : 0; so["job"] = SDI.msg;
    const TripDay &td = trip_today;
    if (td.date[0]) {
      JsonObject o = d["today"].to<JsonObject>();
      o["place"] = td.place; j_num(o, "omin", td.omin); j_num(o, "omax", td.omax); j_num(o, "imin", td.imin); j_num(o, "imax", td.imax);
      j_num(o, "vmin", td.vmin, 2); j_num(o, "kwh", td.kwh, 2);
    }
  }
  send_json(d);
}

static void api_lists() {
  Preferences p; p.begin("check", true);
  JsonDocument d;
  for (int k = 0; k < 2; k++) {
    JsonObject o = d[k ? "arrive" : "leave"].to<JsonObject>();
    int n = p.getUChar(k ? "n1" : "n0", 0); uint32_t m = p.getULong(k ? "m1" : "m0", 0);
    JsonArray a = o["items"].to<JsonArray>();
    char key[8];
    for (int i = 0; i < n; i++) { snprintf(key, sizeof key, "%c%d", k ? 'b' : 'a', i); JsonObject it = a.add<JsonObject>(); it["t"] = p.getString(key, "?"); it["d"] = (bool)(m >> i & 1); }
  }
  p.end();
  send_json(d);
}

static void api_files() {
  String dn = dir_of(arg("d"));
  File d = SD.open(dn);
  String out = "[";
  if (d) {
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
      const char *n = strrchr(f.name(), '/'); n = n ? n + 1 : f.name();
      if (!f.isDirectory() && n[0] != '.' && strcmp(n, "settings.tmp")) {
        if (out.length() > 1) out += ",";
        out += "{\"n\":\""; out += n; out += "\",\"s\":"; out += String((unsigned)f.size()); out += "}";
      }
      f.close();
      if (out.length() > 12000) break;
    }
    d.close();
  }
  out += "]";
  srv->send(200, "application/json", out);
}

bool ble_stop(); void ble_pause(bool on);
static bool fw_busy = false;
static File up;
static String up_err;
static void upload_file() {
  HTTPUpload &u = srv->upload();
  if (u.status == UPLOAD_FILE_START) {
    up_err = ""; ble_pause(true);
    String d = dir_of(arg("d")), n = u.filename;
    int s = n.lastIndexOf('/'); if (s >= 0) n = n.substring(s + 1);
    if (!safe_name(n)) { up_err = "bad name"; return; }
    SD.mkdir(d.c_str());
    up = SD.open((d + "/" + n).c_str(), FILE_WRITE);
    if (!up) up_err = "cannot write the card";
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (up) up.write(u.buf, u.currentSize);
  }
  else if (u.status == UPLOAD_FILE_END || u.status == UPLOAD_FILE_ABORTED) { if (up) up.close(); ble_pause(false); }
}
static void upload_fw() {
  HTTPUpload &u = srv->upload();
  if (u.status == UPLOAD_FILE_START) { up_err = ""; fw_busy = true; log_event("firmware upload from the phone page"); ble_stop(); if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) { up_err = String("update cannot start: ") + Update.errorString(); Serial.println(up_err); } }
  else if (u.status == UPLOAD_FILE_WRITE) {
    if (!up_err.length() && Update.write(u.buf, u.currentSize) != u.currentSize) up_err = "write failed";
    if ((u.totalSize & 0x1FFFF) < HTTP_UPLOAD_BUFLEN) Serial.printf("fw upload %u KB heap %u\n", (unsigned)(u.totalSize / 1024), (unsigned)ESP.getFreeHeap());
  }
  else if (u.status == UPLOAD_FILE_END) { if (!up_err.length() && !Update.end(true)) up_err = String("image not valid: ") + Update.errorString(); if (up_err.length()) Serial.println(up_err); }
  else if (u.status == UPLOAD_FILE_ABORTED) { Update.abort(); up_err = "aborted"; delay(300); sd_restart(); }
}

static void routes() {
  srv->on("/", HTTP_GET, [] { if (!auth()) return; srv->sendHeader("Cache-Control", "no-store"); srv->send_P(200, "text/html", PAGE); });
  srv->on("/api/state", HTTP_GET, [] { if (auth()) api_state(); });
  srv->on("/api/lists", HTTP_GET, [] { if (auth()) api_lists(); });
  srv->on("/api/files", HTTP_GET, [] { if (auth()) api_files(); });
  srv->on("/api/set", HTTP_POST, [] { if (!auth()) return; push("set", arg("k").c_str(), arg("v").toInt()); ok(); });
  srv->on("/api/check", HTTP_POST, [] { if (!auth()) return; push("chk", arg("op").c_str(), arg("i").toInt(), arg("l").toInt(), arg("t").c_str()); ok(); });
  srv->on("/api/bank", HTTP_POST, [] { if (!auth()) return; push("bank", "", arg("o").toInt(), arg("on").toInt()); ok(); });
  srv->on("/api/bankset", HTTP_POST, [] { if (!auth()) return; push("bset", "", arg("r").toInt(), arg("v").toInt()); ok(); });
  srv->on("/api/role", HTTP_POST, [] { if (!auth()) return; String a = arg("a"); if (a.length() < 18) sens_set_role(a.c_str(), arg("r").toInt() % R_COUNT); ok(); });
  srv->on("/api/vkey", HTTP_POST, [] { if (!auth()) return; push("vkey", "", 0, 0, arg("k").c_str()); ok(); });
  srv->on("/api/place", HTTP_POST, [] {
    if (!auth()) return;
    String q = arg("q"), op = arg("op");
    if (op == "search" && q.length()) place_search(q.c_str());
    else push("place", op.c_str(), arg("i").toInt());
    ok();
  });
  srv->on("/api/job", HTTP_POST, [] { if (!auth()) return; String j = arg("j");
    if (j == "check") { ok(); delay(500); ota_request(); return; } sd_job(j == "backup" ? J_BACKUP : j == "restore" ? J_RESTORE : j == "update" ? J_UPDATE : J_NONE); ok(); });
  srv->on("/api/del", HTTP_POST, [] {
    if (!auth()) return;
    String n = arg("n");
    if (!safe_name(n)) { srv->send(400, "text/plain", "bad name"); return; }
    SD.remove((dir_of(arg("d")) + "/" + n).c_str()); ok();
  });
  srv->on("/f", HTTP_GET, [] {
    if (!auth()) return;
    String n = arg("n");
    if (!safe_name(n)) { srv->send(400, "text/plain", "bad name"); return; }
    File f = SD.open((dir_of(arg("d")) + "/" + n).c_str());
    if (!f) { srv->send(404, "text/plain", "not found"); return; }
    String type = n.endsWith(".jpg") || n.endsWith(".JPG") ? "image/jpeg" : n.endsWith(".json") ? "application/json" : "text/plain";
    if (srv->hasArg("dl")) srv->sendHeader("Content-Disposition", "attachment; filename=\"" + n + "\"");
    srv->streamFile(f, type);
    f.close();
  });
  srv->on("/upload", HTTP_POST, [] { if (!auth()) return; if (up_err.length()) srv->send(500, "text/plain", up_err); else ok(); }, [] { if (auth()) upload_file(); });
  srv->on("/update", HTTP_POST, [] {
    if (!auth()) return;
    bool bad = up_err.length() || Update.hasError();
    if (bad) srv->send(500, "text/plain", up_err.length() ? up_err : "update failed"); else ok();
    if (fw_busy) { delay(800); sd_restart(); }          // Bluetooth was switched off for the upload: start clean
  }, [] { if (auth()) upload_fw(); });
  srv->onNotFound([] { srv->send(404, "text/plain", "not here"); });
}

// own task (a slow phone or a big upload must never hold up the SD logger): start / stop, serve requests
static bool web_service() {
  bool net; { Lock l; net = S.wifi; }
  bool want = enabled && (net || cfg.ap);
  if (want && !srv) {
    srv = new WebServer(80);
    routes();
    srv->begin();
    Serial.printf("web: on, heap %u\n", (unsigned)ESP.getFreeHeap());
  } else if (!want && srv) { srv->stop(); delete srv; srv = nullptr; Serial.println("web: off"); }
  // only take a request while the board has room for it (else it waits in the network queue a moment)
  if (srv && ESP.getFreeHeap() > 26000 && heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) > 12000) srv->handleClient();
  return srv != nullptr;
}
static void task(void *) {
  for (;;) {
    bool on = web_service();
    if (!enabled && !srv) { Serial.println("web: task gone"); task_web = nullptr; vTaskDelete(nullptr); }
    delay(on ? 4 : 200);
  }
}
void web_start_task() { started = true; if (enabled) web_enable(true); }

}  // namespace app
