#include "ui_kit.hpp"

namespace ui {

// ---------------------------------------------------------------- night and day look
static const uint32_t NIGHT[17] = {0x0A0F1A, 0x0F1624, 0x172133, 0x223049, 0x2C3B58, 0x2BD9A8, 0x5AA8FF, 0x8CCBFF, 0xFFC54A, 0xFF8B45, 0xB085F5, 0xEEF3FA, 0x7F91AE, 0x4A5A78, 0x42DD7E, 0xF0524F, 0xD9E2EE};
static const uint32_t DAY[17] = {0xE6ECF3, 0xFFFFFF, 0xFFFFFF, 0xD5DEEA, 0xBFCADA, 0x0A9471, 0x1B66C9, 0x2479B8, 0xB87400, 0xCF5A12, 0x7448C2, 0x0E1626, 0x4E5D75, 0x8E9BB0, 0x178F49, 0xD2312E, 0x8D9BB0};   // darker accents: readable in sunlight on a light background
lv_color_t PAL[17];
static int8_t is_day = -1;
bool look_day() { return is_day == 1; }
bool set_look(bool day) {
  if (is_day == (int8_t)day) return false;
  is_day = day;
  for (int i = 0; i < 17; i++) PAL[i] = lv_color_hex(day ? DAY[i] : NIGHT[i]);
  lv_display_t *d = lv_display_get_default();
  if (d) lv_display_set_theme(d, lv_theme_default_init(d, PAL[5], PAL[6], !day, LV_FONT_DEFAULT));
  return true;
}

const lv_font_t *F12 = &lv_font_montserrat_12, *F14 = &lv_font_montserrat_14, *F16 = &lv_font_montserrat_16,
                *F20 = &lv_font_montserrat_20, *F24 = &lv_font_montserrat_24, *F28 = &lv_font_montserrat_28,
                *F40 = &lv_font_montserrat_40;
lv_obj_t *g_modal = nullptr;

lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *txt) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_label_set_text(l, txt);
  return l;
}
lv_obj_t *box(lv_obj_t *p, int w, int h, lv_color_t bg) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, bg, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}
lv_obj_t *card(lv_obj_t *p, int x, int y, int w, int h) {
  lv_obj_t *c = box(p, w, h, C_CARD);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_style_radius(c, 10, 0);
  return c;
}
lv_obj_t *button(lv_obj_t *p, const char *txt, int w, int h, lv_color_t bg, lv_event_cb_t cb, void *ud) {
  lv_obj_t *b = lv_button_create(p);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_t *l = label(b, F14, C_WHITE, txt);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}
