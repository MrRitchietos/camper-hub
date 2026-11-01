// Weather place: IP location is often wrong on campsite WiFi / phone hotspots (it shows the provider),
// so the place can be pinned to a town found with the free Open-Meteo place search.
// Pinned place in NVS "place", recent places on the SD card (/camper/places.csv) so they work offline.
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SD.h>
#include "internal.hpp"

namespace app {

Places P;
static char query[40] = "";
static volatile bool want_search = false;
void weather_request();   // radio.cpp

static void load_recent() {
  File f = SD.open("/camper/places.csv");
  if (!f) return;
  Place tmp[PLACE_RECENT]; int n = 0;
  while (f.available()) {                       // oldest first in the file: keep the last ones
    String l = f.readStringUntil('\n');
    int a = l.indexOf(','), b = l.indexOf(',', a + 1), c = l.indexOf(',', b + 1);
    if (a < 0 || b < 0 || c < 0) continue;
    Place p;
    strlcpy(p.name, l.substring(0, a).c_str(), sizeof p.name);
    strlcpy(p.area, l.substring(a + 1, b).c_str(), sizeof p.area);
    p.lat = l.substring(b + 1, c).toFloat(); p.lon = l.substring(c + 1).toFloat();
    for (int i = 0; i < n; i++) if (!strcmp(tmp[i].name, p.name) && !strcmp(tmp[i].area, p.area)) { memmove(tmp + i, tmp + i + 1, (n - i - 1) * sizeof(Place)); n--; break; }
    if (n == PLACE_RECENT) { memmove(tmp, tmp + 1, (n - 1) * sizeof(Place)); n--; }
    tmp[n++] = p;
  }
  f.close();
  Lock l;
  P.n_recent = n;
  for (int i = 0; i < n; i++) P.recent[i] = tmp[n - 1 - i];
  P.ver++;
}

void places_begin() {           // after the SD card is mounted
  Preferences p; p.begin("place", true);
  Place c;
  bool pin = p.getBool("pin", false);
  p.getString("name", c.name, sizeof c.name); p.getString("area", c.area, sizeof c.area);
  c.lat = p.getFloat("lat", 0); c.lon = p.getFloat("lon", 0);
  p.end();
  { Lock l; P.pinned = pin && (c.lat || c.lon); P.cur = c; }
  if (S.sd_ok) load_recent();
}

bool place_pinned(float &lat, float &lon, char *name, size_t n) {
  Lock l;
  if (!P.pinned) return false;
  lat = P.cur.lat; lon = P.cur.lon; strlcpy(name, P.cur.name, n);
  return true;
}

void place_pin(const Place &c) {
  Preferences p; p.begin("place", false);
  p.putBool("pin", true); p.putString("name", c.name); p.putString("area", c.area);
  p.putFloat("lat", c.lat); p.putFloat("lon", c.lon);
  p.end();
  { Lock l; P.pinned = true; P.cur = c; P.ver++; }
  if (S.sd_ok) {
    File f = SD.open("/camper/places.csv", FILE_APPEND);
    if (f) { f.printf("%s,%s,%.4f,%.4f\n", c.name, c.area, c.lat, c.lon); f.close(); }
    load_recent();
  }
  log_event("weather place pinned: %s %s", c.name, c.area);
  weather_request();
}
void place_auto() {
  Preferences p; p.begin("place", false); p.putBool("pin", false); p.end();
  { Lock l; P.pinned = false; P.ver++; }
  log_event("weather place: auto");
  weather_request();
}

void place_search(const char *q) {
  { Lock l; strlcpy(query, q, sizeof query); P.searching = true; P.err[0] = 0; P.n_hits = -1; P.ver++; }
  want_search = true;
}

static void url_enc(char *o, size_t n, const char *s) {
  size_t k = 0;
  for (; *s && k + 4 < n; s++) {
    uint8_t c = *s;
    if (isalnum(c) || c == '-' || c == '.') o[k++] = c;
    else k += snprintf(o + k, n - k, "%%%02X", c);
  }
  o[k] = 0;
}

void place_service() {          // radio task
  if (!want_search) return;
  want_search = false;
  char q[40]; { Lock l; strlcpy(q, query, sizeof q); }
  auto fail = [](const char *m) { Lock l; P.searching = false; strlcpy(P.err, m, sizeof P.err); P.n_hits = 0; P.ver++; };
  if (WiFi.status() != WL_CONNECTED) { fail("no WiFi"); return; }
  char enc[120], url[220];
  url_enc(enc, sizeof enc, q);
  snprintf(url, sizeof url, "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=%d&language=en&format=json", enc, PLACE_HITS);
  HTTPClient http;
  http.setTimeout(6000);
  http.useHTTP10(true);
  if (!http.begin(url) || http.GET() != 200) { http.end(); fail("search failed (internet?)"); return; }
  JsonDocument f, d;
  f["results"][0]["name"] = true; f["results"][0]["admin1"] = true; f["results"][0]["country_code"] = true;
  f["results"][0]["latitude"] = true; f["results"][0]["longitude"] = true;
  DeserializationError e = deserializeJson(d, http.getStream(), DeserializationOption::Filter(f));
  http.end();
  if (e) { fail("bad answer"); return; }
  Lock l;
  int n = 0;
  for (JsonObject r : d["results"].as<JsonArray>()) {
    if (n >= PLACE_HITS) break;
    Place &p = P.hits[n++];
    strlcpy(p.name, r["name"] | "?", sizeof p.name);
    snprintf(p.area, sizeof p.area, "%s %s", (const char *)(r["country_code"] | ""), (const char *)(r["admin1"] | ""));
    for (char *c = p.name; *c; c++) if (*c == ',') *c = ' ';
    for (char *c = p.area; *c; c++) if (*c == ',') *c = ' ';
    p.lat = r["latitude"] | 0.0f; p.lon = r["longitude"] | 0.0f;
  }
  P.n_hits = n; P.searching = false; P.ver++;
}

}  // namespace app
