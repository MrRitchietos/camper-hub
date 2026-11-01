// POWER: Live (gauge, tiles, last hour) | Today (day stats) | Days (history from SD).
#include "ui_kit.hpp"

namespace ui {
void bank_settings();
lv_color_t pct_color(int p);
lv_color_t state_color(uint8_t s);

static Seg seg;
static lv_obj_t *v_live, *v_today, *v_days, *v_setup, *v_bank;
static lv_obj_t *k_arc, *k_pct, *k_status, *k_in, *k_out, *k_time, *k_btn[4], *k_ports;
static uint32_t kver = UINT32_MAX;
static lv_obj_t *arc, *pv, *pill, *pill_t, *chart, *chart_t, *foot;
static lv_chart_series_t *ser;
struct T { lv_obj_t *c, *v; };
static T tiles[6];
// Today: one drawn list (a few hundred bytes) instead of 4 objects per row
struct Stat { lv_color_t dot, vc; char t[28], v[36]; };
static Stat stats[14];
static int n_stats = 0;
static lv_obj_t *today_dl;
static lv_obj_t *days_dl, *days_empty;
static std::vector<DayRec> dcopy;
static int dmax = 50;
static void day_row(lv_layer_t *L, const lv_area_t &r, int k);
static lv_obj_t *d_chart, *d_title;
static lv_chart_series_t *d_ser;
static lv_obj_t *su_title, *su_text;
static uint32_t ver = UINT32_MAX, hver = UINT32_MAX, dver = UINT32_MAX;

static void view(int i) {
  seg_set(seg, i);
  show(v_live, i == 0); show(v_today, i == 1); show(v_days, i == 2);
  ver = hver = dver = UINT32_MAX;
}
void power_view_test(int i) { if (v_live && lv_obj_is_valid(v_live) && i < 3) view(i); }
static void seg_cb(lv_event_t *e) { view((int)(intptr_t)lv_event_get_user_data(e)); }

static void key_done(const char *k) {
  if (!vic_set_key(k)) { toast("Key = 32 characters 0-9 / a-f"); return; }
  if (demo_on) set_demo(false);
  toast("Key saved, listening...");
}
void power_open_key() {
  open_text(LV_SYMBOL_CHARGE "  Victron Instant Readout key", "32 characters from VictronConnect", 47,
            "0123456789abcdefABCDEF -", key_done);
}

static T tile(lv_obj_t *p, int x, int y, const char *cap, lv_color_t accent) {
  T t;
  t.c = card(p, x, y, 97, 34);
  lv_obj_set_style_radius(t.c, 8, 0);
  lv_obj_t *a = box(t.c, 3, 20, accent);
  lv_obj_set_style_radius(a, 2, 0);
  lv_obj_align(a, LV_ALIGN_LEFT_MID, 5, 0);
  lv_obj_t *l = label(t.c, F12, C_DIM, cap);
  lv_obj_set_pos(l, 13, 1);
  t.v = label(t.c, F14, C_WHITE, "-");
  lv_obj_set_pos(t.v, 13, 16);
  return t;
}

void power_build(lv_obj_t *t) {
  seg = Seg{}; n_stats = 0; ver = hver = dver = UINT32_MAX;
  static const char *N[] = {"Solar", "Today", "Days"};
  seg_make(seg, t, 30, 2, 260, 26, N, 3, seg_cb);

  // ---- live
  v_live = box(t, 320, PAGE_H - 30, C_BG);
  lv_obj_set_pos(v_live, 0, 30);
  arc = lv_arc_create(v_live);
  lv_obj_set_size(arc, 104, 104);
  lv_obj_set_pos(arc, 6, 0);
  lv_arc_set_rotation(arc, 135);
  lv_arc_set_bg_angles(arc, 0, 270);
  lv_arc_set_range(arc, 0, 220);
  lv_arc_set_value(arc, 0);
  lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, C_CARD2, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, C_AMBER, LV_PART_INDICATOR);
  pv = label(arc, F28, C_WHITE, "-");
  lv_obj_align(pv, LV_ALIGN_CENTER, 0, -6);
  lv_obj_t *u = label(arc, F12, C_DIM, "WATT");
  lv_obj_align(u, LV_ALIGN_CENTER, 0, 16);
  pill = box(v_live, 84, 18, C_CARD2);
  lv_obj_set_style_radius(pill, 9, 0);
  lv_obj_set_pos(pill, 16, 88);
  pill_t = label(pill, F12, C_WHITE, "");
  lv_obj_center(pill_t);
  tiles[0] = tile(v_live, 116, 0, "BATTERY", C_GREEN);
  tiles[1] = tile(v_live, 217, 0, "CHARGE", C_TEAL);
  tiles[2] = tile(v_live, 116, 37, "TODAY", C_AMBER);
  tiles[3] = tile(v_live, 217, 37, "LOAD", C_BLUE);
  tiles[4] = tile(v_live, 116, 74, "PEAK", C_ORANGE);
  tiles[5] = tile(v_live, 217, 74, "LEVEL", C_PURPLE);
  chart = lv_chart_create(v_live);
  lv_obj_set_size(chart, 312, 38);
  lv_obj_set_pos(chart, 4, 112);
  lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
  lv_chart_set_point_count(chart, VHIST);
  lv_chart_set_div_line_count(chart, 0, 0);
  lv_obj_set_style_bg_color(chart, C_CARD, 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  lv_obj_set_style_radius(chart, 8, 0);
  lv_obj_set_style_pad_all(chart, 4, 0);
  lv_obj_set_style_pad_top(chart, 14, 0);
  lv_obj_set_style_pad_column(chart, 1, 0);
  lv_obj_set_style_pad_column(chart, 0, LV_PART_ITEMS);
  lv_obj_set_style_radius(chart, 1, LV_PART_ITEMS);
  ser = lv_chart_add_series(chart, C_AMBER, LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_all_value(chart, ser, LV_CHART_POINT_NONE);
  chart_t = label(chart, F12, C_DIM, "SOLAR, LAST HOUR");
  lv_obj_add_flag(chart_t, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_align(chart_t, LV_ALIGN_TOP_LEFT, 2, -13);
  foot = label(chart, F12, C_FAINT, "");
  lv_obj_align(foot, LV_ALIGN_TOP_RIGHT, -2, -13);

  // ---- today
  v_today = box(t, 320, PAGE_H - 30, C_BG);
  lv_obj_set_pos(v_today, 0, 30);
  today_dl = drawlist(v_today, 4, 0, 312, PAGE_H - 30, 30, [](lv_layer_t *L, const lv_area_t &r, int i) {
    if (i >= n_stats) return;
    const Stat &s = stats[i];
    dl_dot(L, r, 7, 7, 6, 12, s.dot);
    dl_text(L, r, 20, 4, F14, C_WHITE, s.t);
    dl_text(L, r, 10, 4, F14, s.vc, s.v, true);
  });

  // ---- days
  v_days = box(t, 320, PAGE_H - 30, C_BG);
  lv_obj_set_pos(v_days, 0, 30);
  d_chart = lv_chart_create(v_days);
  lv_obj_set_size(d_chart, 312, 52);
  lv_obj_set_pos(d_chart, 4, 0);
  lv_chart_set_type(d_chart, LV_CHART_TYPE_BAR);
  lv_chart_set_point_count(d_chart, 14);
  lv_chart_set_div_line_count(d_chart, 0, 0);
  lv_obj_set_style_bg_color(d_chart, C_CARD, 0);
  lv_obj_set_style_border_width(d_chart, 0, 0);
  lv_obj_set_style_radius(d_chart, 8, 0);
  lv_obj_set_style_pad_all(d_chart, 5, 0);
  lv_obj_set_style_pad_top(d_chart, 16, 0);
  lv_obj_set_style_pad_column(d_chart, 5, 0);
  lv_obj_set_style_radius(d_chart, 3, LV_PART_ITEMS);
  d_ser = lv_chart_add_series(d_chart, C_AMBER, LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_all_value(d_chart, d_ser, LV_CHART_POINT_NONE);
  d_title = label(d_chart, F12, C_DIM, "");
  lv_obj_add_flag(d_title, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_align(d_title, LV_ALIGN_TOP_LEFT, 2, -14);
  days_dl = drawlist(v_days, 4, 56, 312, PAGE_H - 30 - 56, 38, day_row);
  days_empty = label(v_days, F14, C_DIM, "");
  lv_obj_set_width(days_empty, 300);
  lv_label_set_long_mode(days_empty, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(days_empty, 10, 64);

  // ---- setup / searching overlay (live view only)
  v_setup = box(v_live, 320, PAGE_H - 30, C_BG);
  lv_obj_t *ic = label(v_setup, F40, C_AMBER, LV_SYMBOL_CHARGE);
  lv_obj_set_pos(ic, 14, 0);
  su_title = label(v_setup, F16, C_WHITE, "");
  lv_obj_set_width(su_title, 250);
  lv_label_set_long_mode(su_title, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(su_title, 56, 0);
  su_text = label(v_setup, F12, C_DIM, "");
  lv_obj_set_width(su_text, 250);
  lv_label_set_long_mode(su_text, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(su_text, 56, 40);
  lv_obj_t *hint = label(v_setup, F12, C_FAINT, "VictronConnect: open the charger, gear icon, 3 dots, Product info, "
                                               "Instant readout via Bluetooth ON, Show next to the details.");
  lv_obj_set_width(hint, 300);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(hint, 10, 70);
  lv_obj_set_pos(button(v_setup, LV_SYMBOL_KEYBOARD "  Enter key", 150, 34, C_TEAL, [](lv_event_t *) { power_open_key(); }), 6, 114);
  lv_obj_set_pos(button(v_setup, LV_SYMBOL_PLAY "  Demo", 150, 34, C_CARD2, [](lv_event_t *) { set_demo(true); toast("Demo: a full day every 2 minutes"); }), 164, 114);
  view(0);
}

// ---------------------------------------------------------------- POWERBANK tab (Fossibot)
void bank_build(lv_obj_t *t) {
  kver = UINT32_MAX;
  v_bank = box(t, 320, PAGE_H, C_BG);
  k_arc = lv_arc_create(v_bank);
  lv_obj_set_size(k_arc, 112, 112);
  lv_obj_set_pos(k_arc, 6, 6);
  lv_arc_set_rotation(k_arc, 270);
  lv_arc_set_bg_angles(k_arc, 0, 360);
  lv_arc_set_range(k_arc, 0, 100);
  lv_arc_set_value(k_arc, 0);
  lv_obj_remove_style(k_arc, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(k_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(k_arc, 11, LV_PART_MAIN);
  lv_obj_set_style_arc_width(k_arc, 11, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(k_arc, C_CARD2, LV_PART_MAIN);
  lv_obj_set_style_arc_color(k_arc, C_PURPLE, LV_PART_INDICATOR);
  k_pct = label(k_arc, F28, C_WHITE, "--");
  lv_obj_align(k_pct, LV_ALIGN_CENTER, 0, -4);
  lv_obj_t *ku = label(k_arc, F12, C_DIM, "F1200");
  lv_obj_align(ku, LV_ALIGN_CENTER, 0, 20);
  k_status = label(v_bank, F12, C_DIM, "");
  lv_obj_set_width(k_status, 116);
  lv_label_set_long_mode(k_status, LV_LABEL_LONG_DOT); lv_obj_set_height(k_status, lv_font_get_line_height(lv_obj_get_style_text_font(k_status, 0)));
  lv_obj_set_style_text_align(k_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(k_status, 4, 124);
  lv_obj_t *gear = button(v_bank, LV_SYMBOL_SETTINGS " Settings", 112, 28, C_CARD2, [](lv_event_t *) { bank_settings(); });
  lv_obj_set_style_text_font(lv_obj_get_child(gear, 0), F12, 0);
  lv_obj_set_pos(gear, 6, 146);
  T ti = tile(v_bank, 126, 6, "IN", C_GREEN), to = tile(v_bank, 222, 6, "OUT", C_ORANGE), tt = tile(v_bank, 126, 44, "TIME", C_PURPLE);
  lv_obj_set_width(ti.c, 92); lv_obj_set_width(to.c, 92); lv_obj_set_width(tt.c, 188);
  k_in = ti.v; k_out = to.v; k_time = tt.v;
  k_ports = label(v_bank, F12, C_DIM, "");
  lv_obj_set_width(k_ports, 188);
  lv_label_set_long_mode(k_ports, LV_LABEL_LONG_DOT); lv_obj_set_height(k_ports, lv_font_get_line_height(lv_obj_get_style_text_font(k_ports, 0)));
  lv_obj_set_pos(k_ports, 130, 82);
  static const char *ON[] = {"USB", "DC 12V", "AC 230V", "Light"};
  for (int i = 0; i < 4; i++) {
    k_btn[i] = button(v_bank, ON[i], 97, 30, C_CARD, [](lv_event_t *e) {
      int k = (int)(intptr_t)lv_event_get_user_data(e);
      bool cur, conn; { Lock l; bool st[] = {S.bank.usb, S.bank.dc, S.bank.ac, S.bank.light}; cur = st[k]; conn = S.bank.connected; }
      if (!conn) { toast("Power bank not connected"); return; }
      bank_switch(k, !cur);
      toast(cur ? "Switching off..." : "Switching on...");
    }, (void *)(intptr_t)i);
    lv_obj_set_size(k_btn[i], 92, 32);
    lv_obj_set_pos(k_btn[i], 126 + (i % 2) * 96, 104 + (i / 2) * 38);
  }
}


static void day_name(char *o, size_t n, const char *date);
static void day_row(lv_layer_t *L, const lv_area_t &r, int k) {
  if (k >= (int)dcopy.size()) return;
  const DayRec &d = dcopy[k];
  char b[72];
  int p = d.yield_10wh * 100 / max(1, dmax);
  dl_dot(L, r, 7, 7, 5, 20, p > 66 ? C_AMBER : p > 33 ? C_ORANGE : C_FAINT);
  day_name(b, sizeof b, d.date);
  dl_text(L, r, 20, 2, F14, C_WHITE, b);
  snprintf(b, sizeof b, "peak %d W   %d.%02d-%d.%02d V", d.peak_w, d.vmin_cv / 100, d.vmin_cv % 100, d.vmax_cv / 100, d.vmax_cv % 100);
  dl_text(L, r, 20, 19, F12, C_DIM, b);
  snprintf(b, sizeof b, "%d.%02d kWh", d.yield_10wh / 100, d.yield_10wh % 100);
  dl_text(L, r, 10, 9, F16, C_AMBER, b, true);
}
static void stat(int &i, lv_color_t dot, const char *title, const char *val, lv_color_t vc = C_WHITE) {
  if (i >= (int)(sizeof stats / sizeof stats[0])) return;
  Stat &s = stats[i++];
  s.dot = dot; s.vc = vc; strlcpy(s.t, title, sizeof s.t); strlcpy(s.v, val, sizeof s.v);
}
static void dur(char *o, size_t n, uint32_t s) {
  if (s < 3600) snprintf(o, n, "%u min", (unsigned)(s / 60)); else snprintf(o, n, "%uh %02um", (unsigned)(s / 3600), (unsigned)(s / 60 % 60));
}
static const char *WD[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *MO[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static void day_name(char *o, size_t n, const char *date) {
  struct tm t{};
  t.tm_year = atoi(date) - 1900; t.tm_mon = atoi(date + 5) - 1; t.tm_mday = atoi(date + 8); t.tm_hour = 12;
  if (t.tm_year < 100 || t.tm_mon < 0 || t.tm_mon > 11) { strlcpy(o, date, n); return; }
  mktime(&t);
  snprintf(o, n, "%s %d %s", WD[t.tm_wday], t.tm_mday, MO[t.tm_mon]);
}

// ---- power bank settings: a drawn list, tap = next value
struct BSet { const char *name; uint16_t reg; const int16_t *opts; int n; int scale; const char *unit; };
static const int16_t O_ONOFF[] = {0, 1}, O_LIGHT[] = {0, 1, 2, 3}, O_AMP[] = {2, 4, 6, 8},
                     O_SCREEN[] = {30, 60, 180, 300, 0}, O_ACSB[] = {0, 60, 240, 480, 960, 1440}, O_DCSB[] = {0, 60, 240, 480, 1440},
                     O_USBSB[] = {0, 3, 5, 10, 30}, O_IDLE[] = {5, 10, 30, 60, 480}, O_FLOOR[] = {0, 50, 100, 150, 200, 250, 300},
                     O_CEIL[] = {600, 700, 800, 900, 1000};
static const BSet BS[] = {
  {"Light", 27, O_LIGHT, 4, 0, ""}, {"Key sound", 56, O_ONOFF, 2, 0, ""}, {"Silent AC charging", 57, O_ONOFF, 2, 0, ""},
  {"Solar / car charge", 20, O_AMP, 4, 1, " A"}, {"Screen off after", 62, O_SCREEN, 5, 2, ""}, {"AC off when idle", 60, O_ACSB, 6, 3, ""},
  {"DC off when idle", 61, O_DCSB, 5, 3, ""}, {"USB off when idle", 59, O_USBSB, 5, 3, ""}, {"Power off when idle", 68, O_IDLE, 5, 3, ""},
  {"Stop discharge at", 66, O_FLOOR, 7, 4, ""}, {"AC charge up to", 67, O_CEIL, 5, 4, ""},
};
static constexpr int BS_N = sizeof BS / sizeof BS[0];
static Bank kb;                      // copy painted without the lock
static lv_obj_t *bs_list, *bs_modal;
static int bs_cur(int i) {
  const Bank &b = kb;
  switch (BS[i].reg) {
    case 27: return b.light_mode; case 56: return b.s_key; case 57: return b.s_silent; case 20: return b.s_dc_amp;
    case 62: return b.s_screen; case 60: return b.s_ac_sb; case 61: return b.s_dc_sb; case 59: return b.s_usb_sb;
    case 68: return b.s_idle; case 66: return b.s_floor; case 67: return b.s_ceil;
  }
  return 0;
}
static void bs_fmt(char *o, size_t n, int i, int v) {
  static const char *LM[] = {"off", "on", "SOS", "strobe"};
  switch (BS[i].scale) {
    case 0: snprintf(o, n, "%s", BS[i].reg == 27 ? LM[v & 3] : v ? "on" : "off"); break;
    case 1: snprintf(o, n, "%d A", v); break;
    case 2: if (!v) snprintf(o, n, "never"); else if (v < 60) snprintf(o, n, "%d s", v); else snprintf(o, n, "%d min", v / 60); break;
    case 3: if (!v) snprintf(o, n, "never"); else if (v < 60) snprintf(o, n, "%d min", v); else snprintf(o, n, "%d h", v / 60); break;
    case 4: snprintf(o, n, "%d %%", v / 10); break;
  }
}
static void bs_row(lv_layer_t *L, const lv_area_t &r, int i) {
  char b[32];
  if (i == BS_N + 1) { dl_text(L, r, 12, 6, F14, C_RED, LV_SYMBOL_TRASH "  Forget this power bank"); return; }
  if (i == BS_N) {
    snprintf(b, sizeof b, "AC charge knob: %d W", kb.s_aclvl * 200);
    dl_text(L, r, 12, 6, F14, C_DIM, b);
    dl_text(L, r, 10, 8, F12, C_FAINT, kb.serial, true);
    return;
  }
  dl_text(L, r, 12, 6, F14, C_WHITE, BS[i].name);
  if (!kb.s_ok) { dl_text(L, r, 10, 8, F12, C_FAINT, "reading...", true); return; }
  bs_fmt(b, sizeof b, i, bs_cur(i));
  dl_text(L, r, 10, 6, F14, C_PURPLE, b, true);
}
static void bs_tap(int i) {
  if (i == BS_N + 1) { bank_forget(); close_modal(); toast("Forgotten: the next power bank close by is paired"); return; }
  if (i >= BS_N) return;
  { Lock l; kb = S.bank; }
  if (!kb.s_ok) { toast("Still reading the settings..."); return; }
  int cur = bs_cur(i), k = 0;
  while (k < BS[i].n && BS[i].opts[k] != cur) k++;
  int nv = BS[i].opts[(k + 1) % BS[i].n];
  if (!bank_set(BS[i].reg, nv)) { toast("Not allowed"); return; }
  char b[32]; bs_fmt(b, sizeof b, i, nv);
  char m[64]; snprintf(m, sizeof m, "%s: %s", BS[i].name, b);
  toast(m);
}
static uint32_t bs_ver = UINT32_MAX;
void bank_settings_update() {
  if (!g_modal || g_modal != bs_modal) return;
  Lock l;
  if (bs_ver == S.bank.ver) return;
  bs_ver = S.bank.ver; kb = S.bank;
  drawlist_redraw(bs_list);
}
void bank_settings() {
  { Lock l; kb = S.bank; if (!kb.connected) { toast("Power bank not connected"); return; } }
  lv_obj_t *m = modal(LV_SYMBOL_SETTINGS "  Power bank settings");
  modal_owner(&bs_modal, m);
  bs_list = drawlist(m, 4, 34, 312, 204, 30, bs_row);
  drawlist_count(bs_list, BS_N + 2);
  drawlist_on_tap(bs_list, bs_tap);
  bs_ver = UINT32_MAX;
}

static void bank_update() {
  const Bank &b = S.bank;
  if (kver == b.ver) return;
  kver = b.ver;
  bool f = b.data && millis() - b.at < 30000;
  int p = f ? (int)(b.soc + 0.5f) : 0;
  if (lv_arc_get_value(k_arc) != p) lv_arc_set_value(k_arc, p);
  if (f) set_fmt(k_pct, "%d%%", p); else set_txt(k_pct, "--");
  if (b.seen) set_txt(k_status, b.status);
  else set_txt(k_status, "searching...");
  set_col(k_status, f ? C_DIM : C_AMBER);
  if (f) {
    set_fmt(k_in, "%d W", b.in_w);
    set_fmt(k_out, "%d W", b.out_w);
    int net = b.in_w - b.out_w;
    char t[24];
    if (net < 0 && b.remain_min) snprintf(t, sizeof t, "%dh%02d left", b.remain_min / 60, b.remain_min % 60);
    else if (net > 0 && b.to_full_min) snprintf(t, sizeof t, "full in %dh%02d", b.to_full_min / 60, b.to_full_min % 60);
    else strcpy(t, b.full ? "full" : "steady");
    set_txt(k_time, t);
    float usb = b.usb_a1 + b.usb_a2 + b.usb_c1 + b.usb_c2;
    set_fmt(k_ports, "USB %.0f W  12V %.0f W  AC %d W", usb, b.car_w, b.ac_out_w);
    if (b.fault) { set_txt(k_status, LV_SYMBOL_WARNING " power bank reports a fault"); set_col(k_status, C_RED); }
  } else { set_txt(k_in, "-"); set_txt(k_out, "-"); set_txt(k_time, "-"); set_txt(k_ports, ""); }
  bool st[] = {b.usb, b.dc, b.ac, b.light};
  for (int i = 0; i < 4; i++) {
    set_bg(k_btn[i], f && st[i] ? C_PURPLE : C_CARD);
    set_col(lv_obj_get_child(k_btn[i], 0), f && st[i] ? C_WHITE : C_DIM);
  }
}

void bank_page_update(bool force) { if (force) kver = UINT32_MAX; Lock l; bank_update(); }

void power_update(bool force) {
  if (force) ver = hver = dver = UINT32_MAX;
  Lock l;
  const Victron &v = S.vic;
  uint32_t now = millis();
  bool fresh = v.decoded && now - v.at < 30000;

  // footer + setup
  if (seg.active == 0) {
    show(v_setup, !fresh);
    if (!fresh) {
      const char *m = v.model ? vic_model(v.model) : nullptr;
      char mm[40]; if (!m) { snprintf(mm, sizeof mm, "Victron charger %04X", v.model); m = mm; }
      if (!v.seen) { set_txt(su_title, "Looking for your SmartSolar..."); set_txt(su_text, "Stay within about 10 m of the charger."); }
      else if (!v.have_key) { set_fmt(su_title, "Found %s", m); set_fmt(su_text, "Signal %d dBm. Enter its key to see live values.", v.rssi); }
      else if (v.key_bad) { set_fmt(su_title, "Key does not fit %s", m); set_txt(su_text, "Check the key in VictronConnect, then enter it again."); }
      else { set_txt(su_title, "Waiting for data..."); set_txt(su_text, m); }
      return;
    }
    const char *err = vic_error(v.error);
    if (err) { set_fmt(foot, LV_SYMBOL_WARNING " %s", err); set_col(foot, C_RED); }
    else { set_txt(foot, S.demo ? "DEMO" : ""); set_col(foot, C_AMBER); }
  }
  if (seg.active != 2) { if (ver == v.ver) return; ver = v.ver; }

  if (seg.active == 0) {
    int w = v.pv_w == 0xFFFF ? 0 : v.pv_w;
    int range = v.vbat_cv != 0x7FFF && v.vbat_cv > 1900 ? 440 : 220;
    if (lv_arc_get_max_value(arc) != range) lv_arc_set_range(arc, 0, range);
    if (lv_arc_get_value(arc) != min(w, range)) lv_arc_set_value(arc, min(w, range));
    set_fmt(pv, "%d", w);
    set_col(pv, w ? C_WHITE : C_DIM);
    set_txt(pill_t, vic_state(v.state));
    set_bg(pill, state_color(v.state));
    set_col(pill_t, v.state >= 3 && v.state <= 6 ? C_BG : C_WHITE);
    int p = vic_soc(v);
    if (v.vbat_cv != 0x7FFF) set_fmt(tiles[0].v, "%d.%02d V", v.vbat_cv / 100, v.vbat_cv % 100); else set_txt(tiles[0].v, "-");
    set_col(tiles[0].v, pct_color(p));
    if (v.ibat_da != 0x7FFF) set_fmt(tiles[1].v, "%s%d.%d A", v.ibat_da < 0 ? "-" : "", abs(v.ibat_da) / 10, abs(v.ibat_da) % 10); else set_txt(tiles[1].v, "-");
    if (v.yield_10wh == 0xFFFF) set_txt(tiles[2].v, "-");
    else if (v.yield_10wh < 100) set_fmt(tiles[2].v, "%d Wh", v.yield_10wh * 10);
    else set_fmt(tiles[2].v, "%d.%02d kWh", v.yield_10wh / 100, v.yield_10wh % 100);
    if (v.load_da != 0x1FF) set_fmt(tiles[3].v, "%d.%d A", v.load_da / 10, v.load_da % 10); else set_txt(tiles[3].v, "not used");
    set_col(tiles[3].v, v.load_da != 0x1FF ? C_WHITE : C_DIM);
    if (v.peak_at[0]) set_fmt(tiles[4].v, "%d W  %s", v.peak_w, v.peak_at); else set_fmt(tiles[4].v, "%d W", v.peak_w);
    if (p >= 0) set_fmt(tiles[5].v, "~%d%%%s", p, v.state == 5 || v.state == 6 ? " full" : ""); else set_txt(tiles[5].v, "-");
    set_col(tiles[5].v, pct_color(p));
    if (hver != v.hist_ver) {
      hver = v.hist_ver;
      int mx = 50;
      for (int k = 0; k < v.hist_n; k++) mx = max(mx, (int)v.hist[k]);
      lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, mx + mx / 10);
      int pad = VHIST - v.hist_n;
      for (int k = 0; k < VHIST; k++) lv_chart_set_value_by_id(chart, ser, k, k < pad ? LV_CHART_POINT_NONE : v.hist[k - pad]);
      lv_chart_refresh(chart);
      if (S.demo) set_txt(chart_t, "SOLAR, LAST 12 HOURS");
      else set_fmt(chart_t, "SOLAR, LAST %d MIN", max(1, v.hist_n));
    }
    return;
  }

  if (seg.active == 1) {
    char b[48], d1[16];
    int i = 0;
    if (v.yield_10wh != 0xFFFF) snprintf(b, sizeof b, "%d.%02d kWh", v.yield_10wh / 100, v.yield_10wh % 100); else strcpy(b, "-");
    stat(i, C_AMBER, "Solar yield", b, C_AMBER);
    if (v.peak_at[0]) snprintf(b, sizeof b, "%d W at %s", v.peak_w, v.peak_at); else snprintf(b, sizeof b, "%d W", v.peak_w);
    stat(i, C_ORANGE, "Peak power", b);
    snprintf(b, sizeof b, "%.1f Ah", v.ah_in); stat(i, C_TEAL, "Charged into battery", b, C_TEAL);
    if (v.load_da != 0x1FF) { snprintf(b, sizeof b, "%.1f Ah", v.ah_load); stat(i, C_BLUE, "Used by load output", b); }
    snprintf(b, sizeof b, "%d.%02d / %d.%02d V", v.vmin_cv / 100, v.vmin_cv % 100, v.vmax_cv / 100, v.vmax_cv % 100);
    stat(i, C_GREEN, "Battery low / high", b);
    dur(d1, sizeof d1, v.sec_bulk); stat(i, C_ORANGE, "Time in Bulk", d1);
    dur(d1, sizeof d1, v.sec_abs); stat(i, C_AMBER, "Time in Absorption", d1);
    dur(d1, sizeof d1, v.sec_float); stat(i, C_GREEN, "Time in Float", d1);
    dur(d1, sizeof d1, S.demo ? (now - v.state_since) / 1000 * 720 : (now - v.state_since) / 1000);
    snprintf(b, sizeof b, "%s, %s", vic_state(v.state), d1); stat(i, state_color(v.state), "Charger now", b);
    if (v.last_error) snprintf(b, sizeof b, "#%d at %s", v.last_error, v.last_error_at); else strcpy(b, "none");
    stat(i, v.last_error ? C_RED : C_GREEN, "Last charger error", b, v.last_error ? C_RED : C_WHITE);
    const char *m = vic_model(v.model);
    stat(i, C_PURPLE, m ? m : "Victron charger", S.demo ? "demo" : v.addr);
    n_stats = i; drawlist_count(today_dl, i);
    return;
  }

  // days
  if (dver == S.days_ver) return;
  dver = S.days_ver;
  int n = S.days.size(), first = max(0, n - 14), mx = 50;
  for (int k = first; k < n; k++) mx = max(mx, (int)S.days[k].yield_10wh);
  lv_chart_set_range(d_chart, LV_CHART_AXIS_PRIMARY_Y, 0, mx + mx / 10);
  int pad = 14 - (n - first);
  for (int k = 0; k < 14; k++) lv_chart_set_value_by_id(d_chart, d_ser, k, k < pad ? LV_CHART_POINT_NONE : S.days[first + k - pad].yield_10wh);
  lv_chart_refresh(d_chart);
  float tot = 0; for (int k = first; k < n; k++) tot += S.days[k].yield_10wh / 100.0f;
  if (n) set_fmt(d_title, "LAST %d DAYS  %.2f kWh  (avg %.2f)", n - first, tot, tot / (n - first));
  else set_txt(d_title, "SOLAR PER DAY");
  dcopy.assign(S.days.rbegin(), S.days.rend());               // newest first, painted without the lock
  dmax = mx;
  drawlist_count(days_dl, dcopy.size());
  show(days_empty, dcopy.empty());
  set_txt(days_empty, S.sd_ok ? "No finished days yet. A day is saved each morning when the charger starts a new day."
                              : "Insert a microSD card (FAT32) to keep the history.");
}

void bank_settings_test(int tap) { if (!g_modal) bank_settings(); if (g_modal && g_modal == bs_modal && tap >= 0) bs_tap(tap); }
}  // namespace ui
