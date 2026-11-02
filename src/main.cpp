// Camper hub for the CYD ESP32-2432S028: Victron solar, thermometers, weather, level, checklists.
// C++ / LVGL 9 / LovyanGFX DMA. Background work runs on core 0, the UI on core 1.
#include <Arduino.h>
#include <lvgl.h>
#include <Preferences.h>
#include <WiFi.h>
#include <SD.h>
#include "hw.hpp"
#include "internal.hpp"

namespace ui { void saver_show(bool on); void saver_tick(); bool saver_on(); void begin(); bool alarm_active(); void goto_tab(int i); void open_forecast_test(int view); void close_modal(); void power_view_test(int i); void tools_view_test(int i); void open_place(); void bank_settings_test(int tap); void look_request(); }
namespace app {
Settings cfg;
static Preferences sp;
void save_settings() {
  sp.putUChar("bright", cfg.bright); sp.putBool("beep", cfg.beep);
  sp.putUShort("wb", cfg.len_cm); sp.putUShort("trk", cfg.width_cm); sp.putUChar("rot", cfg.lvl_rot);
  sp.putUChar("saver", cfg.saver); sp.putUChar("scroff", cfg.scr_off);
  sp.putUChar("look", cfg.saver_look); sp.putBool("web", cfg.web); sp.putBool("ap", cfg.ap); sp.putUChar("thm", cfg.look_mode);
}
static void load_settings() {
  sp.begin("camper", false);
  cfg.bright = sp.getUChar("bright", 0); cfg.beep = sp.getBool("beep", true);
  cfg.len_cm = sp.getUShort("wb", 404); cfg.width_cm = sp.getUShort("trk", 180); cfg.lvl_rot = sp.getUChar("rot", 0);
  cfg.saver = sp.getUChar("saver", 3) % 6; cfg.scr_off = sp.getUChar("scroff", 2) % 5;
  cfg.saver_look = sp.getUChar("look", 0) % 2; cfg.web = sp.getBool("web", false); cfg.ap = sp.getBool("ap", false); cfg.look_mode = sp.getUChar("thm", 0) % 3;
}
}  // namespace app
using namespace app;

static LGFX tft;
LGFX &app_tft() { return tft; }
namespace app { bool photos_begin(); void photos_tick(); }
static bool photo_mode = false;          // the photo screensaver draws straight to the panel: LVGL flushes are dropped
extern "C" size_t lv_mem_spilled(void), lv_mem_spilled_max(void);
static constexpr int W = 320, H = 240, BUF_LINES = 12;   // one buffer: 5 KB more free RAM than two
static uint32_t last_touch = 0;
static bool dimmed = false, dark = false;
static volatile bool want_wake = false;
static constexpr uint32_t DIM_AFTER = 5 * 60 * 1000;

