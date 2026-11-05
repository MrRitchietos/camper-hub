// LEVEL: bubble + how many cm to raise each corner (camper length / width from Settings).
#include "ui_kit.hpp"

namespace ui {

static lv_obj_t *ring, *bubble, *pitch_l, *roll_l, *verdict, *missing, *van, *live_l;
static lv_obj_t *corner[4];
static constexpr int R = 70, BUB = 24;

// ---- calibration wizard: 3 steps, each one waits until the camper stands still (see level.cpp)
static lv_obj_t *cal_m, *cal_t, *cal_s, *cal_skip, *cal_go;
static lv_timer_t *cal_tm = nullptr;
static int cal_step = 0; static uint32_t cal_until = 0, cal_since = 0; static int cal_last = 0;
static const char *CAL_TXT[] = {
  "Park where you want to level, hand brake on, everyone sits still.\n\nTap Measure.",
  "Turn the camper around and park with the wheels on the same spots (front where the back was).\n\n"
  "Tap Measure. Only on ground you know is level: Skip.",
  "Now raise only the front a bit, for example the front wheels on the levelling ramps.\n\n"
  "Tap Measure. Skip: the tag arrow points to the front (Settings > Level tag turned)."};
static void cal_show() {
  char t[32]; snprintf(t, sizeof t, "Step %d of 3", cal_step + 1);
  set_txt(cal_t, CAL_TXT[cal_step]);
  set_txt(cal_s, t); set_col(cal_s, C_DIM);
  show(cal_skip, cal_step > 0);
  lv_obj_remove_state(cal_go, LV_STATE_DISABLED);
}
static void cal_done() {
  level_cal_save(); close_modal();
  float p; uint8_t c; { Lock l; p = S.pitch; c = S.lvl_cal; }
  char b[80];
  if (c == 2) snprintf(b, sizeof b, "Calibrated. Front now %+.1f\xC2\xB0 (should be + while the front is up)", p);
  else snprintf(b, sizeof b, "Level calibrated");
  toast(b);
}
static void cal_next() { Serial.printf("cal step %d done\n", cal_step + 1); if (cal_step < 2) { cal_step++; cal_show(); } else cal_done(); }
static void cal_tick(lv_timer_t *tm) {
  if (!cal_m) { lv_timer_delete(tm); cal_tm = nullptr; return; }
  if (!cal_until) return;
  int r = level_measure(cal_step, cal_since);
  if (r == 2) cal_since = millis();                     // moved: start the readings again from now
  static int last_r = -1; if (r != last_r) { last_r = r; Serial.printf("cal step %d measure %d\n", cal_step + 1, r); }
  if (r == 0) { cal_until = 0; beep(1); cal_next(); return; }
  if (r != 1) cal_last = r;
  if (r == 3 || (int32_t)(millis() - cal_until) > 0) {
    cal_until = 0; lv_obj_remove_state(cal_go, LV_STATE_DISABLED);
    set_txt(cal_s, r == 3 ? "Front not raised enough (needs 1 degree): raise more, tap Measure"
                 : cal_last == 2 ? "The camper keeps moving: wait, tap Measure again" : "No readings from the tag: is it close by?");
    set_col(cal_s, C_AMBER);
    return;
  }
  set_txt(cal_s, r == 2 ? "Camper moved: starting again, hold still..." : "Measuring, hold still (about 25 s)...");
}
static void open_cal() {
  bool present; { Lock l; present = S.lvl_present && strcmp(S.lvl_tag, "demo"); }
  if (!present) { toast(demo_on ? "Turn the demo off first" : "No RuuviTag heard yet"); return; }
  level_cal_start();
  cal_step = 0; cal_until = 0; cal_last = 0;
  lv_obj_t *m = modal(LV_SYMBOL_REFRESH "  Calibrate the level");
  modal_owner(&cal_m, m);
  cal_t = label(m, F14, C_WHITE, "");
  lv_obj_set_width(cal_t, 300); lv_label_set_long_mode(cal_t, LV_LABEL_LONG_WRAP); lv_obj_set_pos(cal_t, 10, 40);
  cal_s = label(m, F12, C_DIM, "");
  lv_obj_set_width(cal_s, 300); lv_label_set_long_mode(cal_s, LV_LABEL_LONG_WRAP); lv_obj_set_pos(cal_s, 10, 164);
  cal_skip = button(m, "Skip", 100, 32, C_CARD2, [](lv_event_t *) { if (cal_until) return; level_cal_skip(cal_step); cal_next(); });
  lv_obj_set_pos(cal_skip, 10, 200);
  cal_go = button(m, "Measure", 190, 32, C_TEAL, [](lv_event_t *) {
    if (cal_until) return;
    cal_until = millis() + 90000; cal_since = millis(); cal_last = 0; lv_obj_add_state(cal_go, LV_STATE_DISABLED); set_txt(cal_s, "Measuring, hold still..."); });
  lv_obj_set_pos(cal_go, 120, 200);
  cal_show();
  if (!cal_tm) cal_tm = lv_timer_create(cal_tick, 500, nullptr);
}

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
  live_l = label(c, F12, C_FAINT, "");                 // lights up at every new reading from the tag
  lv_obj_align(live_l, LV_ALIGN_TOP_RIGHT, -6, 2);
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
    lv_label_set_long_mode(corner[i], LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(corner[i], i % 2 ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(corner[i], i % 2 ? X[i] : X[i] - 2, Y[i]);
  }
  lv_obj_t *z = button(r, LV_SYMBOL_REFRESH " Calibrate", 120, 22, C_CARD2, [](lv_event_t *) { open_cal(); });
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
                       "Fix it anywhere in the camper where it cannot move (flat or on a wall), then tap Calibrate on this page: "
                       "park, turn around, raise the front. No level ground needed.\n\n"
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
  {
    static uint16_t seen_n = 0; static uint32_t lit_at = 0;
    uint16_t n; uint32_t gap; bool demo; { Lock l; n = S.lvl_n; gap = S.lvl_gap; demo = !strcmp(S.lvl_tag, "demo"); }
    if (n != seen_n) { seen_n = n; lit_at = millis(); }
    bool lit = millis() - lit_at < 400;
    if (demo) set_txt(live_l, "");
    else if (gap) set_fmt(live_l, LV_SYMBOL_REFRESH " %.1f s", gap / 1000.0f);
    set_col(live_l, lit ? C_TEAL : C_FAINT);
  }
  float m = max(fabsf(p), fabsf(r));
  lv_color_t c = m < 0.5f ? C_GREEN : m < 1.5f ? C_AMBER : C_RED;
  // the bubble floats to the HIGH side, full ring = 3 degrees
  float bx = constrain(r / 3.0f, -1.0f, 1.0f) * (R - BUB / 2 - 3), by = constrain(-p / 3.0f, -1.0f, 1.0f) * (R - BUB / 2 - 3);
  float len = sqrtf(bx * bx + by * by), lim = R - BUB / 2 - 3;
  if (len > lim) { bx *= lim / len; by *= lim / len; }
  int ix = lroundf(bx), iy = lroundf(by);            // offset from the ring centre (the bubble is centre aligned)
  if (lv_obj_get_style_x(bubble, 0) != ix || lv_obj_get_style_y(bubble, 0) != iy) lv_obj_align(bubble, LV_ALIGN_CENTER, ix, iy);
  set_bg(bubble, c);
  set_fmt(pitch_l, "Front %s%.1f\xC2\xB0", p >= 0 ? "+" : "", p);
  set_fmt(roll_l, "Side   %s%.1f\xC2\xB0", r >= 0 ? "+" : "", r);
  set_col(pitch_l, fabsf(p) < 0.5f ? C_GREEN : C_WHITE);
  set_col(roll_l, fabsf(r) < 0.5f ? C_GREEN : C_WHITE);
  uint8_t cal; { Lock l; cal = S.lvl_cal; }
  bool demo; { Lock l; demo = !strcmp(S.lvl_tag, "demo"); }
  bool moved; { Lock l; moved = S.lvl_moved; }
  if (!cal && !demo) { set_txt(verdict, "Tap Calibrate first"); set_col(verdict, C_AMBER); }
  else if (moved && !demo) { set_txt(verdict, "Tag moved? Calibrate"); set_col(verdict, C_AMBER); }
  else { set_txt(verdict, m < 0.5f ? LV_SYMBOL_OK "  Level" : m < 1.5f ? "Almost level" : "Not level"); set_col(verdict, c); }
  // corner heights: + front / + right; raise every corner up to the highest one
  float L = cfg.len_cm, Wd = cfg.width_cm, tp = tanf(p / 57.2958f), tr = tanf(r / 57.2958f);
  float hgt[4] = {L / 2 * tp - Wd / 2 * tr, L / 2 * tp + Wd / 2 * tr, -L / 2 * tp - Wd / 2 * tr, -L / 2 * tp + Wd / 2 * tr};
  float top = max(max(hgt[0], hgt[1]), max(hgt[2], hgt[3]));
  for (int i = 0; i < 4; i++) {
    int cm = (int)(top - hgt[i] + 0.5f);
    const lv_font_t *fnt = cm >= 10 ? F12 : F14;         // "+13cm" fits next to the van in the small font
    if (lv_obj_get_style_text_font(corner[i], 0) != fnt) lv_obj_set_style_text_font(corner[i], fnt, 0);
    if (cm < 1) { set_txt(corner[i], "ok"); set_col(corner[i], C_GREEN); }
    else { set_fmt(corner[i], "+%dcm", cm > 99 ? 99 : cm); set_col(corner[i], cm < 4 ? C_AMBER : C_RED); }
  }
}

}  // namespace ui