lv_obj_t *modal(const char *title) {
  close_modal();
  lv_obj_t *m = box(lv_layer_top(), 320, 240, C_BG);
  lv_obj_add_flag(m, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *h = box(m, 320, 30, C_BAR);
  lv_obj_t *line = box(h, 320, 2, C_TEAL);
  lv_obj_align(line, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_t *t = label(h, F16, C_WHITE, title);
  lv_obj_set_width(t, 260);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT); lv_obj_set_height(t, lv_font_get_line_height(lv_obj_get_style_text_font(t, 0)));
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 10, -1);
  lv_obj_t *x = button(h, LV_SYMBOL_CLOSE, 34, 24, C_CARD2, [](lv_event_t *) { close_modal(); });
  lv_obj_align(x, LV_ALIGN_RIGHT_MID, -4, -1);
  g_modal = m;
  return m;
}
void modal_owner(lv_obj_t **p, lv_obj_t *m) {
  *p = m;
  lv_obj_add_event_cb(m, [](lv_event_t *e) { *(lv_obj_t **)lv_event_get_user_data(e) = nullptr; }, LV_EVENT_DELETE, p);
}
void close_modal() { if (g_modal) { lv_obj_delete(g_modal); g_modal = nullptr; } }
lv_obj_t *page_scroll(lv_obj_t *p, int y, int h) {
  lv_obj_t *l = lv_obj_create(p);
  lv_obj_remove_style_all(l);
  lv_obj_set_size(l, 320, h);
  lv_obj_set_pos(l, 0, y);
  lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(l, 4, 0);
  lv_obj_set_style_pad_ver(l, 2, 0);
  lv_obj_set_style_pad_hor(l, 4, 0);
  lv_obj_set_scroll_dir(l, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_ACTIVE);
  return l;
}
void set_txt(lv_obj_t *l, const char *s) { if (strcmp(lv_label_get_text(l), s)) lv_label_set_text(l, s); }
void set_fmt(lv_obj_t *l, const char *fmt, ...) {
  char b[128]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); set_txt(l, b);
}
void set_col(lv_obj_t *l, lv_color_t c) { if (!lv_color_eq(lv_obj_get_style_text_color(l, 0), c)) lv_obj_set_style_text_color(l, c, 0); }
void set_bg(lv_obj_t *o, lv_color_t c) { if (!lv_color_eq(lv_obj_get_style_bg_color(o, 0), c)) lv_obj_set_style_bg_color(o, c, 0); }
void show(lv_obj_t *o, bool on) {
  if (on == lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) { if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }
}
void toast(const char *msg) {
  lv_obj_t *l = label(lv_layer_top(), F14, C_WHITE, msg);
  lv_obj_set_style_bg_color(l, C_CARD2, 0);
  lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(l, C_TEAL, 0);
  lv_obj_set_style_border_width(l, 1, 0);
  lv_obj_set_style_pad_hor(l, 14, 0);
  lv_obj_set_style_pad_ver(l, 8, 0);
  lv_obj_set_style_radius(l, 16, 0);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 30);
  lv_obj_delete_delayed(l, 2200);
}
lv_obj_t *bar_make(lv_obj_t *p, int x, int y, int w, int h, lv_obj_t **fill) {
  lv_obj_t *t = box(p, w, h, C_CARD2);
  lv_obj_set_pos(t, x, y);
  lv_obj_set_style_radius(t, h / 2, 0);
  *fill = box(t, 0, h, C_GREEN);
  lv_obj_set_style_radius(*fill, h / 2, 0);
  return t;
}
void bar_fill(lv_obj_t *track, lv_obj_t *fill, int pct, lv_color_t c) {
  int w = pct <= 0 ? 0 : max((int)lv_obj_get_style_height(track, 0), (int)(lv_obj_get_style_width(track, 0) * min(pct, 100) / 100));
  if (lv_obj_get_style_width(fill, 0) != w) lv_obj_set_width(fill, w);
  set_bg(fill, c);
}

void seg_make(Seg &s, lv_obj_t *p, int x, int y, int w, int h, const char *const *names, int n, lv_event_cb_t cb) {
  lv_obj_t *bg = box(p, w, h, C_CARD);
  lv_obj_set_pos(bg, x, y);
  lv_obj_set_style_radius(bg, h / 2, 0);
  int bw = (w - 4) / n;
  for (int i = 0; i < n; i++) {
    lv_obj_t *b = button(bg, names[i], bw, h - 4, C_CARD, cb, (void *)(intptr_t)i);
    lv_obj_set_style_radius(b, (h - 4) / 2, 0);
    lv_obj_set_pos(b, 2 + i * bw, 2);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), F14, 0);
    s.b.push_back(b);
  }
  seg_set(s, 0);
}
void seg_set(Seg &s, int i) {
  s.active = i;
  for (int k = 0; k < (int)s.b.size(); k++) {
    set_bg(s.b[k], k == i ? C_CARD2 : C_CARD);
    set_col(lv_obj_get_child(s.b[k], 0), k == i ? C_TEAL : C_DIM);
  }
}