static bool shot_mode = false;                         // "shot": exact frame as LVGL draws it, sent over serial
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  if (photo_mode) { lv_display_flush_ready(d); return; }
  uint32_t w = lv_area_get_width(a), h = lv_area_get_height(a);
  if (shot_mode) { Serial.printf("AREA %d %d %u %u\n", (int)a->x1, (int)a->y1, (unsigned)w, (unsigned)h); Serial.write(px, w * h * 2); feedLoopWDT(); }
  lv_draw_sw_rgb565_swap(px, w * h);
  if (tft.getStartCount() == 0) tft.startWrite();
  tft.pushImageDMA(a->x1, a->y1, w, h, (lgfx::swap565_t *)px);   // LVGL waits for it (flush_wait) before reusing the buffer
}
static void read_touch(lv_indev_t *, lv_indev_data_t *data) {
  int16_t x, y;
  if (touch::read(x, y)) {
    last_touch = millis();
    if (dimmed) {                        // first touch only wakes the screen
      dimmed = false; dark = false; ui::saver_show(false);
      data->state = LV_INDEV_STATE_RELEASED;
      while (!digitalRead(touch::IRQ)) delay(10);
      return;
    }
    data->point.x = x; data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else data->state = LV_INDEV_STATE_RELEASED;
}
static uint32_t tick_cb() { return millis(); }
void app::screen_wake() { last_touch = millis(); want_wake = true; }

// ---- brightness: auto from the light sensor (higher reading = darker), smooth steps
static int ldr_avg = -1;
static void brightness() {
  int raw = analogRead(ldr::PIN);
  ldr_avg = ldr_avg < 0 ? raw : (ldr_avg * 7 + raw) / 8;
  int target;
  if (cfg.bright == 1) target = 50; else if (cfg.bright == 2) target = 140; else if (cfg.bright == 3) target = 255;
  else target = constrain(map(ldr_avg, 1400, 0, 25, 255), 25, 255);
  if (dark) target = 0;
  else if (dimmed) target = photo_mode ? max(20, target / 3) : ui::saver_on() ? max(5, target / 10) : max(8, target / 6);
  static int cur = 255;
  cur += (target - cur) / 4 + (target > cur ? 1 : target < cur ? -1 : 0);
  backlight::set(constrain(cur, 0, 255));
}

// ---- sound: non-blocking patterns
static int beep_pat = 0, beep_step = 0; static uint32_t beep_t = 0;
void app::beep(int p) { if (!cfg.beep) return; beep_pat = p; beep_step = 0; beep_t = 0; }
static void sound() {
  static const int P1[] = {2200, 80, 0, 0}, P2[] = {2600, 180, 0, 120, 2600, 180, 0, 120, 2600, 400, 0, 0};
  if (!beep_pat) return;
  const int *p = beep_pat == 1 ? P1 : P2;
  int n = beep_pat == 1 ? 4 : 12;
  if ((int32_t)(millis() - beep_t) < 0) return;      // safe over the 49 day millis() wrap
  if (beep_step >= n) { speaker::tone(0); beep_pat = 0; return; }
  speaker::tone(p[beep_step]);
  beep_t = millis() + p[beep_step + 1];
  beep_step += 2;
}

// ---- serial console: vic | vkey <hex> | vkey remove | ls [dir] | cat <file> | sens | days | ldr | heap
static void console() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n') { if (c != '\r') line += c; continue; }
    String cmd = line; line = "";
    if (cmd == "vkey remove") { vic_forget_key(); Serial.println("key removed"); }
    else if (cmd.startsWith("vkey ")) { int r = vic_set_key(cmd.substring(5).c_str()); Serial.println(r > 0 ? "ok" : r < 0 ? "locked: vkey remove first" : "bad key"); }
    else if (cmd == "ls" || cmd.startsWith("ls ")) {     // SD listing: ls [/dir]
      String d = cmd.length() > 3 ? cmd.substring(3) : String("/camper");
      File dir = SD.open(d);
      if (!dir) Serial.println("no such dir");
      else { for (File f = dir.openNextFile(); f; f = dir.openNextFile()) { Serial.printf(" %s %u\n", f.name(), (unsigned)f.size()); f.close(); } dir.close(); }
      Serial.println("--end--");
    } else if (cmd.startsWith("cat ")) {                 // SD file dump: cat /camper/events.csv
      File f = SD.open(cmd.substring(4));
      if (!f) Serial.println("not found");
      else { uint8_t b[256]; int n; while ((n = f.read(b, sizeof b)) > 0) { Serial.write(b, n); delay(2); } f.close(); }
      Serial.println("--end--");
    }
    else if (cmd == "demo") { set_demo(!demo_on); Serial.printf("demo %d\n", (int)demo_on); }
    else if (cmd == "vic") {
      Lock l; auto &v = S.vic;
      Serial.printf("key=%d seen=%d bad=%d dec=%d %s %04X rssi=%d %s %dcV %ddA %dW y=%d peak=%d hist=%d soc=%d\n", v.have_key, v.seen, v.key_bad,
                    v.decoded, v.addr, v.model, v.rssi, vic_state(v.state), v.vbat_cv, v.ibat_da, v.pv_w, v.yield_10wh, v.peak_w, v.hist_n, vic_soc(v));
    } else if (cmd == "sens") {
      Lock l;
      for (auto &s : S.sensors) Serial.printf(" %s %s role=%s t=%.2f h=%.1f b=%d rssi=%d age=%us\n", s.addr, s.kind, role_name(s.role), s.t, s.h, s.batt, s.rssi, (unsigned)((millis() - s.seen) / 1000));
    } else if (cmd == "days") {
      Lock l; for (auto &d : S.days) Serial.printf(" %s %.2f kWh peak %u\n", d.date, d.yield_10wh / 100.0f, d.peak_w);
      Serial.printf("sd=%d %s rows=%u clock_est=%d\n", S.sd_ok, S.sd_msg, (unsigned)S.sd_rows, S.clock_est);
    } else if (cmd == "ldr") Serial.printf("ldr raw %d avg %d\n", analogRead(ldr::PIN), ldr_avg);
    else if (cmd == "wx") { Lock l; Serial.printf("wx ok=%d %s %.1fC code %d\n", S.wx.ok, S.wx.city, S.wx.t, S.wx.code); }
    else if (cmd == "sdtest") {   // raw card check on the default VSPI pins, outside the logger
      extern void sd_probe(); sd_probe();
    } else if (cmd == "sdformat YES") {   // one-off, on request: erases the card, FAT32, then restarts
      extern void sd_format(); sd_format();
    }
    else if (cmd.startsWith("tab ")) ui::goto_tab(cmd.substring(4).toInt());
    else if (cmd.startsWith("fc ")) ui::open_forecast_test(cmd.substring(3).toInt());
    else if (cmd == "close") ui::close_modal();
    else if (cmd.startsWith("ts ")) ui::tools_view_test(cmd.substring(3).toInt());
    else if (cmd == "place") ui::open_place();
    else if (cmd == "saver") { dimmed = true; ui::saver_show(true); }
    else if (cmd.startsWith("thm ")) { cfg.look_mode = cmd.substring(4).toInt() % 3; ui::look_request(); Serial.printf("thm %d\n", cfg.look_mode); }
    else if (cmd.startsWith("look ")) { cfg.saver_look = cmd.substring(5).toInt() % 2; Serial.printf("look %d\n", cfg.saver_look); }
    else if (cmd.startsWith("web ")) { cfg.web = cmd.substring(4).toInt(); web_enable(cfg.web); Serial.printf("web %d code %s\n", cfg.web, web_code()); }
    else if (cmd.startsWith("ap ")) { cfg.ap = cmd.substring(3).toInt(); ap_enable(cfg.ap); Serial.printf("ap %d\n", cfg.ap); }
    else if (cmd == "trips") { app::Lock l; Serial.printf("today %s %s omin %.1f omax %.1f v %.2f; %u in RAM\n", trip_today.date, trip_today.place, trip_today.omin, trip_today.omax, trip_today.vmin, (unsigned)trips.size()); }
    else if (cmd == "backup") sd_job(J_BACKUP);
    else if (cmd == "ip") { Serial.printf("ip %s ap %s\n", WiFi.localIP().toString().c_str(), WiFi.softAPIP().toString().c_str()); }
    else if (cmd == "wake") app::screen_wake();
    else if (cmd == "shot") {          // exact RGB565 frame over serial, in the pieces LVGL draws
      Serial.printf("SHOT %d %d\n", W, H);
      shot_mode = true;
      lv_obj_invalidate(lv_screen_active()); lv_obj_invalidate(lv_layer_top());
      lv_refr_now(nullptr);
      shot_mode = false;
      Serial.println("\nEND");
    }
    else if (cmd.startsWith("pv ")) ui::power_view_test(cmd.substring(3).toInt());
    else if (cmd.startsWith("bs ")) ui::bank_settings_test(cmd.substring(3).toInt());
    else if (cmd == "bank") { app::Lock l; auto &b = app::S.bank; Serial.printf("bank conn=%d soc=%.1f in=%d out=%d usb=%d dc=%d ac=%d light=%d key=%d screen=%d acsb=%d idle=%d\n", b.connected, b.soc, b.in_w, b.out_w, b.usb, b.dc, b.ac, b.light_mode, b.s_key, b.s_screen, b.s_ac_sb, b.s_idle); }
    else if (cmd == "tasks") Serial.printf("stack free: radio %u sd %u web %u loop %u\n", uxTaskGetStackHighWaterMark(task_radio), uxTaskGetStackHighWaterMark(task_sd), task_web ? uxTaskGetStackHighWaterMark(task_web) : 0, uxTaskGetStackHighWaterMark(nullptr));
    else if (cmd == "heap") {
      lv_mem_monitor_t m; lv_mem_monitor(&m);
      Serial.printf("heap %u min %u big %u | screen pool free %u max used %u big %u, on heap %u (max %u)\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), (unsigned)m.free_size, (unsigned)m.max_used, (unsigned)m.free_biggest_size, (unsigned)lv_mem_spilled(), (unsigned)lv_mem_spilled_max());
    }
  }
}

