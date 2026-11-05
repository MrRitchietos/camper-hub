// SD card data (runs in the logger task, which owns the card):
//  /camper/min-YYYY-MM-DD.csv    one row per minute with everything known (solar, battery, temps, pressure, bank)
//  /camper/trips.csv             trip diary: one row per day (place, weather, temps, solar, battery)
//  /camper/today.txt             today's trip row so far (survives a power cut)
//  /camper/settings-backup.txt   all settings (not the WiFi passwords, not the Victron key); restored by itself
//                                when the board comes up empty
//  /camper/update.bin            new firmware: Settings > SD card > Install (renamed to update-done.bin)
#include <Arduino.h>
#include <SD.h>
#include <Update.h>
#include <nvs.h>
#include <nvs_flash.h>
#include "internal.hpp"

namespace app {

std::vector<TripDay> trips; TripDay trip_today; uint32_t trips_ver = 0;
SdInfo SDI;
static volatile int8_t want_trips = -1;      // -1 nothing, 0 free, 1 load
static volatile uint8_t job = J_NONE;
static volatile bool want_info = true;
void trips_request(bool load) { want_trips = load; }
void sd_job(uint8_t j) { if (j == J_NONE) want_info = true; else job = j; }

static float nz(float v) { return isnan(v) ? 0 : v; }
static void put(char *row, size_t n, size_t &k, float v, const char *fmt = ",%.1f") {
  if (isnan(v)) k += snprintf(row + k, n - k, ","); else k += snprintf(row + k, n - k, fmt, v);
}
static void lo(float &m, float v) { if (!isnan(v) && (isnan(m) || v < m)) m = v; }
static void hi(float &m, float v) { if (!isnan(v) && (isnan(m) || v > m)) m = v; }

// ---------------------------------------------------------------- trip diary
static const char *TRIPS = "/camper/trips.csv", *TODAY = "/camper/today.txt";
static const char *TRIP_HDR = "date,place,lat,lon,weather_code,rain_mm,forecast_max,forecast_min,outside_min,outside_max,inside_min,inside_max,battery_min_v,solar_kwh,bank_min_pct";
static void trip_fmt(char *o, size_t n, const TripDay &d) {
  size_t k = snprintf(o, n, "%s,%s,%.3f,%.3f,%u,%.1f,%d,%d", d.date, d.place, d.lat, d.lon, d.code, d.rain, d.wmax, d.wmin);
  put(o, n, k, d.omin); put(o, n, k, d.omax); put(o, n, k, d.imin); put(o, n, k, d.imax);
  put(o, n, k, d.vmin, ",%.2f"); put(o, n, k, d.kwh, ",%.2f");
  k += snprintf(o + k, n - k, d.bmin >= 0 ? ",%d" : ",", d.bmin);
}
static bool trip_parse(char *l, TripDay &d) {
  char *f[16]; int n = 0;
  for (char *p = l; n < 16;) { f[n++] = p; p = strchr(p, ','); if (!p) break; *p++ = 0; }
  if (n < 15 || strlen(f[0]) != 10 || f[0][4] != '-') return false;
  auto fl = [&](int i) { return *f[i] ? (float)atof(f[i]) : NAN; };
  strlcpy(d.date, f[0], sizeof d.date); strlcpy(d.place, f[1], sizeof d.place);
  d.lat = atof(f[2]); d.lon = atof(f[3]); d.code = atoi(f[4]); d.rain = atof(f[5]); d.wmax = atoi(f[6]); d.wmin = atoi(f[7]);
  d.omin = fl(8); d.omax = fl(9); d.imin = fl(10); d.imax = fl(11); d.vmin = fl(12); d.kwh = fl(13); d.bmin = *f[14] ? atoi(f[14]) : -1;
  return true;
}
static char trip_last[11] = "";      // newest date in trips.csv (set by trips_load)
static void trips_load();
void trips_check() {                  // boot: clean the diary and learn its newest date, without keeping it in RAM
  bool shown; { Lock l; shown = !trips.empty(); }
  trips_load();
  if (!shown) { Lock l; std::vector<TripDay>().swap(trips); trips_ver++; }
}
static void trip_append(const TripDay &d) {
  if (!trip_last[0]) trips_check();
  if (strcmp(d.date, trip_last) <= 0) { log_event("trip day %s not saved: %s is already in the diary", d.date, trip_last); return; }
  strlcpy(trip_last, d.date, sizeof trip_last);
  bool fresh = !SD.exists(TRIPS);
  File f = SD.open(TRIPS, FILE_APPEND);
  if (!f) return;
  if (fresh) f.println(TRIP_HDR);
  char row[200]; trip_fmt(row, sizeof row, d); f.println(row);
  f.close();
  log_event("trip day saved: %s %s", d.date, d.place);
}
static void trip_save_today() {
  char row[200]; { Lock l; if (!trip_today.date[0]) return; trip_fmt(row, sizeof row, trip_today); }
  File f = SD.open("/camper/today.tmp", FILE_WRITE);
  if (!f) return;
  f.println(row); f.close();
  SD.remove(TODAY); SD.rename("/camper/today.tmp", TODAY);
}
// rows must go up by date: rows out of order (from a wrong clock) are dropped and the file is cleaned once
// (the old file is kept as trips.bak)
static void trips_load() {
  std::vector<TripDay> v; std::vector<String> keep; int dropped = 0;
  char last[11] = "";
  File f = SD.open(TRIPS);
  if (f) {
    char line[220], copy[220];
    while (f.available()) {
      int n = f.readBytesUntil('\n', line, sizeof line - 1); line[n] = 0;
      if (n && line[n - 1] == '\r') line[n - 1] = 0;
      strlcpy(copy, line, sizeof copy);
      TripDay d; if (!trip_parse(line, d)) continue;
      if (strcmp(d.date, last) <= 0) { dropped++; continue; }
      strlcpy(last, d.date, sizeof last);
      keep.push_back(copy);
      v.push_back(d);
      if (v.size() > 60) v.erase(v.begin());
    }
    f.close();
  }
  if (dropped) {
    SD.remove("/camper/trips.bak"); SD.rename(TRIPS, "/camper/trips.bak");
    File o = SD.open(TRIPS, FILE_WRITE);
    if (o) { o.println(TRIP_HDR); for (auto &k : keep) o.println(k); o.close(); }
    log_event("trip diary: %d rows with a wrong date removed (old file: trips.bak)", dropped);
  }
  strlcpy(trip_last, last[0] ? last : "0", sizeof trip_last);
  std::reverse(v.begin(), v.end());
  Lock l; trips = std::move(v); trips_ver++;
}

// called every minute (not in demo) with the clock set
static void trip_minute(const struct tm &t, float tin, float tout, float vbat, float kwh, int bank) {
  char date[11]; fmt_date(date, sizeof date, t);
  bool have; { Lock l; have = trip_today.date[0]; }
  if (!have) {                                   // first minute after boot: carry on with today's row or close the old one
    TripDay d; bool ok = false;
    File f = SD.open(TODAY);
    if (f) { char line[220]; int n = f.readBytesUntil('\n', line, sizeof line - 1); line[n] = 0; f.close(); ok = trip_parse(line, d); }
    if (ok && strcmp(d.date, date)) { trip_append(d); ok = false; if (!trips.empty() || want_trips == 1) trips_load(); }
    if (!ok) { d = TripDay{}; strlcpy(d.date, date, sizeof d.date); }
    Lock l; trip_today = d; trips_ver++;
  }
  bool rolled = false; TripDay done;
  {
    Lock l;
    if (strcmp(trip_today.date, date)) { done = trip_today; rolled = true; trip_today = TripDay{}; strlcpy(trip_today.date, date, sizeof trip_today.date); }
    TripDay &d = trip_today;
    if (S.wx.ok && S.wx.nd) {
      strlcpy(d.place, S.wx.city, sizeof d.place); d.lat = S.wx.lat; d.lon = S.wx.lon;
      d.code = S.wx.d_code[0]; d.rain = S.wx.d_sum[0]; d.wmax = S.wx.d_max[0]; d.wmin = S.wx.d_min[0];
    } else if (P.pinned && !d.place[0]) { strlcpy(d.place, P.cur.name, sizeof d.place); d.lat = P.cur.lat; d.lon = P.cur.lon; }
    for (char *c = d.place; *c; c++) if (*c == ',') *c = ' ';
    lo(d.omin, tout); hi(d.omax, tout); lo(d.imin, tin); hi(d.imax, tin); lo(d.vmin, vbat); hi(d.kwh, kwh);
    if (bank >= 0 && (d.bmin < 0 || bank < d.bmin)) d.bmin = bank;
    trips_ver++;
  }
  if (rolled && done.date[0]) { trip_append(done); if (!trips.empty()) trips_load(); }
  static uint32_t last = 0;
  if (rolled || !last || millis() - last > 600000) { last = millis(); trip_save_today(); }
}

// ---------------------------------------------------------------- minute row with everything
static const char *MIN_HDR = "time,pv_w,vbat,ibat,load_a,state,yield_kwh,t_inside,t_outside,t_fridge,soc_est,h_inside,h_outside,t_freezer,hpa,bank_soc,forecast_t";
void store_minute(const struct tm &t) {
  char row[200]; size_t k = 0;
  float tin = NAN, tout = NAN, vbat = NAN, kwh = NAN; int bank = -1; bool any = false;
  {
    Lock l;
    k = snprintf(row, sizeof row, "%02d:%02d", t.tm_hour, t.tm_min);
    const Victron &v = S.vic;
    bool vk = v.decoded && millis() - v.at < 120000;
    if (vk) {
      any = true;
      k += snprintf(row + k, sizeof row - k, ",%u,%.2f,%.1f,", v.pv_w == 0xFFFF ? 0 : v.pv_w, v.vbat_cv / 100.0f, v.ibat_da / 10.0f);
      if (v.load_da != 0x1FF) k += snprintf(row + k, sizeof row - k, "%.1f", v.load_da / 10.0f);
      k += snprintf(row + k, sizeof row - k, ",%s,%.2f", vic_state(v.state), v.yield_10wh == 0xFFFF ? 0 : v.yield_10wh / 100.0f);
      vbat = v.vbat_cv / 100.0f; if (v.yield_10wh != 0xFFFF) kwh = v.yield_10wh / 100.0f;
    } else k += snprintf(row + k, sizeof row - k, ",,,,,,");
    auto sens = [&](uint8_t r, bool hum) { const Sensor *s = sens_by_role(r); return s ? (hum ? s->h : s->t) : NAN; };
    tin = sens(R_INSIDE, false); tout = sens(R_OUTSIDE, false);
    float tfr = sens(R_FRIDGE, false), tfz = sens(R_FREEZER, false), hin = sens(R_INSIDE, true), hout = sens(R_OUTSIDE, true);
    float hpa = NAN;
    for (auto &s : S.sensors) if (!isnan(s.hpa) && millis() - s.seen < 600000) { hpa = s.hpa; break; }
    put(row, sizeof row, k, tin); put(row, sizeof row, k, tout); put(row, sizeof row, k, tfr);
    int soc = vk ? vic_soc(v) : -1;
    k += snprintf(row + k, sizeof row - k, soc >= 0 ? ",%d" : ",", soc);
    put(row, sizeof row, k, hin, ",%.0f"); put(row, sizeof row, k, hout, ",%.0f"); put(row, sizeof row, k, tfz); put(row, sizeof row, k, hpa);
    const Bank &b = S.bank;
    if (b.data && millis() - b.at < 60000 && b.soc >= 0) { bank = (int)(b.soc + 0.5f); k += snprintf(row + k, sizeof row - k, ",%d", bank); }
    else k += snprintf(row + k, sizeof row - k, ",");
    float wt = S.wx.ok ? S.wx.t : NAN;
    put(row, sizeof row, k, wt);
    any = any || !isnan(tin) || !isnan(tout) || !isnan(tfr) || !isnan(hpa) || bank >= 0 || !isnan(wt);
  }
  trip_minute(t, tin, isnan(tout) ? NAN : tout, vbat, kwh, bank);
  if (!any) return;
  char path[40], date[11]; fmt_date(date, sizeof date, t);
  snprintf(path, sizeof path, "/camper/min-%s.csv", date);
  bool fresh = !SD.exists(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) return;
  if (fresh) f.println(MIN_HDR);
  f.println(row);
  f.close();
  Lock l; S.sd_rows++;
}

// ---------------------------------------------------------------- settings backup (NVS -> text file)
static const char *BACKUP = "/camper/settings-backup.txt";
static const char *NS[] = {"camper", "sensors", "level", "place", "victron", "check"};
static bool skip(const char *ns, const char *key) { return !strcmp(ns, "victron") && !strcmp(key, "key"); }
static void hex(char *o, const uint8_t *b, size_t n) { for (size_t i = 0; i < n; i++) sprintf(o + i * 2, "%02x", b[i]); }
static bool backup() {
  File f = SD.open("/camper/settings.tmp", FILE_WRITE);
  if (!f) return false;
  time_t now = time(nullptr); struct tm t; char ts[20] = "unknown";
  if (now > 1700000000) { localtime_r(&now, &t); strftime(ts, sizeof ts, "%Y-%m-%d %H:%M", &t); }
  f.printf("# camper hub settings backup %s (not included: WiFi passwords, Victron key)\n", ts);
  int n = 0;
  for (const char *ns : NS) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) continue;
    nvs_iterator_t it = nvs_entry_find("nvs", ns, NVS_TYPE_ANY);
    while (it) {
      nvs_entry_info_t e; nvs_entry_info(it, &e);
      it = nvs_entry_next(it);
      if (skip(ns, e.key)) continue;
      char val[260] = ""; const char *ty = nullptr;
      switch (e.type) {
        case NVS_TYPE_U8: { uint8_t v; nvs_get_u8(h, e.key, &v); snprintf(val, sizeof val, "%u", v); ty = "u8"; break; }
        case NVS_TYPE_I8: { int8_t v; nvs_get_i8(h, e.key, &v); snprintf(val, sizeof val, "%d", v); ty = "i8"; break; }
        case NVS_TYPE_U16: { uint16_t v; nvs_get_u16(h, e.key, &v); snprintf(val, sizeof val, "%u", v); ty = "u16"; break; }
        case NVS_TYPE_I16: { int16_t v; nvs_get_i16(h, e.key, &v); snprintf(val, sizeof val, "%d", v); ty = "i16"; break; }
        case NVS_TYPE_U32: { uint32_t v; nvs_get_u32(h, e.key, &v); snprintf(val, sizeof val, "%u", (unsigned)v); ty = "u32"; break; }
        case NVS_TYPE_I32: { int32_t v; nvs_get_i32(h, e.key, &v); snprintf(val, sizeof val, "%d", (int)v); ty = "i32"; break; }
        case NVS_TYPE_STR: { size_t l = 120; char s[120]; if (nvs_get_str(h, e.key, s, &l) == ESP_OK) { hex(val, (uint8_t *)s, l ? l - 1 : 0); ty = "str"; } break; }
        case NVS_TYPE_BLOB: { size_t l = 64; uint8_t b[64]; if (nvs_get_blob(h, e.key, b, &l) == ESP_OK) { hex(val, b, l); ty = "blob"; } break; }
        default: break;
      }
      if (ty) { f.printf("%s|%s|%s|%s\n", ns, e.key, ty, val); n++; }
    }
    nvs_close(h);
  }
  f.close();
  SD.remove(BACKUP); SD.rename("/camper/settings.tmp", BACKUP);
  log_event("settings backed up (%d values)", n);
  return true;
}
static int unhex(uint8_t *o, const char *s, size_t max) {
  size_t n = 0;
  while (s[0] && s[1] && n < max) { char b[3] = {s[0], s[1], 0}; o[n++] = strtoul(b, nullptr, 16); s += 2; }
  return n;
}
static int restore() {
  File f = SD.open(BACKUP);
  if (!f) return -1;
  int n = 0; char line[300];
  while (f.available()) {
    int len = f.readBytesUntil('\n', line, sizeof line - 1); line[len] = 0;
    if (line[0] == '#' || !len) continue;
    char *ns = line, *key = strchr(ns, '|'); if (!key) continue; *key++ = 0;
    char *ty = strchr(key, '|'); if (!ty) continue; *ty++ = 0;
    char *val = strchr(ty, '|'); if (!val) continue; *val++ = 0;
    if (skip(ns, key)) continue;
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) continue;
    long v = atol(val); esp_err_t r = ESP_FAIL;
    if (!strcmp(ty, "u8")) r = nvs_set_u8(h, key, v); else if (!strcmp(ty, "i8")) r = nvs_set_i8(h, key, v);
    else if (!strcmp(ty, "u16")) r = nvs_set_u16(h, key, v); else if (!strcmp(ty, "i16")) r = nvs_set_i16(h, key, v);
    else if (!strcmp(ty, "u32")) r = nvs_set_u32(h, key, strtoul(val, nullptr, 10)); else if (!strcmp(ty, "i32")) r = nvs_set_i32(h, key, v);
    else if (!strcmp(ty, "str")) { char s[121]; int k = unhex((uint8_t *)s, val, 120); s[k] = 0; r = nvs_set_str(h, key, s); }
    else if (!strcmp(ty, "blob")) { uint8_t b[64]; int k = unhex(b, val, 64); r = nvs_set_blob(h, key, b, k); }
    if (r == ESP_OK) n++;
    nvs_commit(h); nvs_close(h);
  }
  f.close();
  return n;
}
bool store_auto_restore() {                     // logger, right after mounting: board came up empty but a backup exists
  nvs_handle_t h; bool empty = true;
  if (nvs_open("camper", NVS_READONLY, &h) == ESP_OK) { uint16_t v; empty = nvs_get_u16(h, "wb", &v) != ESP_OK; nvs_close(h); }
  if (!empty || !SD.exists(BACKUP)) return false;
  File f = SD.open(BACKUP); bool has_wb = false; char line[300];
  while (f && f.available()) { int n = f.readBytesUntil('\n', line, sizeof line - 1); line[n] = 0; if (!strncmp(line, "camper|wb|", 10)) has_wb = true; }
  if (f) f.close();
  if (!has_wb) return false;                    // a backup without our own keys cannot be ours: never loop
  int n = restore();
  Serial.printf("settings restored from SD (%d values), restarting\n", n);
  log_event("empty board: settings restored from the SD card (%d values)", n);
  delay(800);
  sd_restart();
  return true;
}