Row &pool_row(Pool &p, int i) {
  while ((int)p.rows.size() <= i) {
    Row r{};
    r.row = box(p.list, 312, p.h, C_CARD);
    lv_obj_set_style_radius(r.row, 8, 0);
    lv_obj_set_style_bg_color(r.row, C_CARD2, LV_STATE_PRESSED);
    int x = 10;
    if (p.dot) {
      r.dot = box(r.row, 6, p.h - 14, C_DIM);
      lv_obj_set_style_radius(r.dot, 3, 0);
      lv_obj_align(r.dot, LV_ALIGN_LEFT_MID, 7, 0);
      x = 20;
    }
    if (p.icon) {
      r.icon = lv_obj_create(r.row);
      lv_obj_remove_style_all(r.icon);
      lv_obj_set_pos(r.icon, x - 4, (p.h - 26) / 2);
      x += 28;
    }
    bool two = p.h >= 34;
    r.title = label(r.row, F14, C_WHITE, "");
    lv_obj_set_width(r.title, 190);
    lv_label_set_long_mode(r.title, LV_LABEL_LONG_DOT); lv_obj_set_height(r.title, lv_font_get_line_height(lv_obj_get_style_text_font(r.title, 0)));
    lv_obj_set_pos(r.title, x, two ? 3 : (p.h - 17) / 2);
    if (two) {
      r.sub = label(r.row, F12, C_DIM, "");
      lv_obj_set_width(r.sub, 210);
      lv_label_set_long_mode(r.sub, LV_LABEL_LONG_DOT); lv_obj_set_height(r.sub, lv_font_get_line_height(lv_obj_get_style_text_font(r.sub, 0)));
      lv_obj_set_pos(r.sub, x, 21);
    }
    r.val = label(r.row, p.big_val ? F24 : F14, C_WHITE, "");
    lv_obj_align(r.val, LV_ALIGN_RIGHT_MID, -10, p.big_val ? -4 : 0);
    if (p.big_val) {
      r.val2 = label(r.row, F12, C_DIM, "");
      lv_obj_align(r.val2, LV_ALIGN_BOTTOM_RIGHT, -10, -2);
    }
    if (p.cb || p.long_cb) {
      lv_obj_add_flag(r.row, LV_OBJ_FLAG_CLICKABLE);
      if (p.cb) lv_obj_add_event_cb(r.row, p.cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)p.rows.size());
      if (p.long_cb) lv_obj_add_event_cb(r.row, p.long_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)p.rows.size());
    }
    p.rows.push_back(r);
  }
  Row &r = p.rows[i];
  show(r.row, true);
  return r;
}
void pool_trim(Pool &p, int n, const char *empty_text) {
  for (int i = n; i < (int)p.rows.size(); i++) show(p.rows[i].row, false);
  if (!p.empty) {
    p.empty = label(p.list, F14, C_DIM, "");
    lv_obj_set_style_pad_all(p.empty, 8, 0);
    lv_obj_set_width(p.empty, 310);
    lv_label_set_long_mode(p.empty, LV_LABEL_LONG_WRAP);
  }
  show(p.empty, n == 0 && empty_text && *empty_text);
  if (n == 0 && empty_text) set_txt(p.empty, empty_text);
}

// ---- text entry
static lv_obj_t *t_ta;
static text_cb t_cb;
static void t_kb(lv_event_t *e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_CANCEL) { close_modal(); return; }
  if (c != LV_EVENT_READY) return;
  String s = lv_textarea_get_text(t_ta);
  text_cb cb = t_cb;
  close_modal();
  if (cb) cb(s.c_str());
}
void open_text(const char *title, const char *placeholder, int max_len, const char *accepted, text_cb cb, bool upper) {
  lv_obj_t *m = modal(title);
  t_cb = cb;
  t_ta = lv_textarea_create(m);
  lv_textarea_set_one_line(t_ta, true);
  lv_textarea_set_placeholder_text(t_ta, placeholder);
  lv_textarea_set_max_length(t_ta, max_len);
  if (accepted) lv_textarea_set_accepted_chars(t_ta, accepted);
  lv_obj_set_style_text_font(t_ta, F14, 0);
  lv_obj_set_size(t_ta, 304, 36);
  lv_obj_set_pos(t_ta, 8, 34);
  lv_obj_t *kb = lv_keyboard_create(m);
  if (upper) lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER);
  lv_obj_set_size(kb, 320, 166);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, t_ta);
  lv_obj_add_event_cb(kb, t_kb, LV_EVENT_ALL, nullptr);
}

