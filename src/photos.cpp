// Photo screensaver: JPG photos from /camper/photos on the SD card, drawn straight to the panel (LVGL keeps
// running but its flush is skipped while a photo shows). Any size from a phone works: the decoder scales down
// on the fly, portrait photos are turned upright (EXIF orientation), and every photo is cached the first time as
// a ready 320x240 frame in /camper/photos/.cache so the next showing is instant.
#include <Arduino.h>
#include <SD.h>
#include "hw.hpp"
#include "internal.hpp"

LGFX &app_tft();
namespace ui { void saver_text(char *clock, char *date, char *info); }

namespace app {

static const char *DIR = "/camper/photos", *CACHE = "/camper/photos/.cache";
static int count = -1, cur = -1;
static uint32_t shown_at = 0, clock_at = 0;
static int box_w = 0;
static constexpr uint32_t EVERY = 45000;

static bool is_jpg(const char *n) {
  const char *b = strrchr(n, '/'); b = b ? b + 1 : n;
  if (b[0] == '.') return false;
  const char *e = strrchr(b, '.');
  return e && (!strcasecmp(e, ".jpg") || !strcasecmp(e, ".jpeg"));
}
int photos_count() {
  bool sd; { Lock l; sd = S.sd_ok; }
  if (!sd) return 0;
  File d = SD.open(DIR);
  if (!d || !d.isDirectory()) { SD.mkdir(DIR); return 0; }
  int n = 0;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) { if (!f.isDirectory() && is_jpg(f.name())) n++; f.close(); }
  d.close();
  return n;
}
static bool nth(int k, char *path, size_t n, uint32_t &size) {
  File d = SD.open(DIR);
  if (!d) return false;
  int i = 0; bool ok = false;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    if (!f.isDirectory() && is_jpg(f.name())) {
      if (i++ == k) { snprintf(path, n, "%s/%s", DIR, strrchr(f.name(), '/') ? strrchr(f.name(), '/') + 1 : f.name()); size = f.size(); ok = true; f.close(); break; }
    }
    f.close();
  }
  d.close();
  return ok;
}

// JPEG header: size, progressive (not supported by the decoder), EXIF orientation
struct JInfo { int w = 0, h = 0; bool prog = false; int orient = 1; };
static bool jpeg_info(const char *path, JInfo &j) {
  File f = SD.open(path);
  if (!f) return false;
  auto rd = [&]() { return f.read(); };
  auto u16 = [&]() { int a = rd(), b = rd(); return a << 8 | b; };
  if (rd() != 0xFF || rd() != 0xD8) { f.close(); return false; }
  bool ok = false;
  for (int guard = 0; guard < 64 && f.available(); guard++) {
    int c = rd(); if (c != 0xFF) continue;
    int m = rd(); while (m == 0xFF) m = rd();
    if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) continue;
    int len = u16(); if (len < 2) break;
    uint32_t start = f.position();
    if (m == 0xE1 && len > 16) {                                  // APP1 Exif: orientation tag 0x0112
      uint8_t b[512]; int n = f.read(b, min(len - 2, (int)sizeof b));
      if (n > 14 && !memcmp(b, "Exif\0\0", 6)) {
        const uint8_t *t = b + 6; bool le = t[0] == 'I';
        auto g16 = [&](int o) { return o >= 0 && o + 1 < n - 6 ? (le ? t[o] | t[o + 1] << 8 : t[o] << 8 | t[o + 1]) : 0; };
        auto g32 = [&](int o) { return (uint32_t)g16(le ? o : o + 2) | (uint32_t)g16(le ? o + 2 : o) << 16; };
        int ifd = g32(4), cnt = g16(ifd);
        for (int i = 0; i < cnt && i < 40; i++) { int e = ifd + 2 + i * 12; if (g16(e) == 0x0112) { j.orient = g16(e + 8); break; } }
      }
    } else if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
      rd(); j.h = u16(); j.w = u16(); j.prog = m == 0xC2; ok = true; break;
    }
    f.seek(start + len - 2);
  }
  f.close();
  return ok && j.w > 0 && j.h > 0;
}

static uint32_t name_hash(const char *s, uint32_t size) { uint32_t h = 2166136261u ^ size; while (*s) h = (h ^ (uint8_t)*s++) * 16777619u; return h; }

static bool draw_cached(const char *cpath) {
  File c = SD.open(cpath);
  if (!c || c.size() != 320 * 240 * 2) { if (c) c.close(); return false; }
  LGFX &t = app_tft();
  static uint16_t line[320 * 4];
  t.startWrite();
  for (int y = 0; y < 240; y += 4) { c.read((uint8_t *)line, sizeof line); t.pushImage(0, y, 320, 4, (lgfx::swap565_t *)line); }
  t.endWrite();
  c.close();
  return true;
}
static void save_cache(const char *cpath) {
  SD.mkdir(CACHE);
  File c = SD.open(cpath, FILE_WRITE);
  if (!c) return;
  LGFX &t = app_tft();
  static uint16_t line[320 * 4];
  for (int y = 0; y < 240; y += 4) { t.readRect(0, y, 320, 4, (lgfx::swap565_t *)line); c.write((uint8_t *)line, sizeof line); }
  c.close();
}

