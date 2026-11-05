// Data shared between the radio/sensor tasks (core 0) and the UI (core 1). Lock with app::Lock.
#pragma once
#include <Arduino.h>
#include <string>
#include <vector>

namespace app {

// ---- Victron SmartSolar (BLE instant readout) + derived day stats. Raw units: NA = 0x7FFF / 0xFFFF / 0x1FF.
constexpr int VHIST = 60;
struct Victron {
  bool have_key = false;
  bool seen = false, key_bad = false, decoded = false;
  char addr[18] = ""; uint16_t model = 0; int rssi = 0; uint32_t seen_at = 0, at = 0;
  uint8_t state = 0, error = 0;
  int16_t vbat_cv = 0x7FFF, ibat_da = 0x7FFF;          // 0.01 V, 0.1 A
  uint16_t yield_10wh = 0xFFFF, pv_w = 0xFFFF, load_da = 0x1FF;
  // current solar day (reset when the charger's day counter resets at sunrise)
  uint16_t peak_w = 0; char peak_at[6] = "";
  float ah_in = 0, ah_load = 0; int16_t vmin_cv = 0, vmax_cv = 0;
  uint32_t sec_bulk = 0, sec_abs = 0, sec_float = 0, state_since = 0;
  uint8_t last_error = 0; char last_error_at[6] = "";
  uint16_t hist[VHIST]; int hist_n = 0; uint32_t hist_ver = 0;   // pv bars, one per minute
  uint8_t batt = 0;            // 0 AGM/lead, 1 gel, 2 LiFePO4
  int16_t alarm_cv = 1180;     // 0 = off
  uint32_t ver = 0;
};
// one finished solar day (sunrise to sunrise) as stored in /camper/days.csv
struct DayRec {
  char date[11]; uint16_t yield_10wh, peak_w; float ah_in, ah_load;
  int16_t vmin_cv, vmax_cv; uint16_t bulk_m, abs_m, float_m;
};

// ---- Bluetooth thermometers (BTHome v2, pvvx / ATC custom firmware)
enum Role : uint8_t { R_NONE, R_INSIDE, R_OUTSIDE, R_FRIDGE, R_FREEZER, R_OTHER, R_COUNT };
struct Sensor {
  char addr[18]; char kind[10]; uint8_t role;
  float t = NAN, h = NAN, tmin = NAN, tmax = NAN, hpa = NAN; int batt = -1; int rssi = 0; uint32_t seen = 0;
  float hpa_hist[19]; int hpa_n = 0;              // one sample per 10 min: 3 h trend
  uint32_t warm_since = 0;                        // fridge / freezer above its limit since
};

// ---- weather (Open-Meteo, when online; the last forecast is kept on SD and shown offline)
struct WxH { int16_t t10; uint8_t rain, wind, gust, code; };
constexpr int WX_DAYS = 7, WX_HOURS = 48;
struct Wx {
  bool ok = false; uint32_t fetched = 0;      // epoch of the download (0 = unknown)
  char city[24] = ""; float lat = 0, lon = 0;
  float t = 0, hum = 0, wind = 0, gust = 0, pres = 0; int code = 0; bool day = true;
  int nd = 0; uint8_t d_code[WX_DAYS]; int8_t d_max[WX_DAYS], d_min[WX_DAYS]; uint8_t d_rain[WX_DAYS], d_gust[WX_DAYS];
  float d_sum[WX_DAYS], d_uv[WX_DAYS]; char d_rise[WX_DAYS][6], d_set[WX_DAYS][6], d_name[WX_DAYS][4];
  int nh = 0; uint8_t h0 = 0; WxH h[WX_HOURS];  // h0 = hour of day of h[0]
  char warn[64] = ""; uint8_t warn_lvl = 0;   // 0 none, 1 note, 2 warning
};

// ---- Fossibot / BrightEMS power station (BLE GATT, Modbus-like registers; separate from the solar system)
struct Bank {
  bool seen = false, connected = false; char addr[18] = ""; char name[24] = ""; int rssi = 0; char status[40] = "searching";
  bool data = false; uint32_t at = 0;
  float soc = -1; int in_ac = 0, in_dc = 0, in_w = 0, out_w = 0, ac_out_w = 0; float ac_v = 0;
  int to_full_min = 0, remain_min = 0;
  bool usb = false, dc = false, ac = false, light = false;
  uint8_t light_mode = 0;                       // 0 off, 1 on, 2 SOS, 3 strobe
  float usb_a1 = 0, usb_a2 = 0, usb_c1 = 0, usb_c2 = 0, car_w = 0;   // per-port watts
  float grid_v = 0, grid_hz = 0; int ac_level = 0; float cap_ah = 0;
  bool charging = false, discharging = false, full = false, fault = false;
  // settings (holding registers)
  bool s_ok = false; int s_dc_amp = 0, s_usb_sb = 0, s_ac_sb = 0, s_dc_sb = 0, s_screen = 0, s_idle = 0, s_aclvl = 0;
  bool s_key = false, s_silent = false; int s_floor = 0, s_ceil = 1000; char serial[17] = "";
  uint32_t ver = 0;
};

struct Ap { char ssid[33]; int8_t rssi; bool open; };

struct State {
  // wifi
  bool wifi = false, online = false, portal = false; char ssid[33] = ""; int8_t rssi = 0; char wifi_status[40] = "starting"; uint32_t ip = 0;
  std::vector<Ap> aps; bool scanning = false; uint32_t scan_ver = 0;
  // clock: ok = set, est = only guessed from the last time saved on SD; src = where the time came from (CLK_*)
  bool clock_ok = false, clock_est = false; uint8_t clock_src = 0;
  // sd
  bool sd_ok = false; uint32_t sd_used_kb = 0, sd_total_mb = 0, sd_rows = 0; char sd_msg[40] = "no card";
  // modules
  Victron vic;
  std::vector<DayRec> days; uint32_t days_ver = 0;          // newest last, up to 60
  std::vector<Sensor> sensors; uint32_t sens_ver = 0;
  Wx wx; uint32_t wx_ver = 0;
  Bank bank;
  bool lvl_present = false; float pitch = 0, roll = 0;     // degrees, + = front up / right up
  char lvl_tag[18] = "";                                   // RuuviTag used for the level
  uint8_t lvl_cal = 0;
  uint16_t lvl_n = 0; uint32_t lvl_gap = 0; bool lvl_moved = false;   // moved = the tag itself was turned: calibrate again                // new level readings so far, ms between them                                     // 0 not calibrated, 1 level only, 2 level + front
  bool demo = false;
};

extern State S;
extern SemaphoreHandle_t mtx;
struct Lock { Lock() { xSemaphoreTake(mtx, portMAX_DELAY); } ~Lock() { xSemaphoreGive(mtx); } };
extern volatile bool demo_on;

// radio.cpp
void radio_begin();
void request_wifi_scan();
void connect_to(const char *ssid, const char *pass);
void set_demo(bool on);
// victron.cpp
int vic_set_key(const char *hex);       // 32 hex chars: 1 saved, 0 not valid, -1 locked (a key is already stored)
void vic_forget_key();                  // the only way to unlock: remove the stored key on purpose

void vic_settings(uint8_t batt, int16_t alarm_cv);
const char *vic_model(uint16_t id);
const char *vic_state(uint8_t s);
const char *vic_error(uint8_t e);
int vic_soc(const Victron &v);          // battery % estimate, -1 = unknown
// sensors.cpp
void sens_set_role(const char *addr, uint8_t role);
const char *role_name(uint8_t r);
const Sensor *sens_by_role(uint8_t r);  // call with the lock held
// level.cpp
bool level_begin();
void level_poll();                      // UI loop while the Level tab is open
void level_next_tag();
void level_cal_start();                 // calibration steps, see level.cpp
int level_measure(int step, uint32_t since);   // readings since millis() 'since': 0 ok, 1 not enough yet, 2 moving, 3 front not raised enough
void level_cal_skip(int step);
void level_cal_save();
void level_cal_clear();
void level_rot_changed();
void level_raw(char *out, size_t n);
// fossibot.cpp
void bank_switch(int output, bool on);   // 0 USB, 1 DC, 2 AC, 3 light
bool bank_set(uint16_t reg, uint16_t val);
void bank_forget();                       // pair again with the next power bank close by   // guarded settings write (false = refused)
// logger.cpp
void logger_begin();
// weather place: auto (from the internet connection) or pinned to a town
struct Place { char name[32] = ""; char area[28] = ""; float lat = 0, lon = 0; };
constexpr int PLACE_HITS = 6, PLACE_RECENT = 8;
struct Places {
  bool pinned = false; Place cur;                 // cur = pinned place (when pinned)
  int n_hits = -1; Place hits[PLACE_HITS];        // -1 = no search yet, 0 = nothing found
  bool searching = false; char err[32] = "";
  int n_recent = 0; Place recent[PLACE_RECENT];   // from the SD card, newest first
  uint32_t ver = 0;
};
extern Places P;
void place_search(const char *q);   // runs in the radio task
void place_pin(const Place &p);     // also refreshes the forecast
void place_auto();
enum { CLK_NONE, CLK_SD, CLK_HAND, CLK_PHONE, CLK_WIFI, CLK_NET };   // better source = higher
void clock_set(long epoch, uint8_t src);   // any task; ignored when a better source already set it (by hand always works)
const char *clock_src_name(uint8_t src);
void log_event(const char *fmt, ...);   // -> /camper/events.csv (any task)
float hpa_trend(const Sensor &s);       // hPa change over the last 3 h (NAN = not enough data)
// settings (main.cpp)
struct Settings {
  uint8_t bright = 0; bool beep = true;
  uint16_t len_cm = 404, width_cm = 180;   // WHEELBASE and TRACK (Fiat Ducato maxi: 4035 / ~1800 mm)
  uint8_t lvl_rot = 0;
  uint8_t saver = 3, scr_off = 2;          // index into SAVER_MIN / OFF_MIN
  uint8_t look_mode = 0;                   // screen look: 0 auto (day by sun), 1 always day, 2 always night
  uint8_t saver_look = 0;                  // 0 camper scene, 1 photos from the SD card
  bool web = false, ap = false;            // phone page; the board's own WiFi "CamperHub"
};
constexpr uint8_t SAVER_MIN[] = {0, 1, 2, 5, 10, 30};     // 0 = no screensaver (just dim after 5 min)
constexpr uint8_t OFF_MIN[] = {0, 15, 30, 60, 120};       // screen fully dark this long after the saver (0 = never)
extern Settings cfg;

// ---- trip diary (store.cpp): one row per day in /camper/trips.csv
struct TripDay {
  char date[11] = ""; char place[24] = ""; float lat = 0, lon = 0;
  uint8_t code = 0; float rain = 0; int8_t wmax = 0, wmin = 0;   // forecast of that day
  float omin = NAN, omax = NAN, imin = NAN, imax = NAN;          // measured outside / inside
  float vmin = NAN, kwh = NAN; int bmin = -1;                    // camper battery, solar, power bank
};
extern std::vector<TripDay> trips; extern TripDay trip_today; extern uint32_t trips_ver;   // newest first, lock
void trips_request(bool load);                 // load the diary from the card (true) or free the RAM (false)
// ---- SD tools
enum SdJob : uint8_t { J_NONE, J_BACKUP, J_RESTORE, J_UPDATE };
void sd_job(uint8_t job);                      // J_NONE = refresh SDI
struct SdInfo { bool backup = false, update = false; uint32_t update_kb = 0; char backup_at[20] = ""; char msg[48] = ""; int files = 0; uint32_t ver = 0; };
extern SdInfo SDI;
int photos_count(); void photos_drop_cache(const char *path);
// ---- phone page (web.cpp); changes come to the UI task through a queue
struct WebCmd { char op[8]; char key[12]; int a, b; char text[72]; };
void web_begin_code(); const char *web_code(); bool web_set_pass(const char *p); void ap_new_pass();
void web_enable(bool on); bool web_on();
bool web_cmd_pop(WebCmd &c);
void ap_enable(bool on);                       // radio.cpp
void save_settings();
void screen_wake();
void beep(int pattern);                 // 1 short, 2 alarm

}  // namespace app