// ---------------------------------------------------------------- firmware update from the card
static const char *UPD = "/camper/update.bin";
static void set_msg(const char *m) { Lock l; strlcpy(SDI.msg, m, sizeof SDI.msg); SDI.ver++; }
static void update() {
  File f = SD.open(UPD);
  if (!f) { set_msg("No update.bin on the card"); return; }
  size_t sz = f.size();
  if (f.peek() != 0xE9 || sz < 100000) { f.close(); set_msg("update.bin is not a firmware file"); return; }
  set_msg("Installing, do not switch off...");
  log_event("installing firmware from SD (%u KB)", (unsigned)(sz / 1024));
  if (!Update.begin(sz, U_FLASH)) { f.close(); set_msg("Update does not fit (old layout?)"); return; }
  size_t w = Update.writeStream(f);
  f.close();
  if (w != sz || !Update.end(true)) { set_msg("Update failed, old version kept"); log_event("firmware update failed"); return; }
  SD.remove("/camper/update-done.bin"); SD.rename(UPD, "/camper/update-done.bin");
  set_msg("Done, restarting...");
  log_event("firmware installed, restarting");
  delay(1000);
  sd_restart();
}

static void info() {
  SdInfo s;
  File b = SD.open(BACKUP);
  if (b) { s.backup = true; char l[80]; int n = b.readBytesUntil('\n', l, sizeof l - 1); l[n] = 0; b.close();
           const char *p = strstr(l, "backup "); if (p) { strlcpy(s.backup_at, p + 7, 17); } }
  File u = SD.open(UPD);
  if (u) { s.update = true; s.update_kb = u.size() / 1024; u.close(); }
  File d = SD.open("/camper");
  if (d) { for (File f = d.openNextFile(); f; f = d.openNextFile()) { if (!f.isDirectory()) s.files++; f.close(); } d.close(); }
  Lock l; strlcpy(s.msg, SDI.msg, sizeof s.msg); s.ver = SDI.ver + 1; SDI = s;
}

// logger task, every loop (the card is mounted)
void store_service() {
  if (want_trips >= 0) {
    int8_t w = want_trips; want_trips = -1;
    if (w) trips_load(); else { Lock l; std::vector<TripDay>().swap(trips); trips_ver++; }
  }
  uint8_t j = job; job = J_NONE;
  if (j == J_BACKUP) { set_msg(backup() ? "Settings saved on the card" : "Could not write the card"); want_info = true; }
  else if (j == J_RESTORE) {
    int n = restore();
    if (n < 0) set_msg("No backup on the card");
    else { set_msg("Restored, restarting..."); log_event("settings restored from SD (%d values)", n); delay(1200); sd_restart(); }
  } else if (j == J_UPDATE) update();
  static uint32_t last_bk = 0;
  if (!demo_on && millis() > 180000 && (!last_bk || millis() - last_bk > 86400000UL)) { last_bk = millis(); save_settings(); backup(); want_info = true; }
  static uint32_t last_info = 0;
  if (want_info || millis() - last_info > 600000) { want_info = false; last_info = millis(); info(); }
}

}  // namespace app
