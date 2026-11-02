// CLIMATE: weather card (now + 3 days) and the Bluetooth thermometers (tap = choose inside / outside / fridge...).
#include "ui_kit.hpp"

namespace ui {
lv_color_t temp_color(float t);

static lv_obj_t *w_card, *w_icon, *w_temp, *w_text, *w_city, *w_off;
struct Day { lv_obj_t *name, *icon, *temp, *rain; };
static Day dd[3];
static Pool sens;
static uint32_t sver = UINT32_MAX, wver = UINT32_MAX, last_draw = 0;
static char addrs[12][18];
static lv_color_t role_color(uint8_t r) {
  switch (r) { case R_INSIDE: return C_TEAL; case R_OUTSIDE: return C_BLUE; case R_FRIDGE: return C_SKY; case R_FREEZER: return C_PURPLE; case R_OTHER: return C_DIM; default: return C_FAINT; }
}

static void sens_tap(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  uint8_t role = R_NONE;
  { Lock l; for (auto &s : S.sensors) if (!strcmp(s.addr, addrs[i])) role = s.role; }
  role = (role + 1) % R_COUNT;
  sens_set_role(addrs[i], role);
  char m[40]; snprintf(m, sizeof m, "Sensor is now: %s", role_name(role));
  toast(m);
  sver = UINT32_MAX;
}

void open_place();
// ---------------------------------------------------------------- forecast screen (48 h | 7 days)
// Lists are drawn lists (one object each) from a private copy of the forecast: no lock while painting.
static Seg fseg;
static lv_obj_t *f_body, *f_list, *f_tchart, *f_rchart, *f_tlab;
static lv_chart_series_t *f_tser, *f_rser;
static Wx fw;
static lv_color_t cond_color(int c) {
  return c >= 95 ? C_PURPLE : (c >= 51 && c < 70) || (c >= 80 && c < 95) ? C_BLUE : c >= 71 && c < 80 ? C_WHITE : c >= 3 ? C_DIM : C_AMBER;
}
static void hour_row(lv_layer_t *L, const lv_area_t &r, int k) {
  const WxH &h = fw.h[k * 2];                                  // every 2 hours
  char b[40];
  dl_dot(L, r, 8, 7, 5, 9, cond_color(h.code));
  snprintf(b, sizeof b, "%02d:00", (fw.h0 + k * 2) % 24);
  dl_text(L, r, 20, 3, F14, (fw.h0 + k * 2) % 24 == 0 ? C_AMBER : C_WHITE, b);
  snprintf(b, sizeof b, "%.1f\xC2\xB0", h.t10 / 10.0f);
  dl_text(L, r, 70, 3, F14, temp_color(h.t10 / 10.0f), b);
  dl_text(L, r, 124, 5, F12, C_DIM, wx_text(h.code));
  if (h.rain >= 10) snprintf(b, sizeof b, LV_SYMBOL_TINT "%d%%  %d/%d", h.rain, h.wind, h.gust); else snprintf(b, sizeof b, "%d/%d km/h", h.wind, h.gust);
  dl_text(L, r, 10, 5, F12, h.gust >= 50 ? C_RED : h.gust >= 35 ? C_AMBER : C_DIM, b, true);
}
static void day_row(lv_layer_t *L, const lv_area_t &r, int i) {
  char b[64];
  dl_icon(L, r.x1 + 8, r.y1 + 6, fw.d_code[i], 26);
  if (fw.d_rain[i] >= 20) snprintf(b, sizeof b, "%s   %s %d%%", fw.d_name[i], wx_text(fw.d_code[i]), fw.d_rain[i]);
  else snprintf(b, sizeof b, "%s   %s", fw.d_name[i], wx_text(fw.d_code[i]));
  dl_text(L, r, 42, 3, F14, C_WHITE, b);
  snprintf(b, sizeof b, "gust %d  UV %.0f  %.0f mm  " LV_SYMBOL_UP "%s " LV_SYMBOL_DOWN "%s", fw.d_gust[i], fw.d_uv[i], fw.d_sum[i], fw.d_rise[i], fw.d_set[i]);
  dl_text(L, r, 42, 21, F12, fw.d_gust[i] >= 50 ? C_ORANGE : C_DIM, b);
  snprintf(b, sizeof b, "%d\xC2\xB0", fw.d_max[i]);
  dl_text(L, r, 10, 1, F20, temp_color(fw.d_max[i]), b, true);
  snprintf(b, sizeof b, "%d\xC2\xB0", fw.d_min[i]);
  dl_text(L, r, 10, 22, F12, C_DIM, b, true);
}
static lv_obj_t *mk_chart(lv_obj_t *p, int y, int h, lv_chart_type_t t, lv_color_t c, lv_chart_series_t **s) {
  lv_obj_t *ch = lv_chart_create(p);
  lv_obj_set_size(ch, 312, h);
  lv_obj_set_pos(ch, 4, y);
  lv_chart_set_type(ch, t);
  lv_chart_set_point_count(ch, WX_HOURS);
  lv_chart_set_div_line_count(ch, 0, 0);
  lv_obj_set_style_bg_color(ch, C_CARD, 0);
  lv_obj_set_style_border_width(ch, 0, 0);
  lv_obj_set_style_radius(ch, 6, 0);
  lv_obj_set_style_pad_all(ch, 3, 0);
  lv_obj_set_style_pad_column(ch, 1, 0);
  lv_obj_set_style_size(ch, 0, 0, LV_PART_INDICATOR);
  lv_obj_set_style_line_width(ch, 3, LV_PART_ITEMS);
  *s = lv_chart_add_series(ch, c, LV_CHART_AXIS_PRIMARY_Y);
  return ch;
}
static void fc_show(int view) {
  seg_set(fseg, view);
  lv_obj_clean(f_body);                                        // only one view in RAM
  if (view == 0) {
    f_tchart = mk_chart(f_body, 0, 50, LV_CHART_TYPE_LINE, C_ORANGE, &f_tser);
    f_rchart = mk_chart(f_body, 52, 22, LV_CHART_TYPE_BAR, C_BLUE, &f_rser);
    lv_chart_set_range(f_rchart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_obj_set_style_pad_top(f_tchart, 17, 0);                 // room for the caption: the line stays below it
    f_tlab = label(f_tchart, F12, C_DIM, "");
    lv_obj_add_flag(f_tlab, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(f_tlab, LV_ALIGN_TOP_LEFT, 2, -16);
    int mn = 999, mx = -999;
    for (int i = 0; i < fw.nh; i++) { mn = min(mn, fw.h[i].t10 / 10 - 1); mx = max(mx, fw.h[i].t10 / 10 + 1); }
    if (mn > mx) { mn = 0; mx = 20; }
    lv_chart_set_range(f_tchart, LV_CHART_AXIS_PRIMARY_Y, mn * 10, mx * 10);
    for (int i = 0; i < WX_HOURS; i++) {
      lv_chart_set_value_by_id(f_tchart, f_tser, i, i < fw.nh ? fw.h[i].t10 : LV_CHART_POINT_NONE);
      lv_chart_set_value_by_id(f_rchart, f_rser, i, i < fw.nh ? fw.h[i].rain : LV_CHART_POINT_NONE);
    }
    lv_chart_refresh(f_tchart); lv_chart_refresh(f_rchart);
    lv_label_set_text_fmt(f_tlab, "%d..%d\xC2\xB0  next 48 h, bars = rain chance", mn + 1, mx - 1);
    f_list = drawlist(f_body, 4, 78, 312, 98, 26, hour_row);
    drawlist_count(f_list, fw.nh / 2);
  } else {
    f_list = drawlist(f_body, 4, 0, 312, 176, 42, day_row);
    drawlist_count(f_list, fw.nd);
  }
}
static void fc_seg(lv_event_t *e) { fc_show((int)(intptr_t)lv_event_get_user_data(e)); }
void open_forecast(lv_event_t *) {
  bool ok; { Lock l; ok = S.wx.ok; if (ok) fw = S.wx; }
  if (!ok) { toast("No forecast yet: needs internet once"); return; }
  if (ESP.getFreeHeap() < 24000) { toast("Busy, try again in a moment"); return; }
  lv_obj_t *m = modal(LV_SYMBOL_TINT "  Forecast");
  fseg = Seg{};
  static const char *N[] = {"48 hours", "7 days"};
  seg_make(fseg, m, 10, 34, 200, 26, N, 2, fc_seg);
  lv_obj_t *pb = button(m, LV_SYMBOL_GPS " Place", 92, 26, C_CARD2, [](lv_event_t *) { close_modal(); open_place(); });
  lv_obj_set_style_text_font(lv_obj_get_child(pb, 0), F12, 0);
  lv_obj_set_pos(pb, 220, 34);
  f_body = box(m, 320, 176, C_BG);
  lv_obj_set_pos(f_body, 0, 64);
  fc_show(0);
}
void open_forecast_test(int view) { open_forecast(nullptr); if (g_modal && view) fc_show(1); }

// ---------------------------------------------------------------- weather place (auto / pinned town)
static lv_obj_t *pl_modal, *pl_list, *pl_info;
static Places pc;                   // painted copy
static bool pl_show_hits() { return pc.n_hits > 0; }
static void pl_row(lv_layer_t *L, const lv_area_t &r, int i) {
  const Place &p = pl_show_hits() ? pc.hits[i] : pc.recent[i];
  bool cur = pc.pinned && !strcmp(p.name, pc.cur.name) && fabsf(p.lat - pc.cur.lat) < 0.01f;
  dl_dot(L, r, 8, 8, 5, 14, cur ? C_GREEN : pl_show_hits() ? C_BLUE : C_FAINT);
  dl_text(L, r, 20, 5, F14, C_WHITE, p.name);
  dl_text(L, r, 10, 7, F12, C_DIM, p.area, true);
}
static void pl_tap(int i) {
  Place p = pl_show_hits() ? pc.hits[i] : pc.recent[i];
  place_pin(p);
  char m[64]; snprintf(m, sizeof m, "Forecast for %s", p.name);
  close_modal();
  toast(m);
}
static uint32_t pl_ver = UINT32_MAX;
void place_picker_update() {
  if (!g_modal || g_modal != pl_modal) return;
  { Lock l; if (pl_ver == P.ver) return; pl_ver = P.ver; pc = P; }
  if (pc.searching) set_txt(pl_info, "Searching...");
  else if (pc.err[0]) set_txt(pl_info, pc.err);
  else if (pc.n_hits == 0) set_txt(pl_info, "Nothing found, try another spelling");
  else if (pc.n_hits > 0) set_txt(pl_info, "Tap the right town:");
  else if (pc.n_recent) set_fmt(pl_info, "Now: %s.  Places used before:", pc.pinned ? pc.cur.name : "auto");
  else set_fmt(pl_info, "Now: %s.\nOn campsite WiFi the auto place can be far off: search the town.", pc.pinned ? pc.cur.name : "auto");
  drawlist_count(pl_list, pl_show_hits() ? pc.n_hits : pc.n_hits == 0 ? 0 : pc.n_recent);
}
void open_place() {
  lv_obj_t *m = modal(LV_SYMBOL_GPS "  Weather place");
  modal_owner(&pl_modal, m);
  lv_obj_t *a = button(m, LV_SYMBOL_REFRESH " Auto", 120, 30, C_CARD2, [](lv_event_t *) {
    place_auto(); close_modal(); toast("Weather place: auto (from the internet)");
  });
  lv_obj_set_pos(a, 8, 36);
  lv_obj_t *b = button(m, LV_SYMBOL_KEYBOARD " Search town", 176, 30, C_TEAL, [](lv_event_t *) {
    open_text("Town or village", "e.g. Annecy", 30, nullptr, [](const char *q) { open_place(); if (q[0]) place_search(q); });
  });
  lv_obj_set_pos(b, 136, 36);
  pl_info = label(m, F12, C_DIM, "");
  lv_obj_set_width(pl_info, 304);
  lv_label_set_long_mode(pl_info, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(pl_info, 8, 72);
  pl_list = drawlist(m, 4, 104, 312, 134, 30, pl_row);
  drawlist_on_tap(pl_list, pl_tap);
  { Lock l; P.n_hits = -1; P.err[0] = 0; P.ver++; }
  pl_ver = UINT32_MAX;
  place_picker_update();
}

void climate_build(lv_obj_t *t) {
  sens = Pool{}; sver = wver = UINT32_MAX;
  w_card = card(t, 4, 4, 312, 72);
  lv_obj_add_flag(w_card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(w_card, C_CARD2, LV_STATE_PRESSED);
  on_tap(w_card, open_forecast);
  w_icon = lv_obj_create(w_card);
  lv_obj_remove_style_all(w_icon);
  lv_obj_set_pos(w_icon, 8, 6);
  lv_obj_remove_flag(w_icon, LV_OBJ_FLAG_CLICKABLE);
  w_temp = label(w_card, F28, C_WHITE, "");
  lv_obj_set_pos(w_temp, 58, 2);
  w_text = label(w_card, F12, C_DIM, "");
  lv_obj_set_width(w_text, 136);
  lv_label_set_long_mode(w_text, LV_LABEL_LONG_DOT); lv_obj_set_height(w_text, lv_font_get_line_height(lv_obj_get_style_text_font(w_text, 0)));
  lv_obj_set_pos(w_text, 10, 54);
  w_city = label(w_card, F12, C_FAINT, "");
  lv_obj_set_width(w_city, 86);
  lv_label_set_long_mode(w_city, LV_LABEL_LONG_DOT); lv_obj_set_height(w_city, lv_font_get_line_height(lv_obj_get_style_text_font(w_city, 0)));
  lv_obj_set_pos(w_city, 58, 34);
  for (int i = 0; i < 3; i++) {
    int x = 148 + i * 54;
    if (i) { lv_obj_t *sep = box(w_card, 1, 52, C_LINE); lv_obj_set_pos(sep, x - 4, 10); }
    dd[i].name = label(w_card, F12, C_DIM, "");
    lv_obj_set_pos(dd[i].name, x + 2, 2);
    dd[i].icon = lv_obj_create(w_card);
    lv_obj_remove_style_all(dd[i].icon);
    lv_obj_remove_flag(dd[i].icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(dd[i].icon, x + 4, 17);
    dd[i].temp = label(w_card, F12, C_WHITE, "");
    lv_obj_set_pos(dd[i].temp, x, 40);
    dd[i].rain = label(w_card, F12, C_BLUE, "");
    lv_obj_set_pos(dd[i].rain, x, 54);
  }
  w_off = label(w_card, F14, C_DIM, LV_SYMBOL_WIFI "  Weather needs internet once.\nThe last forecast stays on the SD card.");
  lv_obj_set_pos(w_off, 12, 14);

  sens.list = page_scroll(t, 80, PAGE_H - 80);
  sens.h = 44; sens.dot = true; sens.big_val = true; sens.cb = sens_tap;
}

static void ago_txt(char *o, size_t n, uint32_t fetched) {
  time_t now = time(nullptr);
  if (!fetched || now < 1700000000 || now < (time_t)fetched) { o[0] = 0; return; }
  uint32_t m = (now - fetched) / 60;
  if (m < 60) snprintf(o, n, "%u min ago", (unsigned)m);
  else if (m < 48 * 60) snprintf(o, n, "%u h ago", (unsigned)(m / 60));
  else snprintf(o, n, "%u days ago", (unsigned)(m / 1440));
}

void climate_update(bool force) {
  static uint32_t last_ago = 0;
  if (force) sver = wver = UINT32_MAX;
  Lock l;
  if (wver != S.wx_ver || millis() - last_ago > 60000) {
    wver = S.wx_ver; last_ago = millis();
    const Wx &w = S.wx;
    show(w_off, !w.ok);
    show(w_icon, w.ok); show(w_temp, w.ok); show(w_text, w.ok); show(w_city, w.ok);
    for (auto &d : dd) { show(d.name, w.ok); show(d.icon, w.ok); show(d.temp, w.ok); show(d.rain, w.ok); }
    if (w.ok) {
      wx_icon(w_icon, w.code, 42);
      set_fmt(w_temp, "%.1f\xC2\xB0", w.t);
      set_col(w_temp, temp_color(w.t));
      if (w.warn_lvl) { set_fmt(w_text, LV_SYMBOL_WARNING " %s", w.warn); set_col(w_text, w.warn_lvl == 2 ? C_ORANGE : C_AMBER); }
      else { set_fmt(w_text, "%s, wind %.0f km/h", wx_text(w.code), w.wind); set_col(w_text, C_DIM); }
      char ag[20]; ago_txt(ag, sizeof ag, w.fetched);
      if (ag[0]) set_fmt(w_city, "%s, %s", w.city, ag); else set_txt(w_city, w.city);
      for (int i = 0; i < 3; i++) {
        bool has = i + 1 < w.nd;
        set_txt(dd[i].name, has ? w.d_name[i + 1] : "");
        if (has) wx_icon(dd[i].icon, w.d_code[i + 1], 22);
        if (has) set_fmt(dd[i].temp, "%d\xC2\xB0/%d\xC2\xB0", w.d_max[i + 1], w.d_min[i + 1]); else set_txt(dd[i].temp, "");
        if (has && w.d_rain[i + 1] >= 20) set_fmt(dd[i].rain, LV_SYMBOL_TINT "%d%%", w.d_rain[i + 1]); else set_txt(dd[i].rain, "");
      }
    }
  }
  if (sver == S.sens_ver && millis() - last_draw < 5000) return;
  if (millis() - last_draw < 700 && sver != UINT32_MAX) return;
  sver = S.sens_ver; last_draw = millis();
  std::vector<Sensor> v = S.sensors;
  std::sort(v.begin(), v.end(), [](const Sensor &a, const Sensor &b) {
    if ((a.role == R_NONE) != (b.role == R_NONE)) return a.role != R_NONE;
    return a.role != b.role ? a.role < b.role : a.rssi > b.rssi;
  });
  int i = 0;
  for (auto &s : v) {
    if (i >= 12) break;
    strlcpy(addrs[i], s.addr, sizeof addrs[i]);
    Row &r = pool_row(sens, i++);
    set_bg(r.dot, role_color(s.role));
    if (s.role == R_NONE) set_fmt(r.title, "%s sensor  (tap to name)", s.kind); else set_txt(r.title, role_name(s.role));
    set_col(r.title, s.role == R_NONE ? C_DIM : C_WHITE);
    char bat[16] = ""; if (s.batt >= 0) snprintf(bat, sizeof bat, "   batt %d%%", s.batt);
    uint32_t age = (millis() - s.seen) / 1000;
    if (age > 300) set_fmt(r.sub, "not heard for %u min%s", (unsigned)(age / 60), bat);
    else if (!isnan(s.tmin)) set_fmt(r.sub, LV_SYMBOL_DOWN " %.1f\xC2\xB0  " LV_SYMBOL_UP " %.1f\xC2\xB0 today%s", s.tmin, s.tmax, bat);
    else set_txt(r.sub, bat);
    if (!isnan(s.hpa) && age <= 300) {
      float tr = hpa_trend(s);
      const char *arrow = isnan(tr) ? "" : tr < -1.5f ? "  " LV_SYMBOL_DOWN : tr > 1.5f ? "  " LV_SYMBOL_UP : "  =";
      set_fmt(r.sub, "%.0f hPa%s%s%s", s.hpa, arrow, !isnan(tr) && tr <= -4 ? " storm risk" : "", bat);
      set_col(r.sub, !isnan(tr) && tr <= -4 ? C_ORANGE : C_DIM);
    } else set_col(r.sub, C_DIM);
    if (!isnan(s.t)) set_fmt(r.val, "%.1f\xC2\xB0", s.t); else set_txt(r.val, "--");
    set_col(r.val, s.role == R_FRIDGE && s.t > 7 ? C_RED : s.role == R_FREEZER && s.t > -12 ? C_RED : temp_color(s.t));
    lv_obj_align(r.val, LV_ALIGN_RIGHT_MID, -10, -6);
    if (!isnan(s.h)) set_fmt(r.val2, LV_SYMBOL_TINT " %d%%", (int)(s.h + 0.5f)); else set_txt(r.val2, "");
  }
  pool_trim(sens, i, "No Bluetooth thermometers heard yet.\nWorks with Xiaomi LYWSD03MMC (pvvx firmware), "
                     "Shelly BLU H&T and other BTHome sensors. Tap a sensor to name it.");
}

}  // namespace ui