// ---- weather icons
enum WxKind { WK_SUN, WK_PART, WK_CLOUD, WK_FOG, WK_RAIN, WK_SNOW, WK_STORM };
static WxKind kind(int c) {
  if (c <= 1) return WK_SUN;
  if (c == 2) return WK_PART;
  if (c == 3) return WK_CLOUD;
  if (c == 45 || c == 48) return WK_FOG;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WK_SNOW;
  if (c >= 95) return WK_STORM;
  return WK_RAIN;
}
const char *wx_text(int c) {
  if (c == 0) return "Clear"; if (c == 1) return "Mostly clear"; if (c == 2) return "Partly cloudy"; if (c == 3) return "Cloudy";
  if (c == 45 || c == 48) return "Fog"; if (c >= 51 && c <= 55) return "Drizzle"; if (c == 56 || c == 57) return "Freezing drizzle";
  if (c >= 61 && c <= 65) return "Rain"; if (c == 66 || c == 67) return "Freezing rain"; if (c >= 71 && c <= 77) return "Snow";
  if (c >= 80 && c <= 82) return "Showers"; if (c == 85 || c == 86) return "Snow showers"; if (c >= 95) return "Thunderstorm";
  return "?";
}
// one object per icon: it paints its shapes itself (no child objects, ~1/8 of the RAM)
static void circ(lv_layer_t *L, const lv_area_t &o, float x, float y, float d, lv_color_t c, lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_rect_dsc_t r; lv_draw_rect_dsc_init(&r);
  r.bg_color = c; r.bg_opa = opa; r.radius = LV_RADIUS_CIRCLE;
  lv_area_t a = {(int32_t)(o.x1 + x), (int32_t)(o.y1 + y), (int32_t)(o.x1 + x + d - 1), (int32_t)(o.y1 + y + d - 1)};
  lv_draw_rect(L, &r, &a);
}
static void rrect(lv_layer_t *L, const lv_area_t &o, float x, float y, float w, float h, float rad, lv_color_t c) {
  lv_draw_rect_dsc_t r; lv_draw_rect_dsc_init(&r);
  r.bg_color = c; r.bg_opa = LV_OPA_COVER; r.radius = rad;
  lv_area_t a = {(int32_t)(o.x1 + x), (int32_t)(o.y1 + y), (int32_t)(o.x1 + x + w - 1), (int32_t)(o.y1 + y + h - 1)};
  lv_draw_rect(L, &r, &a);
}
static void cloud(lv_layer_t *L, const lv_area_t &o, float s, float oy, lv_color_t c) {
  circ(L, o, s * 0.10f, oy + s * 0.22f, s * 0.36f, c);
  circ(L, o, s * 0.32f, oy + s * 0.05f, s * 0.46f, c);
  circ(L, o, s * 0.58f, oy + s * 0.20f, s * 0.34f, c);
  rrect(L, o, s * 0.10f, oy + s * 0.34f, s * 0.78f, s * 0.24f, s * 0.12f, c);
}
static void icon_paint(lv_layer_t *L, const lv_area_t &o, int code, int s);
static void icon_draw(lv_event_t *e) {
  lv_obj_t *h = (lv_obj_t *)lv_event_get_target(e);
  intptr_t key = (intptr_t)lv_obj_get_user_data(h);
  if (key <= 0) return;
  lv_area_t o; lv_obj_get_coords(h, &o);
  icon_paint(lv_event_get_layer(e), o, key / 1000 - 1, key % 1000);
}
void dl_icon(lv_layer_t *L, int x, int y, int code, int s) { lv_area_t o = {x, y, x + s - 1, y + s - 1}; icon_paint(L, o, code, s); }
static void icon_paint(lv_layer_t *L, const lv_area_t &o, int code, int s) {
  WxKind k = kind(code);
  if (k == WK_SUN) { circ(L, o, s * 0.12f, s * 0.12f, s * 0.76f, C_AMBER, LV_OPA_30); circ(L, o, s * 0.22f, s * 0.22f, s * 0.56f, C_AMBER); return; }
  if (k == WK_PART) circ(L, o, s * 0.40f, s * 0.04f, s * 0.48f, C_AMBER);
  bool dark = k == WK_RAIN || k == WK_STORM || k == WK_SNOW;
  cloud(L, o, s, k == WK_PART ? s * 0.22f : s * 0.12f, dark ? lv_color_hex(0x9AA8BD) : C_CLOUD);
  float t = max(2.0f, s / 14.0f);
  if (k == WK_FOG) for (int i = 0; i < 2; i++) rrect(L, o, s * 0.15f, s * (0.78f + i * 0.12f), s * 0.7f, t, 2, C_DIM);
  if (k == WK_RAIN) for (int i = 0; i < 3; i++) rrect(L, o, s * (0.26f + i * 0.2f), s * 0.76f, t, s * 0.2f, 2, C_BLUE);
  if (k == WK_SNOW) for (int i = 0; i < 3; i++) circ(L, o, s * (0.24f + i * 0.2f), s * 0.8f, max(3, s / 8), C_WHITE);
  if (k == WK_STORM) {
    lv_point_precise_t p[] = {{s * 0.56f, s * 0.60f}, {s * 0.42f, s * 0.80f}, {s * 0.54f, s * 0.80f}, {s * 0.44f, s * 0.99f}};
    lv_draw_line_dsc_t ld; lv_draw_line_dsc_init(&ld);
    ld.color = C_AMBER; ld.width = max(2, s / 12); ld.round_start = ld.round_end = 1;
    for (int i = 0; i < 3; i++) { ld.p1 = {o.x1 + p[i].x, o.y1 + p[i].y}; ld.p2 = {o.x1 + p[i + 1].x, o.y1 + p[i + 1].y}; lv_draw_line(L, &ld); }
  }
}
// ---- drawn list
struct DL { row_fn fn; int row_h, n; lv_obj_t *inner; tap_fn tap; int pressed; tap_fn hold; };
static void dl_draw(lv_event_t *e) {
  lv_obj_t *in = (lv_obj_t *)lv_event_get_target(e);
  DL *d = (DL *)lv_obj_get_user_data(lv_obj_get_parent(in));
  lv_layer_t *L = lv_event_get_layer(e);
  lv_area_t a; lv_obj_get_coords(in, &a);
  lv_area_t clip; lv_obj_get_coords(lv_obj_get_parent(in), &clip);
  for (int i = 0; i < d->n; i++) {
    lv_area_t r = {a.x1, a.y1 + i * d->row_h, a.x2, a.y1 + (i + 1) * d->row_h - 4};
    if (r.y2 < clip.y1 || r.y1 > clip.y2) continue;            // off screen: skip
    lv_draw_rect_dsc_t bg; lv_draw_rect_dsc_init(&bg);
    bg.bg_color = i == d->pressed ? C_CARD2 : C_CARD; bg.radius = 8;
    lv_draw_rect(L, &bg, &r);
    d->fn(L, r, i);
  }
}
static void dl_free(lv_event_t *e) { delete (DL *)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e)); }
lv_obj_t *drawlist(lv_obj_t *p, int x, int y, int w, int h, int row_h, row_fn fn) {
  lv_obj_t *c = lv_obj_create(p);
  lv_obj_remove_style_all(c);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, w, h);
  lv_obj_set_scroll_dir(c, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_ACTIVE);
  DL *d = new DL{fn, row_h, 0, nullptr, nullptr, -1, nullptr};
  lv_obj_set_user_data(c, d);
  lv_obj_add_event_cb(c, dl_free, LV_EVENT_DELETE, nullptr);
  d->inner = lv_obj_create(c);
  lv_obj_remove_style_all(d->inner);
  lv_obj_set_size(d->inner, w, 1);
  lv_obj_remove_flag(d->inner, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(d->inner, dl_draw, LV_EVENT_DRAW_MAIN, nullptr);
  return c;
}
void drawlist_count(lv_obj_t *c, int n) {
  DL *d = (DL *)lv_obj_get_user_data(c);
  if (d->n != n) { d->n = n; lv_obj_set_height(d->inner, max(1, n * d->row_h)); }
  lv_obj_invalidate(d->inner);
}
static int dl_row_at(lv_obj_t *c) {
  DL *d = (DL *)lv_obj_get_user_data(c);
  lv_point_t p; lv_indev_get_point(lv_indev_active(), &p);
  lv_area_t a; lv_obj_get_coords(d->inner, &a);
  int i = (p.y - a.y1) / d->row_h;
  return i >= 0 && i < d->n ? i : -1;
}
static void dl_input(lv_event_t *e) {
  lv_obj_t *c = (lv_obj_t *)lv_event_get_current_target(e);
  DL *d = (DL *)lv_obj_get_user_data(c);
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) { d->pressed = dl_row_at(c); lv_obj_invalidate(d->inner); }
  else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_SCROLL_BEGIN) { d->pressed = -1; lv_obj_invalidate(d->inner); }
  else if (code == LV_EVENT_LONG_PRESSED) {
    int i = dl_row_at(c);
    d->pressed = -1; lv_obj_invalidate(d->inner);
    if (i >= 0 && d->hold) d->hold(i);
  }
  else if (code == LV_EVENT_SHORT_CLICKED) {
    int i = dl_row_at(c);
    d->pressed = -1; lv_obj_invalidate(d->inner);
    if (i >= 0 && i == dl_row_at(c) && d->tap) d->tap(i);
  }
}
void drawlist_on_tap(lv_obj_t *c, tap_fn fn) {
  DL *d = (DL *)lv_obj_get_user_data(c);
  d->tap = fn;
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(c, dl_input, LV_EVENT_ALL, nullptr);
}
void drawlist_on_hold(lv_obj_t *c, tap_fn fn) { ((DL *)lv_obj_get_user_data(c))->hold = fn; }   // after drawlist_on_tap
void drawlist_redraw(lv_obj_t *c) { lv_obj_invalidate(((DL *)lv_obj_get_user_data(c))->inner); }
void dl_text(lv_layer_t *L, const lv_area_t &r, int x, int y, const lv_font_t *f, lv_color_t c, const char *t, bool right) {
  lv_draw_label_dsc_t d; lv_draw_label_dsc_init(&d);
  d.font = f; d.color = c; d.text = t; d.text_local = 1;
  if (right) d.align = LV_TEXT_ALIGN_RIGHT;
  lv_area_t a = right ? lv_area_t{r.x1, r.y1 + y, r.x2 - x, r.y1 + y + lv_font_get_line_height(f)}
                      : lv_area_t{r.x1 + x, r.y1 + y, r.x2 - 4, r.y1 + y + lv_font_get_line_height(f)};
  lv_draw_label(L, &d, &a);
}
void dl_dot(lv_layer_t *L, const lv_area_t &r, int x, int y, int w, int h, lv_color_t c) {
  lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
  d.bg_color = c; d.radius = min(w, h) / 2;
  lv_area_t a = {r.x1 + x, r.y1 + y, r.x1 + x + w - 1, r.y1 + y + h - 1};
  lv_draw_rect(L, &d, &a);
}

void wx_icon(lv_obj_t *h, int code, int s) {
  intptr_t key = (code + 1) * 1000 + s;
  intptr_t old = (intptr_t)lv_obj_get_user_data(h);
  if (old == key) return;
  if (old == 0) lv_obj_add_event_cb(h, icon_draw, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_set_user_data(h, (void *)key);
  lv_obj_set_size(h, s, s);
  lv_obj_invalidate(h);
}

}  // namespace ui
