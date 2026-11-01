// LEVEL: bubble + how many cm to raise each corner (camper length / width from Settings).
#include "ui_kit.hpp"

namespace ui {

static lv_obj_t *ring, *bubble, *pitch_l, *roll_l, *verdict, *missing, *van;
static lv_obj_t *corner[4];
static constexpr int R = 70, BUB = 24;

void level_build(lv_obj_t *t) {
  lv_obj_t *c = card(t, 4, 4, 168, 172);
  ring = box(c, 2 * R, 2 * R, C_BG);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(ring, 2, 0);
  lv_obj_set_style_border_color(ring, C_LINE, 0);
  lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, 8);
  for (int d : {R, R / 3}) {          // inner rings: 1 degree and 3 degree (edge)
    if (d == R) continue;
    lv_obj_t *in = box(ring, 2 * d, 2 * d, C_BG);
    lv_obj_set_style_radius(in, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(in, 1, 0);
    lv_obj_set_style_border_color(in, C_FAINT, 0);
    lv_obj_center(in);
  }
  lv_obj_t *h = box(ring, 2 * R - 8, 1, C_LINE); lv_obj_center(h);
  lv_obj_t *v = box(ring, 1, 2 * R - 8, C_LINE); lv_obj_center(v);
  bubble = box(ring, BUB, BUB, C_GREEN);
  lv_obj_set_style_radius(bubble, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(bubble, 2, 0);
  lv_obj_set_style_border_color(bubble, C_WHITE, 0);
  lv_obj_set_style_border_opa(bubble, LV_OPA_50, 0);
  lv_obj_center(bubble);
  lv_obj_t *f = label(c, F12, C_FAINT, LV_SYMBOL_UP " front");
  lv_obj_align(f, LV_ALIGN_TOP_LEFT, 6, 2);
  verdict = label(c, F14, C_WHITE, "");
  lv_obj_align(verdict, LV_ALIGN_BOTTOM_MID, 0, -6);

  // right: angles + top view with cm per corner
  lv_obj_t *r = card(t, 176, 4, 140, 172);
  pitch_l = label(r, F14, C_WHITE, "");
  lv_obj_set_pos(pitch_l, 10, 4);
  roll_l = label(r, F14, C_WHITE, "");
  lv_obj_set_pos(roll_l, 10, 22);
  van = box(r, 50, 84, C_CARD2);
  lv_obj_set_style_radius(van, 12, 0);
  lv_obj_set_pos(van, 45, 50);
  lv_obj_t *cab = box(van, 40, 14, C_LINE);
  lv_obj_set_style_radius(cab, 6, 0);
  lv_obj_align(cab, LV_ALIGN_TOP_MID, 0, 4);
  static const int X[] = {4, 98, 4, 98}, Y[] = {50, 50, 118, 118};
  for (int i = 0; i < 4; i++) {
    corner[i] = label(r, F14, C_GREEN, "ok");
    lv_obj_set_width(corner[i], 42);
    lv_obj_set_style_text_align(corner[i], i % 2 ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(corner[i], i % 2 ? X[i] : X[i] - 2, Y[i]);
  }
  lv_obj_t *z = button(r, LV_SYMBOL_REFRESH " Zero", 120, 22, C_CARD2, [](lv_event_t *) { level_zero(); toast("Level zeroed here"); });
  lv_obj_set_style_text_font(lv_obj_get_child(z, 0), F12, 0);
  lv_obj_align(z, LV_ALIGN_BOTTOM_MID, 0, -6);

  missing = box(t, 320, PAGE_H, C_BG);
  lv_obj_t *ic = label(missing, F40, C_PURPLE, LV_SYMBOL_GPS);
  lv_obj_set_pos(ic, 14, 8);
  lv_obj_t *mt = label(missing, F16, C_WHITE, "Add a RuuviTag");
  lv_obj_set_pos(mt, 64, 10);
  lv_obj_t *mx = label(missing, F12, C_DIM,
                       "A RuuviTag is a small Bluetooth sensor on a coin battery. It sends its tilt, "
                       "temperature, humidity and air pressure, no wires and no pairing.\n\n"
                       "Lay it flat in the camper (arrow or logo to the front), park on level ground once and tap Zero.\n\n"
                       "Until then: Tools > Settings > Demo shows how it works.");
  lv_obj_set_width(mx, 300);
  lv_label_set_long_mode(mx, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(mx, 12, 54);
}


void level_update() {
  float p, r; bool present;
  { Lock l; p = S.pitch; r = S.roll; present = S.lvl_present; }
  show(missing, !present);
  if (!present) return;
  float m = max(fabsf(p), fabsf(r));
  lv_color_t c = m < 0.5f ? C_GREEN : m < 1.5f ? C_AMBER : C_RED;
  // the bubble floats to the HIGH side, full ring = 3 degrees
  float bx = constrain(r / 3.0f, -1.0f, 1.0f) * (R - BUB / 2 - 3), by = constrain(-p / 3.0f, -1.0f, 1.0f) * (R - BUB / 2 - 3);
  float len = sqrtf(bx * bx + by * by), lim = R - BUB / 2 - 3;
  if (len > lim) { bx *= lim / len; by *= lim / len; }
  int ix = R - BUB / 2 + (int)bx - 2, iy = R - BUB / 2 + (int)by - 2;
  if (lv_obj_get_x(bubble) != ix || lv_obj_get_y(bubble) != iy) lv_obj_set_pos(bubble, ix, iy);
  set_bg(bubble, c);
  set_fmt(pitch_l, "Front %s%.1f\xC2\xB0", p >= 0 ? "+" : "", p);
  set_fmt(roll_l, "Side   %s%.1f\xC2\xB0", r >= 0 ? "+" : "", r);
  set_col(pitch_l, fabsf(p) < 0.5f ? C_GREEN : C_WHITE);
  set_col(roll_l, fabsf(r) < 0.5f ? C_GREEN : C_WHITE);
  set_txt(verdict, m < 0.5f ? LV_SYMBOL_OK "  Level" : m < 1.5f ? "Almost level" : "Not level");
  set_col(verdict, c);
  // corner heights: + front / + right; raise every corner up to the highest one
  float L = cfg.len_cm, Wd = cfg.width_cm, tp = tanf(p / 57.2958f), tr = tanf(r / 57.2958f);
  float hgt[4] = {L / 2 * tp - Wd / 2 * tr, L / 2 * tp + Wd / 2 * tr, -L / 2 * tp - Wd / 2 * tr, -L / 2 * tp + Wd / 2 * tr};
  float top = max(max(hgt[0], hgt[1]), max(hgt[2], hgt[3]));
  for (int i = 0; i < 4; i++) {
    int cm = (int)(top - hgt[i] + 0.5f);
    if (cm < 1) { set_txt(corner[i], "ok"); set_col(corner[i], C_GREEN); }
    else { set_fmt(corner[i], "+%dcm", cm); set_col(corner[i], cm < 4 ? C_AMBER : C_RED); }
  }
}

}  // namespace ui
