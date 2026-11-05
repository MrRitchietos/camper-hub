// Radio task, core 0: wifi (saved networks, NTP, weather) + one continuous passive BLE scan that feeds the
// Victron decoder and the thermometers. Never blocks the UI.
#include <WiFi.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <esp_sntp.h>
#include "internal.hpp"
#if !defined(RELEASE_BUILD) && __has_include("secrets.hpp")
#include "secrets.hpp"                               // local builds only: a first WiFi to start with
#else
#define HOME_SSID ""
#define HOME_PASS ""
#endif

namespace app {

State S;
TaskHandle_t task_radio, task_sd, task_web;
SemaphoreHandle_t mtx;
volatile bool demo_on = false;
volatile bool ntp_synced = false;

static volatile bool want_scan = false, want_connect = false, want_weather = false;
void weather_request() { want_weather = true; }
static char new_ssid[33], new_pass[65];
static Preferences prefs;

void request_wifi_scan() { want_scan = true; }
volatile bool ota_done = false;
volatile bool ble_off = false;
static volatile uint32_t ble_pause_until = 0; static bool ble_paused = false;
void ble_pause(bool on) {                            // uploads from the phone: the scan takes radio time WiFi needs
  ble_pause_until = on ? millis() + 60000 : 0;
  if (on) for (int i = 0; i < 25 && !ble_paused; i++) delay(20);
} static volatile bool want_ble_off = false;
bool ble_stop() {                                    // firmware upload: Bluetooth off frees ~30 KB (restart after)
  want_ble_off = true;
  for (int i = 0; i < 150 && !ble_off; i++) delay(20);
  return ble_off;
}
static volatile bool ap_want = false, ap_redo = false; static bool ap_is = false;
void ap_enable(bool on) { ap_want = on; }
void ap_new_pass() { ap_redo = true; }               // the phone password changed: the own WiFi uses it too
static void ap_service() {                           // the board's own WiFi for the phone page (follows the camp WiFi channel)
  if (ap_redo && ap_is && ap_want) { ap_redo = false; WiFi.softAP("CamperHub", web_code(), 1, 0, 2); log_event("own WiFi: new password"); }
  ap_redo = false;
  if (ap_want == ap_is) return;
  ap_is = ap_want;
  if (ap_is) { WiFi.mode(WIFI_AP_STA); WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
               WiFi.softAP("CamperHub", web_code(), 1, 0, 2); log_event("own WiFi CamperHub on"); }
  else { WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); log_event("own WiFi off"); }
}
void connect_to(const char *ssid, const char *pass) {
  strlcpy(new_ssid, ssid, sizeof new_ssid);
  strlcpy(new_pass, pass ? pass : "", sizeof new_pass);
  want_connect = true;
}
void set_demo(bool on) {
  demo_on = on;
  { Lock l; S.bank = Bank{}; }
  vic_demo_reset();
  { Lock l; S.demo = on; S.wx = Wx{}; S.wx_ver++; }
  if (on) weather_demo(); else want_weather = true;
}
static void set_status(const char *s) { Lock l; strlcpy(S.wifi_status, s, sizeof S.wifi_status); }

