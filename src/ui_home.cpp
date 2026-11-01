// HOME: 3 x 2 cards. Top: gel battery | solar | power bank. Bottom: inside | outside | weather.
#include "ui_kit.hpp"

namespace ui {

lv_color_t pct_color(int p) { return p < 0 ? C_DIM : p >= 50 ? C_GREEN : p >= 25 ? C_AMBER : C_RED; }
lv_color_t state_color(uint8_t s) {
  switch (s) { case 3: return C_ORANGE; case 4: return C_AMBER; case 5: case 6: return C_GREEN; case 2: return C_RED; default: return C_CARD2; }
}
lv_color_t temp_color(float t) { return isnan(t) ? C_DIM : t < 3 ? C_SKY : t < 15 ? C_BLUE : t < 26 ? C_WHITE : C_ORANGE; }

struct Big { lv_obj_t *card, *num, *unit, *track, *fill, *l1, *l2; };
struct Mini { lv_obj_t *card, *big, *sub, *icon; };
static Big bat, sol, bank;
static Mini m_in, m_out, m_wx;
static constexpr int CW = 101, X0 = 4, GAP = 4;

static lv_obj_t *caption(lv_obj_t *c, const char *t, lv_color_t accent) {
  lv_obj_t *a = box(c, 3, 12, accent);
  lv_obj_set_style_radius(a, 2, 0);
  lv_obj_set_pos(a, 8, 7);
  lv_obj_t *l = label(c, F12, C_DIM, t);
  lv_obj_set_pos(l, 15, 5);
  return l;
}
static Big big_card(lv_obj_t *p, int i, const char *cap, const char *unit, lv_color_t accent) {
  Big b;
  b.card = card(p, X0 + i * (CW + GAP), 4, CW, 104);
  caption(b.card, cap, accent);
  b.num = label(b.card, F28, C_WHITE, "--");
  lv_obj_set_pos(b.num, 8, 22);
  b.unit = label(b.card, F14, C_DIM, unit);
  b.track = bar_make(b.card, 9, 58, CW - 18, 6, &b.fill);
  b.l1 = label(b.card, F12, C_WHITE, "");
  lv_obj_set_pos(b.l1, 9, 68);
  b.l2 = label(b.card, F12, C_DIM, "");
  lv_obj_set_width(b.l2, CW - 14);
  lv_label_set_long_mode(b.l2, LV_LABEL_LONG_DOT); lv_obj_set_height(b.l2, lv_font_get_line_height(lv_obj_get_style_text_font(b.l2, 0)));
  lv_obj_set_pos(b.l2, 9, 84);
  return b;
}
static Mini mini(lv_obj_t *p, int i, const char *cap, lv_color_t accent) {
  Mini m;
  m.card = card(p, X0 + i * (CW + GAP), 112, CW, 64);
  caption(m.card, cap, accent);
  m.big = label(m.card, F24, C_WHITE, "--");
  lv_obj_set_pos(m.big, 8, 19);
  m.sub = label(m.card, F12, C_DIM, "");
  lv_obj_set_width(m.sub, CW - 14);
  lv_label_set_long_mode(m.sub, LV_LABEL_LONG_DOT); lv_obj_set_height(m.sub, lv_font_get_line_height(lv_obj_get_style_text_font(m.sub, 0)));
  lv_obj_set_pos(m.sub, 9, 46);
  m.icon = nullptr;
  return m;
}

void home_build(lv_obj_t *t) {
  bat = big_card(t, 0, "GEL BATTERY", "%", C_GREEN);
  sol = big_card(t, 1, "SOLAR", "W", C_AMBER);
  bank = big_card(t, 2, "POWERBANK", "%", C_PURPLE);
  m_in = mini(t, 0, "INSIDE", C_TEAL);
  m_out = mini(t, 1, "OUTSIDE", C_BLUE);
  m_wx = mini(t, 2, "WEATHER", C_AMBER);
  auto go = [](lv_obj_t *c, int tab) {
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, [](lv_event_t *e) { goto_tab((int)(intptr_t)lv_event_get_user_data(e)); }, LV_EVENT_CLICKED, (void *)(intptr_t)tab);
  };
  go(bat.card, 1); go(sol.card, 1); go(bank.card, 3); go(m_in.card, 2); go(m_out.card, 2); go(m_wx.card, 2);
  m_wx.icon = lv_obj_create(m_wx.card);
  lv_obj_remove_style_all(m_wx.icon);
  lv_obj_set_pos(m_wx.icon, 64, 16);
}

