// Victron SmartSolar "Instant Readout": decrypts the BLE advertisement (AES-128-CTR, key from VictronConnect).
// Layout after the company id 0x02E1: 0x10 0x02 | model id (LE) | record type | nonce (LE) | key[0] | ciphertext
// Verified against the victron-ble test vector (serial: vtest).
#include <Arduino.h>
#include <Preferences.h>
#include <mbedtls/aes.h>
#include "internal.hpp"

namespace app {

static Preferences vprefs;
static uint8_t key[16];
static volatile bool have_key = false;
static char day_date[11] = "";        // date the current solar day started


void vic_begin() {
  vprefs.begin("victron", false);
  have_key = vprefs.getBytes("key", key, 16) == 16;
  Lock l;
  S.vic.have_key = have_key;
  S.vic.batt = vprefs.getUChar("batt", 1);       // Ritchie: gel battery
  S.vic.alarm_cv = vprefs.getShort("alarm", 1180);
}

void vic_settings(uint8_t batt, int16_t alarm_cv) {
  vprefs.putUChar("batt", batt);
  vprefs.putShort("alarm", alarm_cv);
  Lock l; S.vic.batt = batt; S.vic.alarm_cv = alarm_cv; S.vic.ver++;
}

static void clear_live(Victron &v) {
  Victron keep = v;
  v = Victron{};
  v.have_key = keep.have_key; v.batt = keep.batt; v.alarm_cv = keep.alarm_cv;
  v.ver = keep.ver + 1;
}

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void vic_forget_key() {
  vprefs.remove("key"); have_key = false;
  log_event("Victron key removed");
  Lock l; clear_live(S.vic); S.vic.have_key = false;
}

// A stored key is locked: a new one is refused until the old one is removed on purpose
// (Settings > Victron key > Remove, the phone page Remove button, or serial "vkey remove").
int vic_set_key(const char *hex) {
  if (have_key) return -1;
  uint8_t k[16]; int n = 0, hi = -1;
  for (const char *p = hex; *p; p++) {
    int v = hexval(*p);
    if (v < 0) { if (*p == ' ' || *p == '-' || *p == ':') continue; return 0; }
    if (hi < 0) hi = v; else { if (n >= 16) return 0; k[n++] = hi << 4 | v; hi = -1; }
  }
  if (n != 16 || hi >= 0) return 0;
  memcpy(key, k, 16);
  vprefs.putBytes("key", key, 16);
  have_key = true;
  log_event("Victron key saved");
  Lock l; S.vic.have_key = true; S.vic.decoded = false; S.vic.key_bad = false; S.vic.ver++;
  return 1;
}

struct Bits {            // little-endian bit reader over the decrypted record
  const uint8_t *b = nullptr; int len = 0, pos = 0;
  uint32_t get(int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++, pos++)
      if (pos / 8 < len && (b[pos / 8] >> (pos % 8) & 1)) v |= 1u << i;
    return v;
  }
};

void vic_parse(const std::string &m, const char *addr, int rssi) {
  if (demo_on) return;
  if (m.size() < 12 || (uint8_t)m[0] != 0xE1 || (uint8_t)m[1] != 0x02 || (uint8_t)m[2] != 0x10) return;
  const uint8_t *d = (const uint8_t *)m.data() + 2;   // skip company id
  uint16_t model = d[2] | d[3] << 8;
  if (d[4] != 0x01) return;                           // 0x01 = solar charger
  uint16_t nonce = d[5] | d[6] << 8;
  int clen = m.size() - 2 - 8;
  if (clen <= 0 || clen > 16) return;

  bool match = have_key && d[7] == key[0];
  uint8_t plain[16] = {0};
  if (match) {
    uint8_t ctr[16] = {(uint8_t)(nonce & 0xFF), (uint8_t)(nonce >> 8)}, stream[16];
    size_t off = 0;
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, 128);
    mbedtls_aes_crypt_ctr(&aes, clen, &off, ctr, stream, d + 8, plain);
    mbedtls_aes_free(&aes);
  }

  Lock l;
  Victron &v = S.vic;
  bool same = !strcmp(v.addr, addr);
  if (!same && v.decoded && !match && millis() - v.at < 30000) return;   // keep the one we can read
  strlcpy(v.addr, addr, sizeof v.addr);
  v.model = model; v.rssi = rssi; v.seen = true; v.seen_at = millis();
  v.key_bad = have_key && !match;
  if (match) {
    Bits b; b.b = plain; b.len = clen;
    v.state = b.get(8);
    v.error = b.get(8);
    v.vbat_cv = (int16_t)b.get(16);
    v.ibat_da = (int16_t)b.get(16);
    v.yield_10wh = b.get(16);
    v.pv_w = b.get(16);
    v.load_da = b.get(9);
    v.decoded = true; v.at = millis();
  }
  v.ver++;
}

