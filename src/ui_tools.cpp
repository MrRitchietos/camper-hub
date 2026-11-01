// TOOLS: departure / arrival checklists (tap = tick, hold = delete, + = add) and all settings.
#include <Preferences.h>
#include <WiFi.h>
#include "ui_kit.hpp"

namespace app { extern char ota_msg[48]; void ota_request(); }
namespace ui {

static Seg seg;
static lv_obj_t *v_check, *v_set, *c_prog, *c_bar, *c_fill;
static Seg cseg;
static lv_obj_t *items_dl, *items_empty;   // checklist: one drawn list
static lv_obj_t *set_dl;
struct SetRow { char title[40], val[40]; lv_color_t c; };
static SetRow srow[32];
static Preferences cp;
static int list_i = 0;                       // 0 leaving, 1 arriving
static std::vector<String> L[2];
static uint32_t done_mask[2];
static bool dirty = true, lists_loaded = false;
static lv_obj_t *v_trip, *trip_dl, *trip_info;
static std::vector<TripDay> tcopy;          // painted copy of the trip diary (today first)
static uint32_t tver = UINT32_MAX;

static const char *DEF0[] = {"Roof vents + skylights closed", "Windows closed and locked", "Awning in", "Steps in",
                             "TV antenna / dish down", "Gas off", "Fridge on 12 V, door locked", "Loose items stowed",
                             "Water pump off", "Hook-up cable unplugged", "Levelling blocks packed", "Doors and lockers locked"};
static const char *DEF1[] = {"Level the camper", "Hand brake + wheel chocks", "Hook-up cable in", "Gas on",
                             "Fridge on gas / 230 V", "Water pump on", "Awning out (check wind)", "Steps out"};

static void save_list(int k) {
  char key[8];
  cp.putUChar(k ? "n1" : "n0", L[k].size());
  for (size_t i = 0; i < L[k].size(); i++) { snprintf(key, sizeof key, "%c%u", k ? 'b' : 'a', (unsigned)i); cp.putString(key, L[k][i]); }
  cp.putULong(k ? "m1" : "m0", done_mask[k]);
}
static void load_lists() {
  cp.begin("check", false);
  for (int k = 0; k < 2; k++) {
    int n = cp.getUChar(k ? "n1" : "n0", 255);
    L[k].clear();
    if (n == 255) {
      const char **d = k ? DEF1 : DEF0; int dn = k ? 8 : 12;
      for (int i = 0; i < dn; i++) L[k].push_back(d[i]);
      done_mask[k] = 0;
      save_list(k);
    } else {
      char key[8];
      for (int i = 0; i < n; i++) { snprintf(key, sizeof key, "%c%d", k ? 'b' : 'a', i); L[k].push_back(cp.getString(key, "?")); }
      done_mask[k] = cp.getULong(k ? "m1" : "m0", 0);
    }
  }
}
static void item_tap(int i) {
  if (i < 0 || i >= (int)L[list_i].size() || i >= 32) return;
  done_mask[list_i] ^= 1u << i;
  cp.putULong(list_i ? "m1" : "m0", done_mask[list_i]);
  dirty = true;
  int n = L[list_i].size();
  if (n && done_mask[list_i] == (n >= 32 ? 0xFFFFFFFF : (1u << n) - 1)) { toast(list_i ? "All set, enjoy!" : "All done, drive safe!"); beep(1); }
}
static int del_i = -1;
static void item_hold(int i) {
  del_i = i;
  if (del_i < 0 || del_i >= (int)L[list_i].size()) return;
  lv_obj_t *m = modal(LV_SYMBOL_TRASH "  Delete item?");
  lv_obj_t *t = label(m, F16, C_WHITE, L[list_i][del_i].c_str());
  lv_obj_set_width(t, 300);
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(t, 12, 48);
  lv_obj_set_pos(button(m, "Delete", 146, 40, C_RED, [](lv_event_t *) {
    auto &v = L[list_i];
    if (del_i >= 0 && del_i < (int)v.size()) {
      uint32_t m = done_mask[list_i], lo = m & ((1u << del_i) - 1), hi = (m >> (del_i + 1)) << del_i;
      done_mask[list_i] = lo | hi;
      v.erase(v.begin() + del_i);
      save_list(list_i);
    }
    close_modal(); dirty = true;
  }), 8, 120);
  lv_obj_set_pos(button(m, "Keep", 146, 40, C_CARD2, [](lv_event_t *) { close_modal(); }), 166, 120);
}
static void add_done(const char *s) {
  if (!*s) return;
  if (L[list_i].size() >= 24) { toast("List is full (24)"); return; }
  L[list_i].push_back(s);
  save_list(list_i);
  dirty = true;
}
static void cseg_cb(lv_event_t *e) { list_i = (int)(intptr_t)lv_event_get_user_data(e); seg_set(cseg, list_i); dirty = true; }

// ---------------------------------------------------------------- settings rows
static void open_upd();
enum { S_PLACE, S_WIFI, S_WEB, S_PASS, S_AP, S_KEY, S_BATT, S_ALARM, S_BEEP, S_BRIGHT, S_THEME, S_SAVER, S_LOOK, S_SCROFF, S_LEN, S_WIDTH, S_TAG, S_ROT, S_DEMO, S_SD, S_UPD, S_INFO, S_N };
static void open_sd(); static void open_web();
static const int16_t ALARMS[] = {0, 1150, 1180, 1200, 1220, 1250, 1280};
static void set_tap(int k) {
  uint8_t b; int16_t a; { Lock l; b = S.vic.batt; a = S.vic.alarm_cv; }
  switch (k) {
    case S_PLACE: open_place(); break;
    case S_WIFI: wifi_picker(); break;
    case S_KEY: power_open_key(); break;
    case S_BATT: vic_settings((b + 1) % 3, a); break;
    case S_ALARM: { int i = 0; while (i < 7 && ALARMS[i] != a) i++; vic_settings(b, ALARMS[(i + 1) % 7]); break; }
    case S_BEEP: cfg.beep = !cfg.beep; save_settings(); if (cfg.beep) beep(1); break;
    case S_BRIGHT: cfg.bright = (cfg.bright + 1) % 4; save_settings(); break;
    case S_SAVER: cfg.saver = (cfg.saver + 1) % 6; save_settings(); break;
    case S_THEME: cfg.look_mode = (cfg.look_mode + 1) % 3; save_settings(); look_request(); break;
    case S_LOOK: {
      cfg.saver_look ^= 1; save_settings();
      if (cfg.saver_look) { int n = photos_count(); char m[72];
        if (n) snprintf(m, sizeof m, "Screensaver shows your %d photos", n); else strcpy(m, "No photos yet: put JPG files in camper/photos");
        toast(m); }
      break;
    }
    case S_WEB:
      cfg.web = !cfg.web; save_settings(); web_enable(cfg.web);
      if (cfg.web) open_web(); else { if (cfg.ap) { cfg.ap = false; save_settings(); ap_enable(false); } toast("Phone page off (no memory used)"); }
      break;
    case S_PASS:
      open_text(LV_SYMBOL_EDIT "  Phone page password", "8 to 32 characters", 32, nullptr, [](const char *p) {
        if (!p[0]) return;
        if (!web_set_pass(p)) { toast("Use 8 to 32 letters or numbers"); return; }
        ap_new_pass();
        toast("Password saved");
      });
      break;
    case S_AP: cfg.ap = !cfg.ap; if (cfg.ap && !cfg.web) { cfg.web = true; web_enable(true); } save_settings(); ap_enable(cfg.ap);
               if (cfg.ap) open_web(); else toast("Own WiFi off"); break;
    case S_SD: open_sd(); break;
    case S_UPD: open_upd(); break;
    case S_SCROFF: cfg.scr_off = (cfg.scr_off + 1) % 5; save_settings(); break;
    case S_LEN: cfg.len_cm = cfg.len_cm >= 450 ? 300 : cfg.len_cm + 5; save_settings(); break;
    case S_WIDTH: cfg.width_cm = cfg.width_cm >= 200 ? 150 : cfg.width_cm + 5; save_settings(); break;
    case S_TAG: level_next_tag(); break;
    case S_ROT: cfg.lvl_rot = (cfg.lvl_rot + 1) % 4; save_settings(); break;
    case S_DEMO: set_demo(!demo_on); toast(demo_on ? "Demo on: everything is simulated" : "Demo off"); break;
    case S_INFO: break;
  }
  dirty = true;
}

static void tools_view(int i) {
  seg_set(seg, i); show(v_check, i == 0); show(v_trip, i == 1); show(v_set, i == 2); dirty = true;
  trips_request(i == 1);                       // the diary only sits in RAM while it is shown
  if (i != 1) { std::vector<TripDay>().swap(tcopy); tver = UINT32_MAX; if (trip_dl) drawlist_count(trip_dl, 0); }
}
void tools_view_test(int i) { if (v_check && lv_obj_is_valid(v_check)) tools_view(i); }
void tools_leave() { trips_request(false); std::vector<TripDay>().swap(tcopy); tver = UINT32_MAX; }

// ---------------------------------------------------------------- trip diary rows
lv_color_t temp_color(float t);
static void trip_row(lv_layer_t *L, const lv_area_t &r, int i) {
  if (i < 0 || i >= (int)tcopy.size()) return;
  const TripDay &d = tcopy[i];
  static const char *WD[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *MO[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  struct tm t{}; t.tm_year = atoi(d.date) - 1900; t.tm_mon = atoi(d.date + 5) - 1; t.tm_mday = atoi(d.date + 8); t.tm_hour = 12;
  mktime(&t);
  char b[64];
  if (d.place[0] || d.wmax || d.wmin) dl_icon(L, r.x1 + 8, r.y1 + 7, d.code, 24);
  snprintf(b, sizeof b, "%s%s %d %s", i == 0 ? "Today, " : "", WD[t.tm_wday % 7], t.tm_mday, MO[t.tm_mon % 12]);
  dl_text(L, r, 40, 3, F14, i == 0 ? C_TEAL : C_WHITE, b);
  dl_text(L, r, 40, 21, F12, C_DIM, d.place[0] ? d.place : "place unknown");
  float lo = !isnan(d.omin) ? d.omin : d.wmin, hi = !isnan(d.omax) ? d.omax : d.wmax;
  if (!isnan(d.omin) || d.wmax || d.wmin) { snprintf(b, sizeof b, "%.0f..%.0f\xC2\xB0", lo, hi); dl_text(L, r, 10, 3, F14, temp_color(hi), b, true); }
  size_t k = 0; b[0] = 0;
  if (!isnan(d.kwh)) k += snprintf(b + k, sizeof b - k, LV_SYMBOL_CHARGE "%.2f kWh  ", d.kwh);
  if (!isnan(d.vmin)) k += snprintf(b + k, sizeof b - k, "%.1f V  ", d.vmin);
  if (d.rain >= 1) k += snprintf(b + k, sizeof b - k, LV_SYMBOL_TINT "%.0f mm", d.rain);
  dl_text(L, r, 10, 22, F12, C_DIM, b, true);
}

// ---------------------------------------------------------------- phone page + SD card
static lv_obj_t *web_modal, *web_l, *sd_modal, *sd_l, *sd_msg, *sd_upd;
static void web_text() {
  char b[200], ip[20] = "";
  bool w; { Lock l; w = S.wifi; if (w) { IPAddress a(S.ip); strlcpy(ip, a.toString().c_str(), sizeof ip); } }
  size_t k = snprintf(b, sizeof b, "On your phone, open:\n");
  if (w) k += snprintf(b + k, sizeof b - k, "   http://%s   (same WiFi)\n", ip);
  if (cfg.ap) k += snprintf(b + k, sizeof b - k, "   or join WiFi CamperHub, then http://192.168.4.1\n");
  if (!w && !cfg.ap) k += snprintf(b + k, sizeof b - k, "   (no WiFi now: turn on Own WiFi)\n");
  snprintf(b + k, sizeof b - k, "User: camper    Password: %s", web_code());
  set_txt(web_l, b);
}
static void open_web() {
  lv_obj_t *m = modal(LV_SYMBOL_WIFI "  Phone page");
  modal_owner(&web_modal, m);
  web_l = label(m, F14, C_WHITE, "");
  lv_obj_set_width(web_l, 304);
  lv_label_set_long_mode(web_l, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(web_l, 10, 40);
  web_text();
  lv_obj_t *hint = label(m, F12, C_DIM, "Live values, power bank, all settings, checklists, photos and log files. "
                                         "The own WiFi CamperHub uses the same password. Change it in Settings > Phone login.");
  lv_obj_set_width(hint, 304);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(hint, 10, 146);
  lv_obj_set_pos(button(m, "Turn phone page off", 190, 32, C_CARD2, [](lv_event_t *) {
    cfg.web = false; cfg.ap = false; save_settings(); web_enable(false); ap_enable(false); close_modal(); toast("Phone page off");
  }), 10, 200);
  lv_obj_set_pos(button(m, "OK", 100, 32, C_TEAL, [](lv_event_t *) { close_modal(); }), 210, 200);
}
static void open_upd() {
  lv_obj_t *m = modal(LV_SYMBOL_DOWNLOAD "  Update");
  char b[160];
  snprintf(b, sizeof b, "This is version %s.%s%s\n\nThe board restarts, looks on GitHub for a new version and installs it. "
           "That takes about 1 to 3 minutes. Settings and logs stay.", FW_VERSION, ota_msg[0] ? "\nLast check: " : "", ota_msg);
  lv_obj_t *l = label(m, F14, C_WHITE, b);
  lv_obj_set_width(l, 304);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(l, 10, 40);
  lv_obj_set_pos(button(m, "Cancel", 100, 32, C_CARD2, [](lv_event_t *) { close_modal(); }), 10, 200);
  lv_obj_set_pos(button(m, "Check and install", 190, 32, C_TEAL, [](lv_event_t *) { close_modal(); ota_request(); }), 120, 200);
}
static uint32_t sdv = UINT32_MAX;
void sd_modal_update() {
  if (g_modal && g_modal == web_modal) { static uint32_t t = 0; if (millis() - t > 2000) { t = millis(); web_text(); } return; }
  if (!g_modal || g_modal != sd_modal) return;
  SdInfo i; char use[40]; bool ok;
  { Lock l; if (sdv == SDI.ver) return; sdv = SDI.ver; i = SDI; strlcpy(use, S.sd_msg, sizeof use); ok = S.sd_ok; }
  char b[200];
  if (!ok) snprintf(b, sizeof b, "No card found. Use a FAT32 card (up to 32 GB, or let the board format a bigger one).");
  else snprintf(b, sizeof b, "%s, %d files\nSettings backup: %s\nUpdate file: %s", use, i.files, i.backup ? i.backup_at : "none yet",
                i.update ? "found, ready to install" : "none (put update.bin in camper)");
  set_txt(sd_l, b);
  set_txt(sd_msg, i.msg);
  show(sd_upd, i.update);
}
static void open_sd() {
  lv_obj_t *m = modal(LV_SYMBOL_SD_CARD "  SD card");
  modal_owner(&sd_modal, m);
  sd_l = label(m, F14, C_WHITE, "");
  lv_obj_set_width(sd_l, 304);
  lv_label_set_long_mode(sd_l, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(sd_l, 10, 40);
  sd_msg = label(m, F14, C_AMBER, "");
  lv_obj_set_width(sd_msg, 304);
  lv_label_set_long_mode(sd_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(sd_msg, 10, 120);
  lv_obj_set_pos(button(m, LV_SYMBOL_SAVE " Back up", 96, 34, C_CARD2, [](lv_event_t *) { sd_job(J_BACKUP); }), 8, 196);
  lv_obj_set_pos(button(m, LV_SYMBOL_UPLOAD " Restore", 100, 34, C_CARD2, [](lv_event_t *) {
    lv_obj_t *c = modal(LV_SYMBOL_WARNING "  Restore settings?");
    lv_obj_t *t = label(c, F14, C_WHITE, "All settings come back from the backup on the card and the board restarts.");
    lv_obj_set_width(t, 300); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_pos(t, 10, 48);
    lv_obj_set_pos(button(c, "Restore", 146, 40, C_AMBER, [](lv_event_t *) { sd_job(J_RESTORE); close_modal(); toast("Restoring..."); }), 8, 150);
    lv_obj_set_pos(button(c, "Cancel", 146, 40, C_CARD2, [](lv_event_t *) { close_modal(); }), 166, 150);
  }), 110, 196);
  sd_upd = button(m, LV_SYMBOL_DOWNLOAD " Install", 100, 34, C_TEAL, [](lv_event_t *) {
    lv_obj_t *c = modal(LV_SYMBOL_DOWNLOAD "  Install update?");
    lv_obj_t *t = label(c, F14, C_WHITE, "The new version from the card is installed and the board restarts (about 30 s). Do not switch off.");
    lv_obj_set_width(t, 300); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_pos(t, 10, 48);
    lv_obj_set_pos(button(c, "Install", 146, 40, C_TEAL, [](lv_event_t *) { sd_job(J_UPDATE); close_modal(); toast("Installing, do not switch off"); }), 8, 150);
    lv_obj_set_pos(button(c, "Cancel", 146, 40, C_CARD2, [](lv_event_t *) { close_modal(); }), 166, 150);
  });
  lv_obj_set_pos(sd_upd, 212, 196);
  show(sd_upd, false);
  sdv = UINT32_MAX;
  sd_job(J_NONE);
  sd_modal_update();
}

// ---------------------------------------------------------------- commands from the phone page (UI task)
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
void web_drain() {
  WebCmd c;
  while (web_cmd_pop(c)) {
    if (!strcmp(c.op, "set")) {
      const char *k = c.key; int v = c.a;
      uint8_t b; int16_t a; { Lock l; b = S.vic.batt; a = S.vic.alarm_cv; }
      if (!strcmp(k, "bright")) cfg.bright = clampi(v, 0, 3);
      else if (!strcmp(k, "beep")) cfg.beep = v;
      else if (!strcmp(k, "saver")) cfg.saver = clampi(v, 0, 5);
      else if (!strcmp(k, "scroff")) cfg.scr_off = clampi(v, 0, 4);
      else if (!strcmp(k, "look")) cfg.saver_look = clampi(v, 0, 1);
      else if (!strcmp(k, "thm")) { cfg.look_mode = clampi(v, 0, 2); look_request(); }
      else if (!strcmp(k, "wb")) cfg.len_cm = clampi(v, 250, 500);
      else if (!strcmp(k, "trk")) cfg.width_cm = clampi(v, 120, 250);
      else if (!strcmp(k, "rot")) cfg.lvl_rot = clampi(v, 0, 3);
      else if (!strcmp(k, "batt")) vic_settings(clampi(v, 0, 2), a);
      else if (!strcmp(k, "alarm")) vic_settings(b, v == 0 ? 0 : clampi(v, 1000, 1400));
      else if (!strcmp(k, "demo")) { if ((bool)v != demo_on) set_demo(v); }
      else if (!strcmp(k, "ap")) { cfg.ap = v; ap_enable(cfg.ap); }
      else if (!strcmp(k, "zero")) level_zero();
      else if (!strcmp(k, "tag")) level_next_tag();
      save_settings();
    } else if (!strcmp(c.op, "chk")) {
      if (!lists_loaded) { load_lists(); lists_loaded = true; }
      int k = c.b & 1, i = c.a; auto &v = L[k];
      if (!strcmp(c.key, "tick") && i >= 0 && i < (int)v.size()) { done_mask[k] ^= 1u << i; cp.putULong(k ? "m1" : "m0", done_mask[k]); }
      else if (!strcmp(c.key, "reset")) { done_mask[k] = 0; cp.putULong(k ? "m1" : "m0", 0); }
      else if (!strcmp(c.key, "add") && c.text[0] && v.size() < 24) { v.push_back(c.text); save_list(k); }
      else if (!strcmp(c.key, "del") && i >= 0 && i < (int)v.size()) {
        uint32_t m = done_mask[k], lo = m & ((1u << i) - 1), hi = (m >> (i + 1)) << i;
        done_mask[k] = lo | hi; v.erase(v.begin() + i); save_list(k);
      } else if (!strcmp(c.key, "edit") && c.text[0] && i >= 0 && i < (int)v.size()) { v[i] = c.text; save_list(k); }
    } else if (!strcmp(c.op, "bank")) bank_switch(clampi(c.a, 0, 3), c.b);
    else if (!strcmp(c.op, "bset")) bank_set(c.a, c.b);
    else if (!strcmp(c.op, "vkey")) toast(vic_set_key(c.text) ? (c.text[0] ? "Victron key saved" : "Victron key removed") : "Victron key not valid");
    else if (!strcmp(c.op, "place")) {
      Place p; bool ok = false;
      { Lock l; if (!strcmp(c.key, "hit") && c.a >= 0 && c.a < P.n_hits) { p = P.hits[c.a]; ok = true; }
                else if (!strcmp(c.key, "recent") && c.a >= 0 && c.a < P.n_recent) { p = P.recent[c.a]; ok = true; } }
      if (ok) place_pin(p); else if (!strcmp(c.key, "auto")) place_auto();
    }
    dirty = true;
  }
}

void tools_build(lv_obj_t *t) {
  seg = Seg{}; cseg = Seg{}; dirty = true;
  static const char *N[] = {LV_SYMBOL_OK " Checklist", LV_SYMBOL_LIST " Trips", LV_SYMBOL_SETTINGS " Settings"};
  seg_make(seg, t, 6, 2, 308, 26, N, 3, [](lv_event_t *e) { tools_view((int)(intptr_t)lv_event_get_user_data(e)); });
  if (!lists_loaded) { load_lists(); lists_loaded = true; }
  v_check = box(t, 320, PAGE_H - 30, C_BG);
  lv_obj_set_pos(v_check, 0, 30);
  static const char *CN[] = {"Leaving", "Arriving"};
  seg_make(cseg, v_check, 4, 0, 170, 24, CN, 2, cseg_cb);
  c_prog = label(v_check, F12, C_DIM, "");
  lv_obj_set_pos(c_prog, 182, 5);
  c_bar = bar_make(v_check, 224, 9, 44, 6, &c_fill);
  lv_obj_t *add = button(v_check, LV_SYMBOL_PLUS, 20, 24, C_CARD2, [](lv_event_t *) {
    open_text(LV_SYMBOL_PLUS "  New checklist item", "e.g. Bikes locked on the rack", 40, nullptr, add_done);
  });
  lv_obj_set_pos(add, 272, 0);
  lv_obj_t *rst = button(v_check, LV_SYMBOL_REFRESH, 20, 24, C_CARD2, [](lv_event_t *) {
    done_mask[list_i] = 0; cp.putULong(list_i ? "m1" : "m0", 0); dirty = true; toast("Checklist reset");
  });
  lv_obj_set_pos(rst, 296, 0);
  items_dl = drawlist(v_check, 4, 28, 312, PAGE_H - 30 - 28, 32, [](lv_layer_t *Ly, const lv_area_t &r, int i) {
    auto &v = L[list_i];
    if (i >= (int)v.size()) return;
    bool d = i < 32 && (done_mask[list_i] >> i & 1);
    dl_dot(Ly, r, 7, 7, 6, 14, d ? C_GREEN : C_CARD2);
    dl_text(Ly, r, 20, 5, F14, d ? C_FAINT : C_WHITE, v[i].c_str());
    if (d) dl_text(Ly, r, 10, 5, F14, C_GREEN, LV_SYMBOL_OK, true);
  });
  drawlist_on_tap(items_dl, item_tap);
  drawlist_on_hold(items_dl, item_hold);
  items_empty = label(v_check, F14, C_DIM, "Empty list: tap + to add items.");
  lv_obj_set_pos(items_empty, 12, 40);

  v_set = box(t, 320, PAGE_H - 30, C_BG);
  lv_obj_set_pos(v_set, 0, 30);
  set_dl = drawlist(v_set, 4, 0, 312, PAGE_H - 30, 30, [](lv_layer_t *L, const lv_area_t &r, int i) {
    dl_text(L, r, 12, 6, F14, C_WHITE, srow[i].title);
    dl_text(L, r, 10, 6, F14, srow[i].c, srow[i].val, true);
  });
  drawlist_on_tap(set_dl, set_tap);
  show(v_set, false);

  v_trip = box(t, 320, PAGE_H - 30, C_BG);
  lv_obj_set_pos(v_trip, 0, 30);
  trip_dl = drawlist(v_trip, 4, 0, 312, PAGE_H - 30, 40, trip_row);
  trip_info = label(v_trip, F14, C_DIM, "");
  lv_obj_set_width(trip_info, 296);
  lv_label_set_long_mode(trip_info, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(trip_info, 12, 60);
  show(v_trip, false);
  tver = UINT32_MAX;
}

static void set_row(int k, const char *title, const char *val, lv_color_t c = C_AMBER) {
  strlcpy(srow[k].title, title, sizeof srow[k].title);
  strlcpy(srow[k].val, val, sizeof srow[k].val);
  srow[k].c = c;
}

void tools_update(bool force) {
  static uint32_t last = 0;
  if (!force && !dirty && millis() - last < 1000) return;
  last = millis(); dirty = false;
  if (seg.active == 0) {
    auto &v = L[list_i];
    int done = 0;
    for (int i = 0; i < (int)v.size() && i < 32; i++) done += done_mask[list_i] >> i & 1;
    drawlist_count(items_dl, v.size());
    show(items_empty, v.empty());
    set_fmt(c_prog, "%d/%d", done, (int)v.size());
    bar_fill(c_bar, c_fill, v.size() ? done * 100 / (int)v.size() : 0, done == (int)v.size() ? C_GREEN : C_TEAL);
    return;
  }
  if (seg.active == 1) {
    bool sd;
    {
      Lock l; sd = S.sd_ok;
      if (tver == trips_ver) return;
      tver = trips_ver;
      tcopy.clear();
      if (trip_today.date[0]) tcopy.push_back(trip_today);
      for (auto &d : trips) { if (tcopy.size() >= 31) break; if (strcmp(d.date, trip_today.date)) tcopy.push_back(d); }
    }
    drawlist_count(trip_dl, tcopy.size());
    show(trip_info, tcopy.size() < 2);
    set_txt(trip_info, !sd ? "The trip diary needs the SD card." :
            tcopy.empty() ? "Your trip diary starts here: every day the place, weather, temperatures and solar are saved on the SD card."
                          : "Every day is added here by itself: place, weather, temperatures, solar and battery.");
    lv_obj_set_pos(trip_info, 12, tcopy.empty() ? 20 : 60);
    return;
  }
  static const char *BT[] = {"AGM / lead", "Gel", "LiFePO4"};
  static const char *BRIGHT_N[] = {"Auto", "Low", "Medium", "High"};
  static const char *ROT[] = {"0\xC2\xB0", "90\xC2\xB0", "180\xC2\xB0", "270\xC2\xB0"};
  char b[48];
  Lock l;
  { set_row(S_PLACE, LV_SYMBOL_GPS "  Weather place", P.pinned ? P.cur.name : "auto", P.pinned ? C_GREEN : C_DIM); }
  { char w[48]; snprintf(w, sizeof w, "%s%s", S.wifi ? S.ssid : "offline", S.portal ? " (login page)" : S.wifi && !S.online ? " (no internet)" : "");
    set_row(S_WIFI, LV_SYMBOL_WIFI "  WiFi", w, S.portal ? C_AMBER : S.wifi ? C_TEAL : C_DIM); }
  { char ip[24] = "off"; if (cfg.web) { if (S.wifi) { IPAddress a(S.ip); strlcpy(ip, a.toString().c_str(), sizeof ip); } else strcpy(ip, cfg.ap ? "192.168.4.1" : "on (no WiFi)"); }
    set_row(S_WEB, LV_SYMBOL_EYE_OPEN "  Phone page", ip, cfg.web ? C_TEAL : C_DIM); }
  set_row(S_PASS, LV_SYMBOL_EDIT "  Phone login", "camper / change", C_DIM);
  set_row(S_AP, LV_SYMBOL_WIFI "  Own WiFi", cfg.ap ? "CamperHub" : "off", cfg.ap ? C_TEAL : C_DIM);
  set_row(S_KEY, LV_SYMBOL_CHARGE "  Victron key", S.vic.have_key ? "set" : "enter", S.vic.have_key ? C_GREEN : C_AMBER);
  set_row(S_BATT, LV_SYMBOL_BATTERY_FULL "  Battery type", BT[S.vic.batt % 3]);
  if (S.vic.alarm_cv) snprintf(b, sizeof b, "below %d.%02d V", S.vic.alarm_cv / 100, S.vic.alarm_cv % 100); else strcpy(b, "off");
  set_row(S_ALARM, LV_SYMBOL_WARNING "  Low battery alarm", b);
  set_row(S_BEEP, LV_SYMBOL_BELL "  Alarm sound", cfg.beep ? "on" : "off");
  set_row(S_BRIGHT, LV_SYMBOL_EYE_OPEN "  Brightness", BRIGHT_N[cfg.bright % 4]);
  if (SAVER_MIN[cfg.saver]) snprintf(b, sizeof b, "after %d min", SAVER_MIN[cfg.saver]); else strcpy(b, "off");
  set_row(S_SAVER, LV_SYMBOL_IMAGE "  Screensaver", b, SAVER_MIN[cfg.saver] ? C_AMBER : C_DIM);
  if (OFF_MIN[cfg.scr_off]) snprintf(b, sizeof b, "%d min later", OFF_MIN[cfg.scr_off]); else strcpy(b, "never");
  { static const char *TM[] = {"auto", "day", "night"}; char b[24]; snprintf(b, sizeof b, "%s (%s now)", TM[cfg.look_mode], look_day() ? "day" : "night");
    set_row(S_THEME, LV_SYMBOL_IMAGE "  Day / night look", b, C_DIM); }
  set_row(S_LOOK, LV_SYMBOL_IMAGE "  Screensaver look", cfg.saver_look ? "my photos" : "camper", cfg.saver_look ? C_TEAL : C_DIM);
  set_row(S_SCROFF, LV_SYMBOL_POWER "  Screen off", b);
  snprintf(b, sizeof b, "%.2f m", cfg.len_cm / 100.0f); set_row(S_LEN, LV_SYMBOL_GPS "  Wheelbase", b);
  snprintf(b, sizeof b, "%.2f m", cfg.width_cm / 100.0f); set_row(S_WIDTH, LV_SYMBOL_GPS "  Track (wheel to wheel)", b);
  set_row(S_TAG, LV_SYMBOL_GPS "  Level RuuviTag", S.lvl_tag[0] ? S.lvl_tag + 9 : "none heard", S.lvl_tag[0] ? C_AMBER : C_DIM);
  set_row(S_ROT, LV_SYMBOL_LOOP "  Level tag turned", ROT[cfg.lvl_rot % 4]);
  set_row(S_DEMO, LV_SYMBOL_PLAY "  Demo mode", demo_on ? "on" : "off", demo_on ? C_AMBER : C_DIM);
  set_row(S_SD, LV_SYMBOL_SD_CARD "  SD card", S.sd_ok ? S.sd_msg : "no card", S.sd_ok ? C_DIM : C_RED);
  set_row(S_UPD, LV_SYMBOL_DOWNLOAD "  Update", ota_msg[0] ? ota_msg : "check now", C_DIM);
  snprintf(b, sizeof b, "%u KB free", (unsigned)(ESP.getFreeHeap() / 1024));
  set_row(S_INFO, LV_SYMBOL_SETTINGS "  Camper hub " FW_VERSION, b, C_DIM);
  drawlist_count(set_dl, S_N);
}

// ---------------------------------------------------------------- wifi picker
static Pool wp;
static lv_obj_t *wp_modal = nullptr;
static uint32_t wp_ver = UINT32_MAX;
static char wp_ssid[33];
static char ap_names[30][33];
static bool ap_open[30];
static void pass_done(const char *p) { connect_to(wp_ssid, p); toast("Connecting..."); }
static void ap_tap(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  strlcpy(wp_ssid, ap_names[i], sizeof wp_ssid);
  if (ap_open[i]) { close_modal(); connect_to(wp_ssid, ""); toast("Connecting..."); return; }
  char t[48]; snprintf(t, sizeof t, LV_SYMBOL_WIFI "  %s", wp_ssid);
  open_text(t, "password", 63, nullptr, pass_done);
}
void wifi_picker() {
  lv_obj_t *m = modal(LV_SYMBOL_WIFI "  Choose WiFi");
  wp = Pool{};
  wp.list = page_scroll(m, 34, 206);
  wp.h = 30; wp.cb = ap_tap;
  modal_owner(&wp_modal, m);
  wp_ver = UINT32_MAX;
  request_wifi_scan();
}
void wifi_picker_update() {
  if (!g_modal || g_modal != wp_modal) return;
  Lock l;
  if (wp_ver == S.scan_ver && !S.scanning) return;
  if (S.scanning && wp_ver != UINT32_MAX && wp_ver == S.scan_ver) return;
  wp_ver = S.scan_ver;
  int i = 0;
  for (auto &a : S.aps) {
    if (i >= 30) break;
    strlcpy(ap_names[i], a.ssid, 33); ap_open[i] = a.open;
    Row &r = pool_row(wp, i++);
    set_fmt(r.title, "%s%s", a.open ? "" : LV_SYMBOL_EYE_CLOSE " ", a.ssid);
    set_fmt(r.val, "%d dBm", a.rssi);
    set_col(r.val, a.rssi > -60 ? C_GREEN : a.rssi > -72 ? C_AMBER : C_RED);
    lv_obj_align(r.val, LV_ALIGN_RIGHT_MID, -10, 0);
  }
  pool_trim(wp, i, S.scanning ? "Scanning..." : "No networks found.");
}

}  // namespace ui