void photos_drop_cache(const char *path) {            // a photo is deleted: its ready frame goes too
  File f = SD.open(path);
  if (!f) return;
  uint32_t size = f.size(); f.close();
  char cpath[48]; snprintf(cpath, sizeof cpath, "%s/%08x.raw", CACHE, (unsigned)name_hash(path, size));
  SD.remove(cpath);
}

static bool show(int k) {
  char path[160]; uint32_t size;
  if (!nth(k, path, sizeof path, size)) return false;
  char cpath[48]; snprintf(cpath, sizeof cpath, "%s/%08x.raw", CACHE, (unsigned)name_hash(path, size));
  if (draw_cached(cpath)) return true;
  JInfo j;
  if (!jpeg_info(path, j) || j.prog) { Serial.printf("photo skipped (%s): %s\n", j.prog ? "progressive" : "not a jpeg", path); return false; }
  LGFX &t = app_tft();
  uint32_t t0 = millis();
  t.fillScreen(0);
  bool turn = j.orient == 6 || j.orient == 8;                    // stored sideways: draw in a turned frame
  if (turn) t.setRotation(j.orient == 6 ? 1 : 3);
  int W = t.width(), Hh = t.height();
  // landscape photo on the landscape screen: fill it (crop the edges); portrait: fit (black sides)
  float z = turn ? min((float)W / j.w, (float)Hh / j.h) : max((float)W / j.w, (float)Hh / j.h);
  int x = (W - (int)(j.w * z)) / 2, y = (Hh - (int)(j.h * z)) / 2;    // centred; negative = cropped edges
  t.setClipRect(0, 0, W, Hh);
  bool ok = t.drawJpgFile(SD, path, x, y, 0, 0, 0, 0, z, z);
  t.clearClipRect();
  if (turn) t.setRotation(0);
  Serial.printf("photo %s %dx%d o%d z%.3f %s %u ms\n", path, j.w, j.h, j.orient, z, ok ? "ok" : "FAIL", (unsigned)(millis() - t0));
  if (!ok) return false;
  save_cache(cpath);
  return true;
}

static void overlay() {
  char clk[8], date[24], info[96], line[128];
  ui::saver_text(clk, date, info);                // date + clock (the info line has LVGL symbols: own text here)
  {
    Lock l; size_t k = 0, n = sizeof info; info[0] = 0;
    const Victron &v = S.vic;
    bool vk = v.decoded && millis() - v.at < 60000;
    if (vk && vic_soc(v) >= 0) k += snprintf(info + k, n - k, "Battery %d%%   ", vic_soc(v));
    if (vk && v.pv_w != 0xFFFF && v.pv_w) k += snprintf(info + k, n - k, "Solar %d W   ", v.pv_w);
    const Sensor *o = sens_by_role(R_OUTSIDE);
    if (o && !isnan(o->t)) k += snprintf(info + k, n - k, "Outside %.1f C", o->t);
    else if (S.wx.ok) k += snprintf(info + k, n - k, "Outside %.0f C", S.wx.t);
  }
  snprintf(line, sizeof line, "%s%s%s", date, date[0] && info[0] ? "   " : "", info);
  LGFX &t = app_tft();
  t.startWrite();
  t.setFont(&fonts::FreeSansBold18pt7b);
  int w = (int)t.textWidth(clk);
  t.setFont(&fonts::Font2);
  w = max(w, (int)t.textWidth(line)) + 20;
  if (w > 304) w = 304;
  if (box_w > w) t.fillRect(8 + w, 178, box_w - w, 54, 0);      // shorter text than before: clear the rest
  box_w = w;
  t.fillRoundRect(8, 178, w, 54, 8, 0x10A2);
  if (clk[0]) { t.setFont(&fonts::FreeSansBold18pt7b); t.setTextColor(0xEF7D, 0x10A2); t.setCursor(18, 182); t.print(clk); }
  t.setFont(&fonts::Font2); t.setTextColor(0xBDF7, 0x10A2); t.setCursor(18, 214); t.print(line);
  t.endWrite();
}

// main loop while the screensaver is on and set to photos. false = no photos: use the camper scene.
bool photos_begin() {
  count = photos_count();
  cur = -1; shown_at = 0; box_w = 0;
  return count > 0;
}
void photos_tick() {
  if (count <= 0) return;
  if (!shown_at || millis() - shown_at > EVERY) {
    int tries = min(count, 4);
    while (tries--) {
      int k = count == 1 ? 0 : (cur + 1 + (int)(esp_random() % (count - 1))) % count;    // never the same twice
      cur = k;
      if (show(k)) { box_w = 0; break; }
    }
    shown_at = millis(); clock_at = 0;
  }
  if (!clock_at || millis() - clock_at > 10000) { clock_at = millis(); overlay(); }
}

}  // namespace app