// ---------------------------------------------------------------- battery % estimate from voltage
struct VP { int16_t cv; int8_t pct; };
static const VP LEAD[] = {{1050, 0}, {1151, 10}, {1166, 20}, {1181, 30}, {1196, 40}, {1210, 50}, {1224, 60}, {1237, 70}, {1250, 80}, {1262, 90}, {1273, 100}};
static const VP LFP[] = {{1000, 0}, {1200, 4}, {1280, 10}, {1290, 20}, {1300, 30}, {1310, 40}, {1313, 50}, {1316, 60}, {1320, 70}, {1325, 80}, {1330, 90}, {1340, 99}, {1360, 100}};
static int curve(const VP *t, int n, int cv) {
  if (cv <= t[0].cv) return 0;
  for (int i = 1; i < n; i++)
    if (cv <= t[i].cv) return t[i - 1].pct + (cv - t[i - 1].cv) * (t[i].pct - t[i - 1].pct) / (t[i].cv - t[i - 1].cv);
  return 100;
}
int vic_soc(const Victron &v) {
  if (!v.decoded || v.vbat_cv == 0x7FFF) return -1;
  if (v.state == 5 || v.state == 6) return 100;
  int cv = v.vbat_cv, i = v.ibat_da == 0x7FFF ? 0 : v.ibat_da;
  bool v24 = cv > 1900;
  if (v24) cv /= 2;
  cv -= i * (v.batt == 2 ? 1 : 2) / (v24 ? 2 : 1);   // ~0.01-0.02 ohm: charging lifts the voltage
  int p = v.batt == 2 ? curve(LFP, 13, cv) : curve(LEAD, 11, cv - (v.batt == 1 ? 5 : 0));
  if (v.state == 4 || v.state == 246) p = constrain(p, 90, 99);
  return constrain(p, 0, 100);
}

// ---------------------------------------------------------------- day bookkeeping
static void close_day(Victron &v) {
  if (v.peak_w || v.ah_in > 0.05f || v.yield_10wh) {
    if (!demo_on) log_event("solar day closed: %d.%02d kWh", v.yield_10wh / 100, v.yield_10wh % 100);
    DayRec d{};
    strlcpy(d.date, day_date[0] ? day_date : "\?\?\?\?-\?\?-\?\?", sizeof d.date);
    d.yield_10wh = v.yield_10wh == 0xFFFF ? 0 : v.yield_10wh;
    d.peak_w = v.peak_w; d.ah_in = v.ah_in; d.ah_load = v.ah_load;
    d.vmin_cv = v.vmin_cv; d.vmax_cv = v.vmax_cv;
    d.bulk_m = v.sec_bulk / 60; d.abs_m = v.sec_abs / 60; d.float_m = v.sec_float / 60;
    logger_day_closed(d);
  }
  v.peak_w = 0; v.peak_at[0] = 0; v.ah_in = v.ah_load = 0; v.vmin_cv = v.vmax_cv = 0;
  v.sec_bulk = v.sec_abs = v.sec_float = 0;
  day_date[0] = 0;
}

