// Small LVGL kit shared by the pages: colors, fonts, cards, labels that only redraw on change, pooled rows.
#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include "state.hpp"

namespace ui {
using namespace app;

// colours come from the active palette (night or day look), see set_look()
extern lv_color_t PAL[17];
bool set_look(bool day);   // true when it changed
bool look_day();
#define C_BG ui::PAL[0]
#define C_BAR ui::PAL[1]
#define C_CARD ui::PAL[2]
#define C_CARD2 ui::PAL[3]
#define C_LINE ui::PAL[4]
#define C_TEAL ui::PAL[5]
#define C_BLUE ui::PAL[6]
#define C_SKY ui::PAL[7]
#define C_AMBER ui::PAL[8]
#define C_ORANGE ui::PAL[9]
#define C_PURPLE ui::PAL[10]
#define C_WHITE ui::PAL[11]
#define C_DIM ui::PAL[12]
#define C_FAINT ui::PAL[13]
#define C_GREEN ui::PAL[14]
#define C_RED ui::PAL[15]
#define C_CLOUD ui::PAL[16]

extern const lv_font_t *F12, *F14, *F16, *F20, *F24, *F28, *F40;
constexpr int W = 320, TOP = 22, TABS = 38, PAGE_H = 240 - TOP - TABS;   // 180 px per page

lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *txt = "");
lv_obj_t *box(lv_obj_t *p, int w, int h, lv_color_t bg);
lv_obj_t *card(lv_obj_t *p, int x, int y, int w, int h);
lv_obj_t *button(lv_obj_t *p, const char *txt, int w, int h, lv_color_t bg, lv_event_cb_t cb, void *ud = nullptr);
lv_obj_t *modal(const char *title);
void modal_owner(lv_obj_t **p, lv_obj_t *m);   // *p = m, set back to nullptr when the modal is deleted
lv_obj_t *page_scroll(lv_obj_t *p, int y, int h);
void set_txt(lv_obj_t *l, const char *s);
void set_fmt(lv_obj_t *l, const char *fmt, ...);
void set_col(lv_obj_t *l, lv_color_t c);
void set_bg(lv_obj_t *o, lv_color_t c);
void show(lv_obj_t *o, bool on);
void toast(const char *msg);
void bar_fill(lv_obj_t *track, lv_obj_t *fill, int pct, lv_color_t c);   // pct 0..100
lv_obj_t *bar_make(lv_obj_t *p, int x, int y, int w, int h, lv_obj_t **fill);

// segmented control: n buttons, the active one lit
struct Seg { std::vector<lv_obj_t *> b; int active = 0; };
void seg_make(Seg &s, lv_obj_t *p, int x, int y, int w, int h, const char *const *names, int n, lv_event_cb_t cb);
void seg_set(Seg &s, int i);

// pooled rows (created once, updated in place)
struct Row { lv_obj_t *row, *dot, *title, *sub, *val, *val2, *icon; };
struct Pool {
  lv_obj_t *list = nullptr; std::vector<Row> rows; lv_obj_t *empty = nullptr;
  int h = 30; bool dot = false, icon = false; bool big_val = false; lv_event_cb_t cb = nullptr, long_cb = nullptr;
};
Row &pool_row(Pool &p, int i);
void pool_trim(Pool &p, int n, const char *empty_text);

// text entry modal with keyboard
typedef void (*text_cb)(const char *);
void open_text(const char *title, const char *placeholder, int max_len, const char *accepted, text_cb cb, bool upper = false);
void close_modal();
extern lv_obj_t *g_modal;

// weather icon drawn from shapes (sun, cloud, rain, snow, fog, storm), rebuilt only when the code changes
void wx_icon(lv_obj_t *holder, int code, int size);
const char *wx_text(int code);

// drawn list: ONE object paints all rows (no objects per row -> fits any length in RAM)
typedef void (*row_fn)(lv_layer_t *L, const lv_area_t &r, int i);
lv_obj_t *drawlist(lv_obj_t *p, int x, int y, int w, int h, int row_h, row_fn fn);
void drawlist_count(lv_obj_t *dl, int n);       // set rows + redraw
void drawlist_redraw(lv_obj_t *dl);
typedef void (*tap_fn)(int row);
void drawlist_on_tap(lv_obj_t *dl, tap_fn fn);
void drawlist_on_hold(lv_obj_t *dl, tap_fn fn);   // long press, set after drawlist_on_tap
void dl_text(lv_layer_t *L, const lv_area_t &r, int x, int y, const lv_font_t *f, lv_color_t c, const char *t, bool right = false);
void dl_dot(lv_layer_t *L, const lv_area_t &r, int x, int y, int w, int h, lv_color_t c);
void dl_icon(lv_layer_t *L, int x, int y, int code, int size);

// pages
void home_build(lv_obj_t *tab); void home_update();
void power_build(lv_obj_t *tab); void power_update(bool force);
void climate_build(lv_obj_t *tab); void climate_update(bool force);
void level_build(lv_obj_t *tab); void level_update();
void open_place(); void place_picker_update();
void bank_build(lv_obj_t *tab); void bank_page_update(bool force);
void goto_tab(int i);
void tools_build(lv_obj_t *tab); void tools_update(bool force);
void power_open_key();
void power_view_test(int i);
void bank_settings_test(int tap);
void wifi_picker();
void web_drain(); void sd_modal_update(); void tools_leave();
void look_check(); void look_request();   // request = from an event handler: applied on the next tick
}  // namespace ui
