// Automatic updates from GitHub releases (github.com/MrRitchietos/camper-hub, newest release, asset firmware.bin).
// Only when asked (Settings > Update or the phone page): the board restarts and looks on GitHub before Bluetooth
// and the screen start (TLS needs ~40 KB of RAM). Certificates are checked
// against the roots in roots.hpp, so only GitHub can deliver an update. A new version that keeps crashing
// is rolled back to the previous one after 3 failed starts.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include "internal.hpp"
#include "roots.hpp"

namespace app {

char ota_msg[48] = "";
static const char *API = "https://api.github.com/repos/MrRitchietos/camper-hub/releases/latest";

static void msg(const char *m) {               // also kept over the restart, so Settings can show the result
  { Lock l; strlcpy(ota_msg, m, sizeof ota_msg); }
  Preferences p; p.begin("ota", false); p.putString("msg", m); p.end();
  Serial.printf("update: %s\n", m);
}
static long ver_num(const char *v) {             // "v1.2.3" -> 10203
  while (*v && !isdigit((uint8_t)*v)) v++;
  int a = 0, b = 0, c = 0; sscanf(v, "%d.%d.%d", &a, &b, &c);
  return a * 10000L + b * 100 + c;
}

// asked for an update: remember it and restart (the check runs early in the next start)
void ota_request() {
  Preferences p; p.begin("ota", false); p.putBool("now", true); p.end();
  log_event("update check asked, restarting");
  delay(300); sd_restart();
}
bool ota_wanted() { Preferences p; p.begin("ota", true); bool w = p.getBool("now", false); p.end(); return w; }

// main.cpp setup, before anything else: roll back a new version that does not stay up
void ota_boot_guard() {
  Preferences p; p.begin("ota", false);
  {                       // a message naming another version is stale (e.g. after a USB flash): show "check now"
    char m[48]; size_t n = p.getString("msg", m, sizeof m);
    const char *v = n ? strpbrk(m, "0123456789") : nullptr;
    if (v && strncmp(v, FW_VERSION, strlen(FW_VERSION))) { p.remove("msg"); n = 0; }
    if (n) strlcpy(ota_msg, m, sizeof ota_msg);
  }
  // only for the version GitHub just installed (a USB flash runs elsewhere) and only real crashes count
  const esp_partition_t *run = esp_ota_get_running_partition();
  char part[17] = ""; p.getString("part", part, sizeof part);
  if (p.getBool("new", false) && run && strcmp(part, run->label)) { p.putBool("new", false); p.putUChar("tries", 0); }
  esp_reset_reason_t why = esp_reset_reason();
  bool crash = why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT;
  if (p.getBool("new", false) && crash) {
    int tries = p.getUChar("tries", 0) + 1;
    p.putUChar("tries", tries);
    if (tries > 3) {
      const esp_partition_t *other = esp_ota_get_next_update_partition(nullptr);
      p.putBool("new", false); p.putUChar("tries", 0); p.putBool("rolled", true); p.end();
      if (other && esp_ota_set_boot_partition(other) == ESP_OK) { Serial.println("update: new version keeps failing, back to the old one"); delay(200); ESP.restart(); }
      return;
    }
  }
  p.end();
}
void ota_boot_ok() {                              // after a few minutes up: the new version is fine
  Preferences p; p.begin("ota", false);
  if (p.getBool("new", false)) { p.putBool("new", false); p.putUChar("tries", 0); log_event("update to %s running fine", FW_VERSION); p.putString("msg", "updated to " FW_VERSION); }
  if (p.getBool("rolled", false)) { p.putBool("rolled", false); log_event("a new version failed to start: went back to %s", FW_VERSION); }
  p.end();
}

// radio task, after WiFi is up and before Bluetooth starts. Returns only when there is nothing to install.
void ota_check() {
  if (!ota_wanted()) return;
  { Preferences p; p.begin("ota", false); p.putBool("now", false); p.end(); }   // once per tap, never a loop
  if (WiFi.status() != WL_CONNECTED) { msg("no internet, try again later"); return; }
  extern volatile bool ntp_synced;
  for (int i = 0; i < 200 && !ntp_synced; i++) delay(100);        // certificates need the right date
  if (!ntp_synced) { msg("no clock yet, check later"); return; }
  WiFiClientSecure c;
  c.setCACert(GITHUB_ROOTS);
  c.setTimeout(12);
  HTTPClient h;
  h.useHTTP10(true);
  h.setTimeout(12000);
  if (!h.begin(c, API)) { msg("no connection to GitHub"); return; }
  h.addHeader("Accept", "application/vnd.github+json");
  h.setUserAgent("camper-hub/" FW_VERSION);
  int code = h.GET();
  if (code != 200) {
    char e[100] = ""; c.lastError(e, sizeof e);
    Serial.printf("update: GitHub %d, tls: %s, heap %u\n", code, e, (unsigned)ESP.getFreeHeap());
    char b[48]; snprintf(b, sizeof b, code < 0 ? "no connection to GitHub" : "GitHub answered %d", code); msg(b); h.end(); return;
  }
  JsonDocument f, d;
  f["tag_name"] = true; f["draft"] = true; f["prerelease"] = true;
  f["assets"][0]["name"] = true; f["assets"][0]["browser_download_url"] = true; f["assets"][0]["size"] = true;
  DeserializationError e = deserializeJson(d, h.getStream(), DeserializationOption::Filter(f));
  h.end();
  if (e) { msg("bad answer from GitHub"); return; }
  const char *tag = d["tag_name"] | "";
  if (!tag[0] || d["draft"] | false || d["prerelease"] | false) { msg("no release yet"); return; }
  if (ver_num(tag) <= ver_num(FW_VERSION)) { char b[48]; snprintf(b, sizeof b, "up to date (%s)", FW_VERSION); msg(b); return; }
  String url; int size = 0;
  for (JsonObject a : d["assets"].as<JsonArray>()) if (!strcmp(a["name"] | "", "firmware.bin")) { url = a["browser_download_url"] | ""; size = a["size"] | 0; }
  if (!url.length() || size < 100000) { msg("release has no firmware.bin"); return; }
  char b[48]; snprintf(b, sizeof b, "installing %s...", tag); msg(b);
  log_event("installing %s from GitHub", tag);
  HTTPClient g;
  g.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  g.setTimeout(15000);
  if (!g.begin(c, url) || g.GET() != 200) { msg("download failed"); g.end(); return; }
  int len = g.getSize();
  if (len <= 0) len = size;
  if (!Update.begin(len, U_FLASH)) { msg("update does not fit"); g.end(); return; }
  size_t w = Update.writeStream(*g.getStreamPtr());
  g.end();
  if (w != (size_t)len || !Update.end(true)) { Update.abort(); msg("download broken, old version kept"); log_event("update %s failed", tag); return; }
  const esp_partition_t *to = esp_ota_get_boot_partition();
  Preferences p; p.begin("ota", false); p.putBool("new", true); p.putUChar("tries", 0); p.putString("part", to ? to->label : ""); p.end();
  log_event("installed %s from GitHub, restarting", tag);
  { char m[48]; snprintf(m, sizeof m, "updated to %s", tag); msg(m); }
  delay(500);
  sd_restart();
}

}  // namespace app