// ---------------------------------------------------------------- demo: a fast sunny day with clouds
// One tick (1 s) = 12 simulated minutes: day 06:30-20:30 takes 70 s, the night 50 s.
static float d_min = 300, d_soc = 0.55f, d_cloud = 1, d_cloud_to = 1, d_load = 0.6f, d_yield = 0;
static int d_day = 0;
static float frand() { return (esp_random() & 0xFFFF) / 65535.0f; }
void vic_demo_reset() {
  d_min = 300; d_soc = 0.55f; d_yield = 0; d_day = 0;
  Lock l; clear_live(S.vic); day_date[0] = 0;
}
static void demo_date(char *out, int plus) {
  struct tm t;
  time_t now = time(nullptr) + plus * 86400;
  if (!clock_now(t)) now = 1793577600 + plus * 86400;   // 2026-11-01
  localtime_r(&now, &t);
  fmt_date(out, 11, t);
}
static void demo_step(Victron &v) {
  constexpr float STEP = 12, DT_H = STEP / 60;
  float prev = d_min;
  d_min += STEP; if (d_min >= 1440) d_min -= 1440;
  if (prev < 390 && d_min >= 390) { v.yield_10wh = d_yield * 100; close_day(v); d_yield = 0; d_day++; }
  if (frand() < 0.12f) d_cloud_to = frand() < 0.35f ? 0.25f + frand() * 0.4f : 0.85f + frand() * 0.15f;
  d_cloud += (d_cloud_to - d_cloud) * 0.5f;
  d_load += (frand() - 0.5f) * 0.3f; d_load = constrain(d_load, 0.2f, 1.6f);
  float sun = d_min > 390 && d_min < 1230 ? sinf(PI * (d_min - 390) / 840) : 0;
  float pv = 215 * powf(sun, 1.3f) * d_cloud;
  bool lfp = v.batt == 2;
  float rest = lfp ? 13.0f + d_soc * 0.35f : 11.6f + d_soc * 1.15f;
  float vb, ib; uint8_t st;
  if (pv < 3) { st = 0; ib = 0; pv = 0; vb = rest - d_load * 0.03f; d_soc -= d_load * DT_H / 100; }
  else {
    float avail = pv * 0.96f / 13.2f - d_load;
    if (d_soc < 0.88f) { st = 3; ib = avail; vb = min(lfp ? 14.2f : 14.4f, rest + 0.35f + ib * 0.03f); }
    else if (d_soc < 0.985f) { st = 4; ib = min(avail, (1 - d_soc) * 60 + 0.5f); vb = lfp ? 14.2f : 14.4f; }
    else { st = 5; ib = min(avail, 0.6f); vb = lfp ? 13.5f : 13.8f; }
    if (ib < avail) pv = (ib + d_load) * vb / 0.96f;            // charger throttles once full
    d_soc += ib * DT_H / 100;
  }
  d_soc = constrain(d_soc, 0.05f, 1.0f);
  d_yield += pv * DT_H / 1000;
  if (!day_date[0]) demo_date(day_date, d_day);
  v.decoded = v.seen = true; v.at = v.seen_at = millis(); v.rssi = -58; v.model = 0xA053;
  strcpy(v.addr, "demo");
  v.state = st; v.error = 0;
  v.vbat_cv = vb * 100; v.ibat_da = ib * 10; v.pv_w = pv; v.load_da = d_load * 10;
  v.yield_10wh = d_yield * 100;
}
static void sim_clock(char *out) { snprintf(out, 6, "%02d:%02d", (int)d_min / 60, (int)d_min % 60); }