// ---------------------------------------------------------------- saved networks (NVS)
static constexpr int MAX_SAVED = 8;
struct Saved { String ssid, pass; };
static std::vector<Saved> load_saved() {
  std::vector<Saved> v;
  int n = prefs.getInt("n", 0);
  for (int i = 0; i < n && i < MAX_SAVED; i++)
    v.push_back({prefs.getString(("s" + String(i)).c_str(), ""), prefs.getString(("p" + String(i)).c_str(), "")});
  return v;
}
static void save_network(const char *ssid, const char *pass) {
  auto v = load_saved();
  v.erase(std::remove_if(v.begin(), v.end(), [&](const Saved &s) { return s.ssid == ssid; }), v.end());
  v.insert(v.begin(), {ssid, pass});
  if ((int)v.size() > MAX_SAVED) v.resize(MAX_SAVED);
  prefs.putInt("n", v.size());
  for (size_t i = 0; i < v.size(); i++) {
    prefs.putString(("s" + String(i)).c_str(), v[i].ssid);
    prefs.putString(("p" + String(i)).c_str(), v[i].pass);
  }
}
static void seed_home() {                            // keep the first WiFi in NVS, so a release (no secrets) still has it
  if (prefs.getInt("n", 0) == 0 && HOME_SSID[0]) save_network(HOME_SSID, HOME_PASS);
}
static bool join(const char *ssid, const char *pass, uint32_t timeout_ms = 15000) {
  char msg[48];
  snprintf(msg, sizeof msg, "connecting to %s", ssid);
  set_status(msg);
  WiFi.disconnect(false, false);
  delay(50);
  if (pass && *pass) WiFi.begin(ssid, pass); else WiFi.begin(ssid);
  uint32_t t0 = millis();
  while (millis() - t0 < timeout_ms) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) return true;
    if (st == WL_CONNECT_FAILED) break;
    delay(100);
  }
  snprintf(msg, sizeof msg, "could not join %s", ssid);
  set_status(msg);
  return false;
}
static void join_best_known() {
  auto saved = load_saved();
  set_status("looking for known wifi");
  int n = WiFi.scanNetworks(false, true);
  int best = -1, best_rssi = -999;
  for (int i = 0; i < n; i++)
    for (size_t k = 0; k < saved.size(); k++)
      if (WiFi.SSID(i) == saved[k].ssid && WiFi.RSSI(i) > best_rssi) { best = k; best_rssi = WiFi.RSSI(i); }
  WiFi.scanDelete();
  if (best >= 0) join(saved[best].ssid.c_str(), saved[best].pass.c_str());
  else set_status("offline (no known wifi)");
}
static void update_link() {
  bool c = WiFi.status() == WL_CONNECTED;
  Lock l;
  S.wifi = c;
  if (c) {
    strlcpy(S.ssid, WiFi.SSID().c_str(), sizeof S.ssid);
    S.rssi = WiFi.RSSI();
    S.ip = WiFi.localIP();
    snprintf(S.wifi_status, sizeof S.wifi_status, "online via %s", S.ssid);
  } else { S.ip = 0; S.online = false; S.portal = false; }
}

static bool scan_running = false;
static void start_scan() {
  if (scan_running) return;
  if (WiFi.scanNetworks(true, true) == WIFI_SCAN_FAILED) return;
  scan_running = true;
  Lock l; S.scanning = true;
}
static void poll_scan() {
  if (!scan_running) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  scan_running = false;
  std::vector<Ap> v;
  for (int i = 0; i < n && i < 30; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (auto &a : v) if (s == a.ssid) dup = true;
    if (dup) continue;
    Ap a{};
    strlcpy(a.ssid, s.c_str(), sizeof a.ssid);
    a.rssi = WiFi.RSSI(i);
    a.open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    v.push_back(a);
  }
  WiFi.scanDelete();
  std::sort(v.begin(), v.end(), [](const Ap &a, const Ap &b) { return a.rssi > b.rssi; });
  Lock l; S.aps = std::move(v); S.scanning = false; S.scan_ver++;
}

// ---------------------------------------------------------------- bluetooth: passive, every advert
class BleCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *d) override {
    char addr[18];
    strlcpy(addr, d->getAddress().toString().c_str(), sizeof addr);
    if (d->getManufacturerDataCount()) {
      std::string m = d->getManufacturerData();
      if (m.size() >= 2 && (uint8_t)m[0] == 0xE1 && (uint8_t)m[1] == 0x02) vic_parse(m, addr, d->getRSSI());
      else if (m.size() >= 2 && (uint8_t)m[0] == 0x99 && (uint8_t)m[1] == 0x04) sens_ruuvi(m, addr, d->getRSSI());
    }
    bank_seen(d);
    for (int i = 0; i < (int)d->getServiceDataCount(); i++) {
      NimBLEUUID u = d->getServiceDataUUID(i);
      if (u.bitSize() != 16) continue;
      uint16_t id = u.getValue()[0] | u.getValue()[1] << 8;
      if (id == 0xFCD2 || id == 0x181A) sens_parse(id, d->getServiceData(i), addr, d->getRSSI());
    }
  }
  void onScanEnd(const NimBLEScanResults &, int) override { restart = true; }
 public:
  volatile bool restart = false;
};
static BleCb ble_cb;
static void ble_begin() {
  WiFi.setSleep(true);                                // required while wifi and BT share the radio
  NimBLEDevice::init("");
  auto *sc = NimBLEDevice::getScan();
  sc->setScanCallbacks(&ble_cb, true);
  sc->setActiveScan(false);
  sc->setMaxResults(0);                               // nothing stored: runs for months
  NimBLEDevice::setMTU(247);                          // Fossibot replies are 168 bytes
  sc->setInterval(100);
  sc->setWindow(45);                                  // 45 % of the radio: sensors still caught, WiFi stays quick
  sc->start(0, false, true);
}

