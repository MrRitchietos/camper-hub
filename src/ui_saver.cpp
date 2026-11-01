// Screensaver: the camper parked under the sky (stars + moon at night with warm windows, sun by day),
// clock and the key numbers. One self-painting object; redrawn every 2 s; the scene drifts slowly.
#include "ui_kit.hpp"

namespace ui {

static lv_obj_t *sv, *scene, *s_clock, *s_date, *s_info;
static bool night = true;
static int dx = 0, phase = 0;

static void rr(lv_layer_t *L, int x, int y, int w, int h, int rad, lv_color_t c, lv_opa_t o = LV_OPA_COVER) {
  lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
  d.bg_color = c; d.bg_opa = o; d.radius = rad;
  lv_area_t a = {x, y, x + w - 1, y + h - 1};
  lv_draw_rect(L, &d, &a);
}
static void tri(lv_layer_t *L, int x1, int y1, int x2, int y2, int x3, int y3, lv_color_t c) {
  lv_draw_triangle_dsc_t d; lv_draw_triangle_dsc_init(&d);
  d.bg_color = c; d.bg_opa = LV_OPA_COVER;
  d.p[0] = {(lv_value_precise_t)x1, (lv_value_precise_t)y1};
  d.p[1] = {(lv_value_precise_t)x2, (lv_value_precise_t)y2};
  d.p[2] = {(lv_value_precise_t)x3, (lv_value_precise_t)y3};
  lv_draw_triangle(L, &d);
}

static void paint(lv_event_t *e) {
  lv_layer_t *L = lv_event_get_layer(e);
  lv_color_t sky = night ? lv_color_hex(0x05070D) : lv_color_hex(0x0B1A2C);
  rr(L, 0, 0, 320, 240, 0, sky);
  int g = 196;                                               // ground line
  // stars (fixed pseudo random, a few twinkle) or sun
  if (night) {
    uint32_t r = 12345;
    for (int i = 0; i < 34; i++) {
      r = r * 1103515245 + 12345; int x = (r >> 8) % 320; r = r * 1103515245 + 12345; int y = 4 + (r >> 8) % 130;
      bool tw = (i + phase) % 7 == 0;
      int s = i % 5 == 0 ? 2 : 1;
      rr(L, x, y, s, s, 1, lv_color_hex(0xDDE6F5), tw ? LV_OPA_30 : (i % 3 ? LV_OPA_70 : LV_OPA_COVER));
    }
    int mx = 262 - dx / 2, my = 26;                          // crescent moon
    rr(L, mx - 6, my - 6, 46, 46, LV_RADIUS_CIRCLE, lv_color_hex(0xF2E6B8), LV_OPA_10);
    rr(L, mx, my, 34, 34, LV_RADIUS_CIRCLE, lv_color_hex(0xF2E6B8));
    rr(L, mx + 9, my - 4, 32, 32, LV_RADIUS_CIRCLE, sky);
  } else {
    int sx = 252 - dx / 2, sy = 22;
    rr(L, sx - 10, sy - 10, 58, 58, LV_RADIUS_CIRCLE, lv_color_hex(0xFFB000), LV_OPA_20);
    rr(L, sx, sy, 38, 38, LV_RADIUS_CIRCLE, lv_color_hex(0xFFC23A));
  }
  // mountains + hills
  lv_color_t m1 = night ? lv_color_hex(0x0E1522) : lv_color_hex(0x16283D), m2 = night ? lv_color_hex(0x121B2B) : lv_color_hex(0x1C3249);
  tri(L, -40, g, 70, 112, 180, g, m1);
  tri(L, 110, g, 220, 98, 340, g, m1);
  tri(L, 200, g, 300, 132, 380, g, m2);
  tri(L, -20, g, 30, 150, 100, g, m2);
  // pine trees
  lv_color_t tr = night ? lv_color_hex(0x0A1712) : lv_color_hex(0x123024);
  for (int k = 0; k < 4; k++) {
    int tx = (k < 2 ? 18 + k * 22 : 268 + (k - 2) * 24), th = 34 + (k % 2) * 10;
    tri(L, tx - 11, g, tx, g - th, tx + 11, g, tr);
    tri(L, tx - 9, g - th / 2, tx, g - th - 8, tx + 9, g - th / 2, tr);
  }
  rr(L, 0, g, 320, 240 - g, 0, night ? lv_color_hex(0x0A0F16) : lv_color_hex(0x10202A));
  rr(L, 0, g, 320, 2, 0, night ? lv_color_hex(0x1A2433) : lv_color_hex(0x24384A));

  // the camper: Fiat Ducato based Livingstone, side view facing right
  int x = 70 + dx, y = g - 8;                                // y = wheel centre line - a bit
  lv_color_t body = night ? lv_color_hex(0xB9C2CF) : lv_color_hex(0xE9EEF4);
  lv_color_t win = night ? lv_color_hex(0xFFB547) : lv_color_hex(0x223448);   // warm light at night
  lv_color_t glass = night ? lv_color_hex(0x1A2433) : lv_color_hex(0x2D4660);
  rr(L, x + 20, y + 10, 150, 4, 2, lv_color_hex(0x000000), LV_OPA_40);         // shadow
  rr(L, x + 34, y - 82, 70, 4, 1, lv_color_hex(0x1C3A63));                     // solar panel
  rr(L, x + 4, y - 80, 150, 70, 12, body);                                     // box body
  tri(L, x + 150, y - 80, x + 186, y - 46, x + 150, y - 46, body);             // sloped windscreen
  rr(L, x + 140, y - 50, 52, 40, 10, body);                                    // bonnet
  tri(L, x + 155, y - 76, x + 182, y - 50, x + 155, y - 50, glass);            // windscreen glass
  rr(L, x + 150, y - 72, 6, 22, 0, glass);
  rr(L, x + 124, y - 70, 24, 20, 4, glass);                                    // cab side window
  rr(L, x + 18, y - 68, 34, 20, 5, win);                                       // living windows
  rr(L, x + 64, y - 68, 26, 20, 5, win);
  if (night) { rr(L, x + 14, y - 72, 42, 28, 9, win, LV_OPA_20); rr(L, x + 60, y - 72, 34, 28, 9, win, LV_OPA_20); }
  rr(L, x + 98, y - 70, 20, 50, 4, lv_color_hex(0x8E99A8), LV_OPA_50);         // sliding door line
  rr(L, x + 4, y - 36, 188, 5, 2, lv_color_hex(0x2BB3A7));                     // teal stripe
  rr(L, x + 186, y - 30, 6, 6, 2, night ? lv_color_hex(0xFFE7A0) : lv_color_hex(0xC9D3DE));   // headlight
  for (int wx : {x + 40, x + 156}) {                                           // wheels
    rr(L, wx - 15, y - 22, 30, 30, LV_RADIUS_CIRCLE, lv_color_hex(0x161B22));
    rr(L, wx - 6, y - 13, 12, 12, LV_RADIUS_CIRCLE, lv_color_hex(0x8E99A8));
  }
}

void saver_build() {
  sv = lv_obj_create(lv_layer_sys());
  lv_obj_remove_style_all(sv);
  lv_obj_set_size(sv, 320, 240);
  lv_obj_add_flag(sv, LV_OBJ_FLAG_HIDDEN);
  scene = lv_obj_create(sv);
  lv_obj_remove_style_all(scene);
  lv_obj_set_size(scene, 320, 240);
  lv_obj_add_event_cb(scene, paint, LV_EVENT_DRAW_MAIN, nullptr);
  s_clock = label(sv, F40, lv_color_hex(0xDDE6F5), "");
  s_date = label(sv, F14, C_DIM, "");
  s_info = label(sv, F14, C_DIM, "");
}

static void place_text() {
  lv_obj_set_pos(s_clock, 14 + dx / 2, 10);
  lv_obj_align_to(s_date, s_clock, LV_ALIGN_OUT_BOTTOM_LEFT, 2, 0);
  lv_obj_align(s_info, LV_ALIGN_BOTTOM_MID, dx / 2, -10);
}

bool saver_on() { return sv && !lv_obj_has_flag(sv, LV_OBJ_FLAG_HIDDEN); }

void saver_text(char *clock, char *date, char *info) {     // also used by the photo screensaver
  static const char *WD[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  static const char *MO[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  time_t now = time(nullptr);
  clock[0] = date[0] = info[0] = 0;
  Lock l;
  struct tm t;
  if (now > 1700000000) {
    localtime_r(&now, &t);
    int mins = t.tm_hour * 60 + t.tm_min, rise = 7 * 60, set = 19 * 60;
    if (S.wx.ok && S.wx.nd) { rise = atoi(S.wx.d_rise[0]) * 60 + atoi(S.wx.d_rise[0] + 3); set = atoi(S.wx.d_set[0]) * 60 + atoi(S.wx.d_set[0] + 3); }
    night = mins < rise || mins >= set;
    snprintf(clock, 8, "%02d:%02d", t.tm_hour, t.tm_min);
    snprintf(date, 24, "%s %d %s", WD[t.tm_wday], t.tm_mday, MO[t.tm_mon]);
  }
  size_t k = 0, n = 96;
  const Victron &v = S.vic;
  if (v.decoded && millis() - v.at < 60000) k += snprintf(info + k, n - k, LV_SYMBOL_BATTERY_3 " %d%%    ", vic_soc(v));
  if (v.decoded && millis() - v.at < 60000 && v.pv_w != 0xFFFF && v.pv_w) k += snprintf(info + k, n - k, LV_SYMBOL_CHARGE " %d W    ", v.pv_w);
  const Sensor *o = sens_by_role(R_OUTSIDE);
  if (o && !isnan(o->t)) k += snprintf(info + k, n - k, "%.1f\xC2\xB0", o->t);
  else if (S.wx.ok) k += snprintf(info + k, n - k, "%.0f\xC2\xB0", S.wx.t);
}

void saver_tick() {                     // every 2 s while shown
  phase++;
  if (phase % 15 == 0) { static int dir = 1; dx += dir * 2; if (dx > 24 || dx < -24) dir = -dir; }   // drift 1 px/15 s
  char clk[8], date[24], info[96];
  saver_text(clk, date, info);
  set_txt(s_clock, clk); set_txt(s_date, date); set_txt(s_info, info);
  place_text();
  lv_obj_invalidate(scene);
}

void saver_show(bool on) {
  if (!sv || on == saver_on()) return;
  if (on) { lv_obj_remove_flag(sv, LV_OBJ_FLAG_HIDDEN); phase = 0; saver_tick(); }
  else lv_obj_add_flag(sv, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace ui