static void set_big(Big &b, int v, lv_color_t c) {
  if (v >= 0) set_fmt(b.num, "%d", v); else set_txt(b.num, "--");
  set_col(b.num, v >= 0 ? c : C_FAINT);
  lv_obj_align_to(b.unit, b.num, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -5);
}
static void temp_mini(Mini &m, const Sensor *s, const char *none) {
  if (!s || isnan(s->t)) { set_txt(m.big, "--"); set_col(m.big, C_FAINT); set_txt(m.sub, none); return; }
  set_fmt(m.big, "%.1f\xC2\xB0", s->t);
  set_col(m.big, temp_color(s->t));
  if (!isnan(s->h)) set_fmt(m.sub, LV_SYMBOL_TINT "%d%%  " LV_SYMBOL_DOWN "%.0f\xC2\xB0", (int)(s->h + 0.5f), s->tmin);
  else set_fmt(m.sub, LV_SYMBOL_DOWN "%.0f\xC2\xB0 " LV_SYMBOL_UP "%.0f\xC2\xB0", s->tmin, s->tmax);
}
static void hm(char *o, size_t n, int min) { if (min >= 60) snprintf(o, n, "%dh%02d", min / 60, min % 60); else snprintf(o, n, "%d min", min); }

void home_update() {
  Lock l;
  // gel battery (Victron)
  const Victron &v = S.vic;
  bool fresh = v.decoded && millis() - v.at < 30000;
  int p = fresh ? vic_soc(v) : -1;
  set_big(bat, p, pct_color(p));
  bar_fill(bat.track, bat.fill, p, pct_color(p));
  if (fresh && v.vbat_cv != 0x7FFF) {
    int ia = v.ibat_da == 0x7FFF ? 0 : v.ibat_da;
    set_fmt(bat.l1, "%d.%02d V", v.vbat_cv / 100, v.vbat_cv % 100);
    set_fmt(bat.l2, "%s%d.%d A", ia > 0 ? "+" : ia < 0 ? "-" : "", abs(ia) / 10, abs(ia) % 10);
  } else { set_txt(bat.l1, ""); set_txt(bat.l2, v.have_key || S.demo ? "waiting" : "set up on Power"); }
  // solar
  int w = fresh && v.pv_w != 0xFFFF ? v.pv_w : -1;
  set_big(sol, w, w > 0 ? C_AMBER : C_DIM);
  bar_fill(sol.track, sol.fill, w <= 0 ? 0 : w * 100 / 220, C_AMBER);
  set_txt(sol.l1, fresh ? vic_state(v.state) : "");
  set_col(sol.l1, fresh && v.state >= 3 && v.state <= 6 ? state_color(v.state) : C_DIM);
  if (fresh && v.yield_10wh != 0xFFFF) set_fmt(sol.l2, "%d.%02d kWh", v.yield_10wh / 100, v.yield_10wh % 100); else set_txt(sol.l2, "");
  // power bank (Fossibot)
  const Bank &b = S.bank;
  bool bf = b.data && millis() - b.at < 30000;
  int bp = bf ? (int)(b.soc + 0.5f) : -1;
  set_big(bank, bp, pct_color(bp));
  bar_fill(bank.track, bank.fill, bp, C_PURPLE);
  if (bf) {
    set_fmt(bank.l1, LV_SYMBOL_DOWN "%d  " LV_SYMBOL_UP "%d W", b.in_w, b.out_w);
    char t[12];
    if (b.remain_min && b.out_w > b.in_w) { hm(t, sizeof t, b.remain_min); set_fmt(bank.l2, "%s left", t); }
    else if (b.to_full_min && b.in_w > b.out_w) { hm(t, sizeof t, b.to_full_min); set_fmt(bank.l2, "full in %s", t); }
    else set_fmt(bank.l2, "%s%s%s", b.ac ? "AC " : "", b.dc ? "DC " : "", b.usb ? "USB" : "");
  } else { set_txt(bank.l1, ""); set_txt(bank.l2, b.seen ? b.status : "not found"); }

  temp_mini(m_in, sens_by_role(R_INSIDE), "tap Climate");
  const Sensor *o = sens_by_role(R_OUTSIDE);
  if ((!o || isnan(o->t)) && S.wx.ok) {                // no outside sensor: use the weather
    set_fmt(m_out.big, "%.1f\xC2\xB0", S.wx.t); set_col(m_out.big, temp_color(S.wx.t));
    set_fmt(m_out.sub, LV_SYMBOL_TINT "%d%% (forecast)", (int)S.wx.hum);
  } else temp_mini(m_out, o, "tap Climate");
  if (S.wx.ok) {
    wx_icon(m_wx.icon, S.wx.code, 30);
    set_fmt(m_wx.big, "%d\xC2\xB0", S.wx.nd ? S.wx.d_max[0] : (int)S.wx.t);
    set_col(m_wx.big, C_WHITE);
    if (S.wx.warn_lvl == 2) { set_fmt(m_wx.sub, LV_SYMBOL_WARNING " %s", S.wx.warn); set_col(m_wx.sub, C_ORANGE); }
    else {
      set_col(m_wx.sub, C_DIM);
      if (S.wx.nd && S.wx.d_rain[0] >= 20) set_fmt(m_wx.sub, "%s %d%%", wx_text(S.wx.code), S.wx.d_rain[0]);
      else set_txt(m_wx.sub, wx_text(S.wx.code));
    }
  } else { set_txt(m_wx.big, "--"); set_col(m_wx.big, C_FAINT); set_txt(m_wx.sub, "needs internet"); }
}

}  // namespace ui