static void sntp_cb(struct timeval *tv) { ntp_synced = true; clock_set(tv ? (long)tv->tv_sec : (long)time(nullptr), CLK_NET); }

// ---------------------------------------------------------------- main radio task
static void task(void *) {
  seed_home();
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("camper-hub");
  WiFi.setAutoReconnect(true);
  { Preferences p; p.begin("camper", true); String tz = p.getString("tz", "CET-1CEST,M3.5.0,M10.5.0/3"); p.end(); setenv("TZ", tz.c_str(), 1); tzset(); }
  sntp_set_time_sync_notification_cb(sntp_cb);
  join_best_known();
  update_link();
  { char tz[48]; strlcpy(tz, getenv("TZ") ? getenv("TZ") : "CET-1CEST,M3.5.0,M10.5.0/3", sizeof tz); configTzTime(tz, "pool.ntp.org", "time.google.com"); }
  ota_check();                                        // before Bluetooth and the screen: TLS needs the RAM
  ota_done = true;
  ble_begin();

  uint32_t last_link = 0, last_tick = 0, last_wx = 0, lost_at = 0;
  bool was_online = false;
  for (;;) {
    uint32_t now = millis();
    if (want_connect) {
      want_connect = false;
      if (scan_running) { WiFi.scanDelete(); scan_running = false; Lock l; S.scanning = false; }
      if (join(new_ssid, new_pass)) save_network(new_ssid, new_pass); else join_best_known();
      update_link();
    }
    ap_service();
    if (want_scan) { want_scan = false; start_scan(); }
    poll_scan();
    if (want_ble_off && !ble_off) {
      NimBLEDevice::getScan()->stop(); NimBLEDevice::deinit(true); ble_off = true;
      Serial.printf("bluetooth off for the update, heap %u big %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
    // low memory brake: the scan allocates for every advert it hears, so it rests until memory is back
    static bool low_mem = false;
    { uint32_t f = ESP.getFreeHeap(), b = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
      if (!low_mem && (f < 15000 || b < 6000)) { low_mem = true; Serial.printf("low memory (%u / %u): scan rests\n", (unsigned)f, (unsigned)b); }
      else if (low_mem && f > 22000 && b > 10000) low_mem = false; }
    bool want_pause = low_mem || (ble_pause_until && (int32_t)(ble_pause_until - millis()) > 0);
    if (!ble_off && want_pause && !ble_paused) { ble_paused = true; NimBLEDevice::getScan()->stop(); }
    if (!ble_off && !want_pause && ble_paused) { ble_paused = false; if (!low_mem) ble_pause_until = 0; ble_cb.restart = true; }
    if (!ble_off && !ble_paused && ble_cb.restart) { ble_cb.restart = false; NimBLEDevice::getScan()->start(0, false, true); }
    if (now - last_tick >= 1000) { last_tick = now; vic_tick(); sens_tick(); }
    if (!ble_off) bank_service();
    if (now - last_link > 2000) {
      last_link = now;
      update_link();
      bool c; { Lock l; c = S.wifi; }
      if (c && !was_online) want_weather = true;
      was_online = c;
      if (!c) {
        if (!lost_at) lost_at = now;
        if (now - lost_at > 60000 && !scan_running) { lost_at = now; join_best_known(); }   // camper: look for wifi each minute
      } else lost_at = 0;
    }
    bool portal; { Lock l; portal = S.portal; }
    place_service();
    if (want_weather || (was_online && now - last_wx > (portal ? 2 : 30) * 60000)) {
      want_weather = false; last_wx = now;
      weather_fetch();
    }
    delay(20);
  }
}

void radio_begin() {
  mtx = xSemaphoreCreateMutex();
  prefs.begin("wifi", false);
  vic_begin();
  sens_begin();
  xTaskCreatePinnedToCore(task, "radio", 9216, nullptr, 1, &task_radio, 0);
}

}  // namespace app
