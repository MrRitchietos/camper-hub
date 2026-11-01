// LVGL 9.2 config for the CYD (320x240, RGB565). Unset options use LVGL defaults.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// the screen gets a reserved 40 KB pool and borrows from the heap only when that is full
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CUSTOM   // own pool + heap overflow: src/lv_mem_camper.cpp
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 16       // ~60 fps redraw / input poll
#define LV_DPI_DEF 130

// (LV_ATTRIBUTE_FAST_MEM IRAM_ATTR tried: IRAM overflows with wifi + BT)
#define LV_USE_OS LV_OS_NONE        // LVGL only touched from the UI loop
#define LV_USE_LOG 0
// layers (opacity, transforms) are drawn in small pieces: 8 KB instead of 24 KB peak at once
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE (8 * 1024)
// faster style lookups for a few bytes per object
#define LV_OBJ_STYLE_CACHE 1
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 60

// widgets not needed
#define LV_USE_CALENDAR 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_MENU 0
#define LV_USE_ANIMIMG 0
#define LV_USE_LED 0
#define LV_USE_SCALE 0
#define LV_USE_ARC 1
#define LV_USE_SPINNER 1
#define LV_USE_LOTTIE 0

#endif
