// Shell: status bar (clock, wifi, bluetooth, SD, demo), five tabs, low battery banner, update timer.
#include "ui_kit.hpp"

namespace ui {
void wifi_picker_update();
void bank_settings_update();

static lv_obj_t *tv, *clock_l, *date_l, *i_wifi, *i_bt, *i_sd, *i_demo, *alarm;
enum { T_HOME, T_POWER, T_CLIMATE, T_BANK, T_LEVEL, T_TOOLS, T_N };
void saver_build(); bool saver_on();
static int cur = 0;
static lv_obj_t *tabs[T_N];
static void build_page(int i) {
  switch (i) {
    case T_HOME: home_build(tabs[i]); break;
    case T_POWER: power_build(tabs[i]); break;
    case T_CLIMATE: climate_build(tabs[i]); break;
    case T_BANK: bank_build(tabs[i]); break;
    case T_LEVEL: level_build(tabs[i]); break;
    case T_TOOLS: tools_build(tabs[i]); break;
  }
}
static uint32_t low_since = 0, snooze_at = 0;
static bool snoozed = false, alarm_on = false;

static const char *WD[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *MO[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static void status_build(lv_obj_t *scr) {
  lv_obj_t *b = box(scr, 320, TOP, C_BAR);
  clock_l = label(b, F16, C_WHITE, "--:--");
  lv_obj_align(clock_l, LV_ALIGN_LEFT_MID, 8, 0);
  date_l = label(b, F12, C_DIM, "");
  lv_obj_align_to(date_l, clock_l, LV_ALIGN_OUT_RIGHT_MID, 8, 1);
  i_sd = label(b, F14, C_FAINT, LV_SYMBOL_SD_CARD);
  lv_obj_align(i_sd, LV_ALIGN_RIGHT_MID, -8, 0);
  i_bt = label(b, F14, C_FAINT, LV_SYMBOL_BLUETOOTH);
  lv_obj_align(i_bt, LV_ALIGN_RIGHT_MID, -30, 0);
  i_wifi = label(b, F14, C_FAINT, LV_SYMBOL_WIFI);
  lv_obj_align(i_wifi, LV_ALIGN_RIGHT_MID, -50, 0);
  i_demo = label(b, F12, C_BG, "DEMO");
  lv_obj_set_style_bg_color(i_demo, C_AMBER, 0);
  lv_obj_set_style_bg_opa(i_demo, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(i_demo, 4, 0);
  lv_obj_set_style_pad_hor(i_demo, 5, 0);
  lv_obj_align(i_demo, LV_ALIGN_RIGHT_MID, -80, 0);
}
static void status_update() {
  struct tm t;
  time_t now = time(nullptr);
  bool ok = now > 1700000000;
  bool est; { Lock l; est = S.clock_est; }
  if (ok) {
    localtime_r(&now, &t);
    set_fmt(clock_l, "%02d:%02d", t.tm_hour, t.tm_min);
    set_fmt(date_l, "%s %d %s%s", WD[t.tm_wday], t.tm_mday, MO[t.tm_mon], est ? "  (clock from SD)" : "");
    lv_obj_align_to(date_l, clock_l, LV_ALIGN_OUT_RIGHT_MID, 8, 1);
  }
  Lock l;
  set_col(i_wifi, !S.wifi ? C_FAINT : S.portal ? C_AMBER : S.online ? C_TEAL : C_DIM);
  static bool portal_told = false;
  if (S.portal && !portal_told) { portal_told = true; toast("This WiFi needs a login page: use the phone hotspot for weather"); }
  if (!S.portal) portal_told = false;
  bool bt = S.vic.decoded && millis() - S.vic.at < 30000;
  for (auto &s : S.sensors) if (millis() - s.seen < 120000) bt = true;
  set_col(i_bt, bt ? C_BLUE : C_FAINT);
  set_col(i_sd, S.sd_ok ? C_GREEN : C_RED);
  show(i_demo, S.demo);
}

// Alarms (most urgent first): battery low (20 s), fridge/freezer too warm (20 min), fridge/freezer sensor silent (15 min).
static char last_alarm[64] = "";
static void alarm_check() {
  char msg[64] = "";
  {
    Lock l;
    const Victron &v = S.vic;
    bool fresh = v.decoded && millis() - v.at < 30000;
    int cv = v.vbat_cv > 1900 ? v.vbat_cv / 2 : v.vbat_cv;
    bool low = fresh && v.alarm_cv && v.vbat_cv != 0x7FFF && cv < v.alarm_cv;
    if (!low) low_since = 0;
    else { if (!low_since) low_since = millis(); if (millis() - low_since >= 20000) snprintf(msg, sizeof msg, "Battery low: %d.%02d V", v.vbat_cv / 100, v.vbat_cv % 100); }
    uint32_t warm_ms = demo_on ? 20000 : 20 * 60000;
    for (auto &x : S.sensors) {
      if (x.role != R_FRIDGE && x.role != R_FREEZER) continue;
      bool frz = x.role == R_FREEZER;
      bool warm = !isnan(x.t) && x.t > (frz ? -12 : 7);
      if (!warm) x.warm_since = 0; else if (!x.warm_since) x.warm_since = millis();
      if (msg[0]) continue;
      if (warm && millis() - x.warm_since > warm_ms) snprintf(msg, sizeof msg, "%s too warm: %.1f\xC2\xB0", frz ? "Freezer" : "Fridge", x.t);
      else if (millis() - x.seen > 15 * 60000) snprintf(msg, sizeof msg, "%s sensor silent (battery?)", frz ? "Freezer" : "Fridge");
    }
  }
  if (!msg[0]) { alarm_on = false; last_alarm[0] = 0; show(alarm, false); return; }
  if (strncmp(msg, last_alarm, 12)) { strlcpy(last_alarm, msg, sizeof last_alarm); log_event("alarm: %s", msg); }
  if (snoozed && millis() - snooze_at < 30 * 60000) return;
  snoozed = false;
  if (!alarm_on) { screen_wake(); beep(2); }
  alarm_on = true;
  set_fmt(lv_obj_get_child(alarm, 0), LV_SYMBOL_WARNING "  %s   tap = snooze", msg);
  show(alarm, true);
}
bool alarm_active() { return alarm_on; }
void goto_tab(int i) { lv_tabview_set_active(tv, i, LV_ANIM_OFF); lv_obj_send_event(tv, LV_EVENT_VALUE_CHANGED, nullptr); }

static void tab_changed(lv_event_t *) {
  int nxt = lv_tabview_get_tab_active(tv);
  if (nxt == cur) return;
  if (cur == T_TOOLS) tools_leave();
  lv_obj_clean(tabs[cur]);                 // only the visible page lives in RAM
  cur = nxt;
  build_page(cur);
  if (cur == T_HOME) home_update();
  switch (cur) {
    case T_POWER: power_update(true); break;
    case T_CLIMATE: climate_update(true); break;
    case T_BANK: bank_page_update(true); break;
    case T_TOOLS: tools_update(true); break;
  }
}

// Day / night look: Auto follows sunrise and sunset of today's forecast (07:00 / 19:00 without one).
static bool want_day() {
  if (cfg.look_mode == 1) return true;
  if (cfg.look_mode == 2) return false;
  struct tm t; time_t now = time(nullptr);
  if (now < 1700000000) return false;
  localtime_r(&now, &t);
  int m = t.tm_hour * 60 + t.tm_min, rise = 7 * 60, set = 19 * 60;
  { Lock l; if (S.wx.ok && S.wx.nd) { rise = atoi(S.wx.d_rise[0]) * 60 + atoi(S.wx.d_rise[0] + 3); set = atoi(S.wx.d_set[0]) * 60 + atoi(S.wx.d_set[0] + 3); } }
  return m >= rise && m < set;
}
static void shell_style() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_set_style_bg_color(lv_obj_get_parent(clock_l), C_BAR, 0);
  lv_obj_set_style_text_color(clock_l, C_WHITE, 0);
  lv_obj_set_style_text_color(date_l, C_DIM, 0);
  lv_obj_set_style_text_color(i_demo, C_BG, 0);
  lv_obj_set_style_bg_color(i_demo, C_AMBER, 0);
  lv_obj_set_style_bg_color(tv, C_BG, 0);
  lv_obj_t *bar = lv_tabview_get_tab_bar(tv);
  lv_obj_set_style_bg_color(bar, C_BAR, 0);
  for (uint32_t i = 0; i < lv_obj_get_child_count(bar); i++) {
    lv_obj_t *b = lv_obj_get_child(bar, i);
    lv_obj_set_style_text_color(b, C_FAINT, 0);
    lv_obj_set_style_text_color(b, C_TEAL, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(b, C_TEAL, LV_PART_MAIN | LV_STATE_CHECKED);
  }
}
static bool look_pending = false;
void look_request() { look_pending = true; }
void look_check() {
  look_pending = false;
  if (!set_look(want_day())) return;
  if (g_modal) close_modal();
  shell_style();
  if (cur == T_TOOLS) tools_leave();
  lv_obj_clean(tabs[cur]);
  build_page(cur);
  switch (cur) {
    case T_HOME: home_update(); break;
    case T_POWER: power_update(true); break;
    case T_CLIMATE: climate_update(true); break;
    case T_BANK: bank_page_update(true); break;
    case T_TOOLS: tools_update(true); break;
  }
  status_update();
  lv_obj_invalidate(lv_screen_active());
}

static void tick(lv_timer_t *) {
  static uint32_t last_s = 0, last_home = 0;
  uint32_t now = millis();
  web_drain();
  if (look_pending) look_check();
  if (now - last_s >= 1000) {
    last_s = now;
    static uint8_t n = 0;
    if (++n >= 30) { n = 0; if (!saver_on()) look_check(); }       // twice a minute is plenty for sunset
    if (!saver_on()) status_update();
    alarm_check();
  }
  if (saver_on()) return;
  if (g_modal) { sd_modal_update(); wifi_picker_update(); bank_settings_update(); place_picker_update(); return; }
  switch (cur) {
    case T_HOME: if (now - last_home >= 500) { last_home = now; home_update(); } break;
    case T_POWER: power_update(false); break;
    case T_CLIMATE: climate_update(false); break;
    case T_BANK: bank_page_update(false); break;
    case T_LEVEL: level_poll(); level_update(); break;
    case T_TOOLS: tools_update(false); break;
  }
}

void begin() {
  set_look(want_day());
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  status_build(scr);
  tv = lv_tabview_create(scr);
  lv_obj_set_size(tv, 320, 240 - TOP);
  lv_obj_set_pos(tv, 0, TOP);
  lv_tabview_set_tab_bar_position(tv, LV_DIR_BOTTOM);
  lv_tabview_set_tab_bar_size(tv, TABS);
  lv_obj_set_style_bg_color(tv, C_BG, 0);
  lv_obj_remove_flag(lv_tabview_get_content(tv), LV_OBJ_FLAG_SCROLLABLE);   // tabs by tapping only: no accidental swipes
  lv_obj_t *bar = lv_tabview_get_tab_bar(tv);
  lv_obj_set_style_bg_color(bar, C_BAR, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  static const char *N[] = {LV_SYMBOL_HOME "\nHome", LV_SYMBOL_CHARGE "\nPower", LV_SYMBOL_TINT "\nClimate", LV_SYMBOL_BATTERY_3 "\nBank", LV_SYMBOL_GPS "\nLevel", LV_SYMBOL_LIST "\nTools"};
  for (int i = 0; i < T_N; i++) {
    tabs[i] = lv_tabview_add_tab(tv, N[i]);
    lv_obj_set_style_pad_all(tabs[i], 0, 0);
    lv_obj_remove_flag(tabs[i], LV_OBJ_FLAG_SCROLLABLE);
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(bar); i++) {
    lv_obj_t *b = lv_obj_get_child(bar, i);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_text_font(b, F12, 0);
    lv_obj_set_style_text_line_space(b, -1, 0);
    lv_obj_set_style_text_color(b, C_FAINT, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(b, C_TEAL, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_TOP, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(b, 3, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(b, C_TEAL, LV_PART_MAIN | LV_STATE_CHECKED);
  }
  shell_style();
  saver_build();
  build_page(T_HOME);
  lv_obj_add_event_cb(tv, tab_changed, LV_EVENT_VALUE_CHANGED, nullptr);

  alarm = box(lv_layer_top(), 320, 26, C_RED);
  lv_obj_add_flag(alarm, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(alarm, [](lv_event_t *) { snoozed = true; snooze_at = millis(); show(alarm, false); toast("Alarm snoozed for 30 min"); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(label(alarm, F14, C_WHITE, ""));
  show(alarm, false);

  lv_timer_create(tick, 100, nullptr);
  status_update();
  home_update();
}

}  // namespace ui