// ---------------------------------------------------------------- 1 s tick: demo, day stats, history
void vic_tick() {
  static uint32_t last = 0, min_sum = 0, min_n = 0, min_start = 0;
  static uint16_t last_yield = 0xFFFF;
  static uint8_t last_state = 255;
  uint32_t now = millis();
  float dt_h = last ? (now - last) / 3600000.0f : 0;
  last = now;
  Lock l;
  Victron &v = S.vic;
  if (demo_on) { demo_step(v); dt_h = 12.0f / 60; }
  bool fresh = v.decoded && now - v.at < 15000;
  if (!fresh) { last_state = 255; return; }

  struct tm t; bool tm_ok = clock_now(t);
  if (!demo_on) {
    if (last_yield != 0xFFFF && v.yield_10wh != 0xFFFF && v.yield_10wh + 2 < last_yield) {   // charger started a new day
      uint16_t y = v.yield_10wh; v.yield_10wh = last_yield; close_day(v); v.yield_10wh = y;
    }
    if (!day_date[0] && tm_ok && !S.clock_est) fmt_date(day_date, sizeof day_date, t);   // not from a guessed clock
  }
  last_yield = v.yield_10wh;
  if (v.state != last_state) { v.state_since = now; last_state = v.state; }

  char hhmm[6] = "";
  if (demo_on) sim_clock(hhmm);
  else if (tm_ok) snprintf(hhmm, sizeof hhmm, "%02d:%02d", t.tm_hour, t.tm_min);
  if (v.pv_w != 0xFFFF && v.pv_w > v.peak_w) { v.peak_w = v.pv_w; strlcpy(v.peak_at, hhmm, sizeof v.peak_at); }
  if (v.ibat_da != 0x7FFF && v.ibat_da > 0) v.ah_in += v.ibat_da / 10.0f * dt_h;
  if (v.load_da != 0x1FF) v.ah_load += v.load_da / 10.0f * dt_h;
  if (v.vbat_cv != 0x7FFF) {
    if (!v.vmin_cv || v.vbat_cv < v.vmin_cv) v.vmin_cv = v.vbat_cv;
    if (v.vbat_cv > v.vmax_cv) v.vmax_cv = v.vbat_cv;
  }
  uint32_t secs = demo_on ? 720 : (uint32_t)(dt_h * 3600 + 0.5f);
  if (v.state == 3) v.sec_bulk += secs; else if (v.state == 4 || v.state == 246) v.sec_abs += secs;
  else if (v.state == 5 || v.state == 6) v.sec_float += secs;
  if (v.error && v.error != v.last_error) { v.last_error = v.error; strlcpy(v.last_error_at, hhmm, sizeof v.last_error_at); }

  min_sum += v.pv_w == 0xFFFF ? 0 : v.pv_w; min_n++;
  if (!min_start) min_start = now;
  if (demo_on || now - min_start >= 60000) {
    uint16_t w = min_sum / min_n;
    if (v.hist_n < VHIST) v.hist[v.hist_n++] = w;
    else { memmove(v.hist, v.hist + 1, (VHIST - 1) * sizeof v.hist[0]); v.hist[VHIST - 1] = w; }
    v.hist_ver++;
    min_sum = min_n = 0; min_start = now;
  }
  v.ver++;
}

const char *vic_model(uint16_t id) {
  switch (id) {
    case 0xA053: case 0xA075: return "SmartSolar MPPT 75/15";
    case 0xA054: case 0xA074: return "SmartSolar MPPT 75/10";
    case 0xA055: case 0xA076: return "SmartSolar MPPT 100/15";
    case 0xA056: return "SmartSolar MPPT 100/30";
    case 0xA057: return "SmartSolar MPPT 100/50";
    case 0xA042: return "MPPT 75/15";
    case 0xA043: return "MPPT 100/15";
    case 0xA044: return "MPPT 100/30";
    case 0xA04C: return "MPPT 75/10";
    default: return nullptr;
  }
}
const char *vic_state(uint8_t s) {
  switch (s) {
    case 0: return "Off"; case 1: return "Low power"; case 2: return "Fault"; case 3: return "Bulk";
    case 4: return "Absorption"; case 5: return "Float"; case 6: return "Storage"; case 7: return "Equalize";
    case 245: return "Starting"; case 246: return "Re-absorb"; case 247: return "Auto equalize";
    case 248: return "BatterySafe"; case 252: return "Ext. control";
    default: return "?";
  }
}
const char *vic_error(uint8_t e) {
  switch (e) {
    case 0: return nullptr;
    case 2: return "Battery voltage too high"; case 17: return "Charger too hot"; case 18: return "Charger over current";
    case 19: return "Charger current reversed"; case 20: return "Bulk time limit hit"; case 21: return "Current sensor issue";
    case 26: return "Terminals overheated"; case 28: return "Power stage issue"; case 33: return "Solar voltage too high";
    case 34: return "Solar current too high"; case 38: case 39: return "Input shut down";
    case 65: case 66: return "Communication lost"; case 67: return "BMS connection lost";
    case 116: return "Calibration lost"; case 117: return "Invalid firmware"; case 119: return "Settings invalid";
    default: return "Charger error";
  }
}

}  // namespace app