static void lvgl_begin() {
  lv_init();
  lv_tick_set_cb(tick_cb);
  lv_display_t *disp = lv_display_create(W, H);
  size_t sz = W * BUF_LINES * 2;
  void *b1 = heap_caps_malloc(sz, MALLOC_CAP_DMA);
  if (!b1) { Serial.println("no RAM for the screen buffer, restarting"); delay(1000); ESP.restart(); }
  lv_display_set_buffers(disp, b1, nullptr, sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_flush_wait_cb(disp, [](lv_display_t *) { tft.waitDMA(); });
  lv_indev_t *in = lv_indev_create();
  lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(in, read_touch);
  Serial.printf("heap after display %u\n", (unsigned)ESP.getFreeHeap());
}

void setup() {
  Serial.begin(115200);
  ota_boot_guard();
  backlight::begin();
  led::begin();
  led::set(0, 0, 0);
  speaker::begin();
  touch::begin();
  analogSetPinAttenuation(ldr::PIN, ADC_11db);
  load_settings();
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(0x0000);
  tft.initDMA();

  // LVGL takes its 40 KB pool here. When an update was asked for, that waits until after the GitHub check:
  // the secure download needs about 40 KB in one piece.
  bool upd = app::ota_wanted();
  if (!upd) lvgl_begin();
  Serial.printf("heap after display %u\n", (unsigned)ESP.getFreeHeap());
  web_begin_code();
  web_enable(cfg.web);
  ap_enable(cfg.ap);
  radio_begin();
  {                                                    // the update check at start needs the RAM the screen takes later
    tft.setTextColor(0x8C71, 0x0000); tft.setFont(&fonts::Font2); tft.setTextDatum(lgfx::middle_center);
    tft.drawString("Camper hub " FW_VERSION, 160, 110); tft.drawString(upd ? "looking for an update, please wait..." : "starting...", 160, 130);
    uint32_t t0 = millis();
    while (!app::ota_done && millis() - t0 < (upd ? 300000UL : 20000UL)) delay(50);
    tft.setTextDatum(lgfx::top_left);
  }
  if (upd) lvgl_begin();
  delay(500);
  Serial.printf("heap after radio %u\n", (unsigned)ESP.getFreeHeap());
  logger_begin();
  web_start_task();
  delay(1500);
  Serial.printf("heap after sd %u\n", (unsigned)ESP.getFreeHeap());
  level_begin();
  ui::begin();
  Serial.printf("heap after ui %u\n", (unsigned)ESP.getFreeHeap());
  last_touch = millis();
  Serial.printf("CAMPER READY heap %u\n", (unsigned)ESP.getFreeHeap());
  enableLoopWDT();                                     // a frozen screen restarts by itself (and counts for the update guard)
}

void loop() {
  console();
  sound();
  uint32_t wait = lv_timer_handler();
  if (tft.getStartCount() > 0) tft.endWrite();
  if (want_wake) { want_wake = false; dimmed = dark = false; ui::saver_show(false); }
  uint32_t idle = millis() - last_touch;
  uint32_t dim_at = SAVER_MIN[cfg.saver] ? SAVER_MIN[cfg.saver] * 60000UL : DIM_AFTER;
  if (!dimmed && idle > dim_at && !ui::alarm_active()) { dimmed = true; if (SAVER_MIN[cfg.saver]) ui::saver_show(true); }
  if (dimmed && !dark && OFF_MIN[cfg.scr_off] && idle > dim_at + OFF_MIN[cfg.scr_off] * 60000UL) dark = true;
  static uint32_t ls = 0;
  bool sv = ui::saver_on();
  static bool tried = false;                            // once per screensaver start (no photos: camper scene)
  if (!sv) tried = false;
  if (sv && !photo_mode && cfg.saver_look == 1 && !dark && !tried) {
    tried = true;
    if (app::photos_begin()) { if (tft.getStartCount()) { tft.waitDMA(); tft.endWrite(); } photo_mode = true; }
  }
  if (photo_mode && (!sv || cfg.saver_look != 1)) {     // woken up: LVGL paints the whole screen again
    photo_mode = false;
    lv_obj_invalidate(lv_screen_active()); lv_obj_invalidate(lv_layer_top()); lv_obj_invalidate(lv_layer_sys());
  }
  static bool boot_ok = false;
  if (!boot_ok && millis() > 180000) { boot_ok = true; ota_boot_ok(); }
  if (photo_mode && !dark) app::photos_tick();
  else if (sv && !photo_mode && !dark && millis() - ls > 2000) { ls = millis(); ui::saver_tick(); }
  static uint32_t lb = 0, la = 0;
  if (millis() - lb > 100) { lb = millis(); brightness(); }
  if (millis() - la > 500) { la = millis(); bool on = ui::alarm_active() && (millis() / 500) % 2; led::set(on, 0, 0); }
  delay(wait < 5 ? wait : 5);
}
