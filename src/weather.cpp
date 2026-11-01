// Weather from Open-Meteo (no key, plain HTTP). Location from the internet connection (ip-api.com), so it
// follows the camper whenever it is online (campsite wifi / phone hotspot). Every download is saved on SD
// (/camper/wx.json) and loaded again at boot, so the last forecast is there offline. Each update also adds a
// row to /camper/weather-log.csv. Captive portals (campsite login pages) are detected first.
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <Preferences.h>
#include "internal.hpp"

namespace app {

static const char *WXFILE = "/camper/wx.json";
static const char *WD[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// 204 = real internet; anything else (200 page, redirect) = a login page in between
static int net_check() {
  HTTPClient http;
  http.setTimeout(5000);
  http.useHTTP10(true);
  if (!http.begin("http://connectivitycheck.gstatic.com/generate_204")) return -1;
  int code = http.GET();
  http.end();
  return code;
}

// time zone follows the camper (Open-Meteo tells us which one we are in)
static void apply_tz(const char *tzname) {
  static const char *WET[] = {"Europe/London", "Europe/Lisbon", "Europe/Dublin", "Atlantic/Canary", "Atlantic/Madeira"};
  static const char *EET[] = {"Europe/Athens", "Europe/Helsinki", "Europe/Bucharest", "Europe/Sofia", "Europe/Riga",
                              "Europe/Tallinn", "Europe/Vilnius", "Europe/Kiev", "Europe/Kyiv", "Asia/Nicosia"};
  const char *tz = "CET-1CEST,M3.5.0,M10.5.0/3";
  for (auto z : WET) if (!strcmp(tzname, z)) tz = "GMT0BST,M3.5.0/1,M10.5.0";
  for (auto z : EET) if (!strcmp(tzname, z)) tz = "EET-2EEST,M3.5.0/3,M10.5.0/4";
  if (!strcmp(tzname, "Europe/Istanbul")) tz = "<+03>-3";
  const char *cur = getenv("TZ");
  if (cur && !strcmp(cur, tz)) return;
  setenv("TZ", tz, 1); tzset();
  Preferences p; p.begin("camper", false); p.putString("tz", tz); p.end();
}

static void hhmm(char *out, long t, long off) {   // unix time + the place's UTC offset -> "07:39"
  time_t lt = t + off; struct tm m; gmtime_r(&lt, &m);
  snprintf(out, 6, "%02d:%02d", m.tm_hour, m.tm_min);
}

void weather_warnings(Wx &x) {
  x.warn[0] = 0; x.warn_lvl = 0;
  int frost_h = -1, gust_h = -1, storm_h = -1, maxg = 0;
  for (int i = 0; i < x.nh && i < 24; i++) {
    if (frost_h < 0 && x.h[i].t10 <= 10) frost_h = i;
    if (x.h[i].gust > maxg) maxg = x.h[i].gust;
    if (gust_h < 0 && x.h[i].gust >= 50) gust_h = i;
    if (storm_h < 0 && x.h[i].code >= 95) storm_h = i;
  }
  auto when = [&](int i, char *o) { if (i <= 0) strcpy(o, "now"); else snprintf(o, 12, "%02d:00", (x.h0 + i) % 24); };
  char w[12];
  if (storm_h >= 0) { when(storm_h, w); snprintf(x.warn, sizeof x.warn, "Thunderstorm from %s", w); x.warn_lvl = 2; }
  else if (gust_h >= 0) { when(gust_h, w); snprintf(x.warn, sizeof x.warn, "Gusts %d from %s: awning in", maxg, w); x.warn_lvl = 2; }
  else if (frost_h >= 0) { when(frost_h, w); snprintf(x.warn, sizeof x.warn, "Frost risk from %s: protect water", w); x.warn_lvl = 2; }
  else if (maxg >= 35) { snprintf(x.warn, sizeof x.warn, "Windy, gusts up to %d km/h", maxg); x.warn_lvl = 1; }
  else if (x.nd && x.d_uv[0] >= 6) { snprintf(x.warn, sizeof x.warn, "High UV today (%.0f)", x.d_uv[0]); x.warn_lvl = 1; }
  else if (x.nd && x.d_sum[0] >= 10) { snprintf(x.warn, sizeof x.warn, "Heavy rain today, %.0f mm", x.d_sum[0]); x.warn_lvl = 1; }
}

static bool parse(JsonDocument &w, Wx &x) {
  JsonObject c = w["current"], d = w["daily"], h = w["hourly"];
  if (c.isNull() || d.isNull() || !d["time"][0].is<long>()) return false;   // old text-time cache: ignore
  x.ok = true;
  x.lat = w["latitude"] | 0.0f; x.lon = w["longitude"] | 0.0f;
  x.t = c["temperature_2m"] | 0.0f; x.hum = c["relative_humidity_2m"] | 0.0f;
  x.wind = c["wind_speed_10m"] | 0.0f; x.gust = c["wind_gusts_10m"] | 0.0f;
  x.pres = c["pressure_msl"] | 0.0f; x.code = c["weather_code"] | 0; x.day = (c["is_day"] | 1) == 1;
  long off = w["utc_offset_seconds"] | 3600L;
  x.nd = min((int)d["time"].size(), WX_DAYS);
  for (int i = 0; i < x.nd; i++) {
    x.d_code[i] = d["weather_code"][i] | 0;
    x.d_max[i] = lroundf(d["temperature_2m_max"][i] | 0.0f);
    x.d_min[i] = lroundf(d["temperature_2m_min"][i] | 0.0f);
    x.d_rain[i] = d["precipitation_probability_max"][i] | 0;
    x.d_sum[i] = d["precipitation_sum"][i] | 0.0f;
    x.d_gust[i] = min(255L, lroundf(d["wind_gusts_10m_max"][i] | 0.0f));
    x.d_uv[i] = d["uv_index_max"][i] | 0.0f;
    hhmm(x.d_rise[i], d["sunrise"][i] | 0L, off);
    hhmm(x.d_set[i], d["sunset"][i] | 0L, off);
    time_t dt = (d["time"][i] | 0L) + off + 12 * 3600;
    struct tm t; gmtime_r(&dt, &t);
    strlcpy(x.d_name[i], i == 0 ? "Tod" : WD[t.tm_wday], sizeof x.d_name[i]);
  }
  x.nh = h.isNull() ? 0 : min((int)h["time"].size(), WX_HOURS);
  if (x.nh) { time_t t0 = (h["time"][0] | 0L) + off; struct tm m; gmtime_r(&t0, &m); x.h0 = m.tm_hour; }
  for (int i = 0; i < x.nh; i++) {
    x.h[i].t10 = lroundf((h["temperature_2m"][i] | 0.0f) * 10);
    x.h[i].rain = h["precipitation_probability"][i] | 0;
    x.h[i].wind = min(255L, lroundf(h["wind_speed_10m"][i] | 0.0f));
    x.h[i].gust = min(255L, lroundf(h["wind_gusts_10m"][i] | 0.0f));
    x.h[i].code = h["weather_code"][i] | 0;
  }
  apply_tz(w["timezone"] | "Europe/Brussels");
  weather_warnings(x);
  return true;
}

// the file holds the Open-Meteo answer plus our own "_city" and "_fetched" (added below)
void weather_load_cache() {
  if (demo_on) return;
  File f = SD.open(WXFILE, FILE_READ);
  if (!f) return;
  JsonDocument w;
  bool ok = !deserializeJson(w, f);
  f.close();
  Wx x{};
  if (!ok || !parse(w, x)) return;
  strlcpy(x.city, w["_city"] | "", sizeof x.city);
  x.fetched = w["_fetched"] | 0;
  Lock l; if (!S.wx.ok) { S.wx = x; S.wx_ver++; }
}

void weather_fetch() {
  if (demo_on || WiFi.status() != WL_CONNECTED) return;
  int nc = net_check();
  { Lock l; S.online = nc == 204; S.portal = nc > 0 && nc != 204; }
  static int last_nc = -2;
  if (nc != last_nc) { last_nc = nc; log_event(nc == 204 ? "internet ok" : nc > 0 ? "wifi needs login (captive portal)" : "no internet"); }
  if (nc != 204) return;

  HTTPClient http;
  http.setTimeout(6000);
  http.useHTTP10(true);                // no chunked replies: the JSON parser reads the raw stream
  float plat, plon; char pname[32];
  if (!place_pinned(plat, plon, pname, sizeof pname)) {
    JsonDocument loc, f1;
    f1["status"] = true; f1["lat"] = true; f1["lon"] = true; f1["city"] = true;
    if (!http.begin("http://ip-api.com/json/?fields=status,lat,lon,city") || http.GET() != 200 ||
        deserializeJson(loc, http.getStream(), DeserializationOption::Filter(f1)) || strcmp(loc["status"] | "", "success")) { http.end(); return; }
    http.end();
    plat = loc["lat"]; plon = loc["lon"]; strlcpy(pname, loc["city"] | "", sizeof pname);
  }

  char url[600];
  snprintf(url, sizeof url,
           "http://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
           "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,wind_gusts_10m,pressure_msl,is_day"
           "&hourly=temperature_2m,precipitation_probability,weather_code,wind_speed_10m,wind_gusts_10m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,precipitation_sum,"
           "wind_gusts_10m_max,uv_index_max,sunrise,sunset&timezone=auto&forecast_days=7&forecast_hours=48&timeformat=unixtime",
           plat, plon);
  if (!http.begin(url) || http.GET() != 200) { http.end(); return; }
  JsonDocument w;
  DeserializationError e = deserializeJson(w, http.getStream());
  http.end();
  if (e) return;
  Wx x{};
  if (!parse(w, x)) return;
  strlcpy(x.city, pname, sizeof x.city);
  x.fetched = time(nullptr) > 1700000000 ? time(nullptr) : 0;

  bool sd; { Lock l; sd = S.sd_ok; }
  if (sd) {
    w["_city"] = x.city; w["_fetched"] = x.fetched;
    File f = SD.open("/camper/wx.tmp", FILE_WRITE);
    if (f) { serializeJson(w, f); f.close(); SD.remove(WXFILE); SD.rename("/camper/wx.tmp", WXFILE); }
    struct tm t;
    if (clock_now(t)) {
      bool fresh = !SD.exists("/camper/weather-log.csv");
      File g = SD.open("/camper/weather-log.csv", FILE_APPEND);
      if (g) {
        if (fresh) g.println("date,time,place,lat,lon,temp_c,humidity,wind_kmh,gust_kmh,pressure_hpa,code,today_max,today_min,rain_pct");
        char date[11]; fmt_date(date, sizeof date, t);
        g.printf("%s,%02d:%02d,%s,%.3f,%.3f,%.1f,%.0f,%.0f,%.0f,%.1f,%d,%d,%d,%d\n", date, t.tm_hour, t.tm_min, x.city, x.lat, x.lon,
                 x.t, x.hum, x.wind, x.gust, x.pres, x.code, x.d_max[0], x.d_min[0], x.d_rain[0]);
        g.close();
      }
    }
  }
  Lock l; S.wx = x; S.wx_ver++;
}

void weather_demo() {
  Wx x{};
  x.ok = true; x.fetched = time(nullptr) > 1700000000 ? time(nullptr) - 1800 : 0; strcpy(x.city, "Demo beach");
  x.t = 17.4f; x.hum = 68; x.wind = 14; x.gust = 31; x.pres = 1018; x.code = 2;
  static const uint8_t C[] = {2, 61, 0, 3, 80, 1, 95};
  static const int8_t MX[] = {19, 15, 22, 18, 16, 21, 23}, MN[] = {11, 10, 12, 9, 8, 11, 14};
  static const uint8_t R[] = {10, 80, 0, 20, 60, 5, 70}, G[] = {31, 48, 18, 25, 56, 20, 62};
  static const char *N[] = {"Tod", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  x.nd = 7;
  for (int i = 0; i < 7; i++) {
    x.d_code[i] = C[i]; x.d_max[i] = MX[i]; x.d_min[i] = MN[i]; x.d_rain[i] = R[i]; x.d_gust[i] = G[i];
    x.d_sum[i] = R[i] / 12.0f; x.d_uv[i] = 2 + i % 5; strcpy(x.d_name[i], N[i]);
    snprintf(x.d_rise[i], 6, "07:%02d", 30 + i); snprintf(x.d_set[i], 6, "17:%02d", 25 - i);
  }
  x.nh = 48; x.h0 = 13;
  for (int i = 0; i < 48; i++) {
    float hr = (13 + i) % 24;
    x.h[i].t10 = (14 + 5 * sinf((hr - 9) / 24 * 2 * PI) - i * 0.05f) * 10;
    x.h[i].rain = i > 14 && i < 26 ? 40 + (i % 5) * 10 : 5;
    x.h[i].wind = 12 + i % 9; x.h[i].gust = 22 + (i * 7) % 30;
    x.h[i].code = i > 14 && i < 26 ? 61 : (hr > 7 && hr < 18 ? 2 : 1);
  }
  weather_warnings(x);
  Lock l; S.wx = x; S.wx_ver++;
}

}  // namespace app
