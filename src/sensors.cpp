// Bluetooth thermometers, no pairing: BTHome v2 (service 0xFCD2, e.g. Shelly BLU H&T) and Xiaomi LYWSD03MMC
// & co. with pvvx / ATC custom firmware (service 0x181A). Stock Xiaomi firmware is encrypted: not supported.
#include <Arduino.h>
#include <Preferences.h>
#include "internal.hpp"

namespace app {

static Preferences sprefs;
static constexpr int MAX_SENS = 10;
static const char *ROLES[] = {"Sensor", "Inside", "Outside", "Fridge", "Freezer", "Other"};
const char *role_name(uint8_t r) { return r < R_COUNT ? ROLES[r] : "Sensor"; }

static void nvs_key(char *out, const char *addr) {     // "a4:c1:38:12:34:56" -> "a4c138123456"
  int n = 0;
  for (const char *p = addr; *p && n < 12; p++) if (*p != ':') out[n++] = *p;
  out[n] = 0;
}
void sens_begin() { sprefs.begin("sensors", false); }

void sens_set_role(const char *addr, uint8_t role) {
  char k[16]; nvs_key(k, addr);
  if (strcmp(addr, "demo") && strncmp(addr, "demo", 4)) sprefs.putUChar(k, role);
  Lock l;
  for (auto &s : S.sensors) {
    if (!strcmp(s.addr, addr)) s.role = role;
    else if (role != R_NONE && role != R_OTHER && s.role == role) s.role = R_NONE;   // one inside, one outside...
  }
  S.sens_ver++;
}
const Sensor *sens_by_role(uint8_t r) {
  const Sensor *best = nullptr;
  for (auto &s : S.sensors) if (s.role == r && millis() - s.seen < 15 * 60000) best = &s;
  return best;
}

static void update(const char *addr, const char *kind, float t, float h, int batt, int rssi) {
  Lock l;
  Sensor *s = nullptr;
  for (auto &x : S.sensors) if (!strcmp(x.addr, addr)) s = &x;
  if (!s) {
    if ((int)S.sensors.size() >= MAX_SENS) {           // drop the one not heard the longest
      auto old = std::min_element(S.sensors.begin(), S.sensors.end(), [](const Sensor &a, const Sensor &b) { return a.seen < b.seen; });
      S.sensors.erase(old);
    }
    Sensor n{};
    strlcpy(n.addr, addr, sizeof n.addr);
    char k[16]; nvs_key(k, addr);
    n.role = strncmp(addr, "demo", 4) ? sprefs.getUChar(k, R_NONE) : R_NONE;
    n.t = n.h = n.tmin = n.tmax = NAN; n.batt = -1;
    S.sensors.push_back(n);
    s = &S.sensors.back();
  }
  strlcpy(s->kind, kind, sizeof s->kind);
  if (!isnan(t)) {
    s->t = t;
    if (isnan(s->tmin) || t < s->tmin) s->tmin = t;
    if (isnan(s->tmax) || t > s->tmax) s->tmax = t;
  }
  if (!isnan(h)) s->h = h;
  if (batt >= 0) s->batt = batt;
  s->rssi = rssi; s->seen = millis();
  S.sens_ver++;
}

// BTHome v2 object lengths (id -> bytes), 0 = unknown: stop parsing
static int bth_len(uint8_t id) {
  if (id <= 0x01) return 1;
  if (id == 0x02 || id == 0x03 || id == 0x08 || id == 0x0C || id == 0x0D || id == 0x0E || id == 0x12 || id == 0x13 || id == 0x14) return 2;
  if (id == 0x04 || id == 0x05 || id == 0x0A || id == 0x0B || id == 0x42 || id == 0x4B) return 3;
  if (id >= 0x0F && id <= 0x2F) return 1;
  if (id == 0x3A || id == 0x46) return 1;
  if (id == 0x3C || id == 0x3D || id == 0x3F || id == 0x40 || id == 0x41 || id == 0x43 || id == 0x44 || id == 0x45 ||
      id == 0x47 || id == 0x48 || id == 0x49 || id == 0x4A || id == 0x51 || id == 0x52) return 2;
  if (id == 0x3E || (id >= 0x4C && id <= 0x50)) return 4;
  return 0;
}

void sens_parse(uint16_t uuid, const std::string &sd, const char *addr, int rssi) {
  if (demo_on) return;
  const uint8_t *d = (const uint8_t *)sd.data();
  int n = sd.size();
  if (uuid == 0xFCD2 && n >= 2) {                        // BTHome v2
    if (d[0] & 1) return;                                // encrypted
    if ((d[0] >> 5) != 2) return;
    float t = NAN, h = NAN; int batt = -1;
    for (int i = 1; i < n;) {
      uint8_t id = d[i++];
      int len = bth_len(id);
      if (!len || i + len > n) break;
      const uint8_t *v = d + i;
      if (id == 0x01) batt = v[0];
      else if (id == 0x02) t = (int16_t)(v[0] | v[1] << 8) * 0.01f;
      else if (id == 0x45) t = (int16_t)(v[0] | v[1] << 8) * 0.1f;
      else if (id == 0x03) h = (v[0] | v[1] << 8) * 0.01f;
      else if (id == 0x2E) h = v[0];
      i += len;
    }
    if (!isnan(t) || !isnan(h)) update(addr, "BTHome", t, h, batt, rssi);
  } else if (uuid == 0x181A && n == 15) {                // pvvx custom: mac(6, LE) t h mV % cnt flags
    float t = (int16_t)(d[6] | d[7] << 8) * 0.01f, h = (d[8] | d[9] << 8) * 0.01f;
    update(addr, "pvvx", t, h, d[12], rssi);
  } else if (uuid == 0x181A && n == 13) {                // ATC1441: mac(6, BE) t(BE, 0.1) h% batt% mV cnt
    float t = (int16_t)(d[6] << 8 | d[7]) * 0.1f;
    update(addr, "ATC", t, d[8], d[9], rssi);
  }
}

// RuuviTag RAWv2 (format 5), manufacturer 0x0499: temp 0.005 C, humidity 0.0025 %, pressure +50000 Pa,
// acceleration x/y/z in mG (all big endian), battery mV in the top 11 bits of the power field (+1600).
bool ruuvi_dbg = false;
void sens_ruuvi(const std::string &m, const char *addr, int rssi) {
  if (demo_on || m.size() < 2 + 24 || (uint8_t)m[2] != 0x05) return;
  const uint8_t *d = (const uint8_t *)m.data() + 3;
  auto s16 = [&](int i) { return (int16_t)(d[i] << 8 | d[i + 1]); };
  auto u16 = [&](int i) { return (uint16_t)(d[i] << 8 | d[i + 1]); };
  float t = s16(0) == (int16_t)0x8000 ? NAN : s16(0) * 0.005f;
  float h = u16(2) == 0xFFFF ? NAN : u16(2) * 0.0025f;
  int mv = (u16(12) >> 5) + 1600;
  int batt = constrain((mv - 2500) * 100 / (3000 - 2500), 0, 100);       // CR2477: ~3.0 V full, 2.5 V empty
  update(addr, "Ruuvi", t, h, batt, rssi);
  if (u16(4) != 0xFFFF) {                                                  // pressure for the storm trend
    Lock l; for (auto &x : S.sensors) if (!strcmp(x.addr, addr)) x.hpa = (u16(4) + 50000) / 100.0f;
  }
  if (s16(6) != (int16_t)0x8000) level_accel(addr, s16(6), s16(8), s16(10), u16(15) == 0xFFFF ? -1 : u16(15));
  if (ruuvi_dbg) Serial.printf("ruuvi %s %d %d %d mv %d mc %u seq %u\n", addr, s16(6), s16(8), s16(10), mv, d[14], (unsigned)u16(15));
}

// demo: inside warms in the afternoon, outside follows the weather, fridge cycles
static void demo(uint32_t s) {
  float ph = (s % 120) / 120.0f * 2 * PI;               // one "day" per 2 minutes, like the solar demo
  update("demo:inside", "BTHome", 19.5f + 2.5f * sinf(ph - 1.2f), 52 + 6 * sinf(ph), 87, -61);
  update("demo:outside", "pvvx", 9 + 6 * sinf(ph - 1.6f), 74 - 12 * sinf(ph - 1.6f), 64, -78);
  update("demo:fridge", "pvvx", 4.2f + 1.3f * sinf(s * 0.21f), 61, 41, -70);
  update("demo:ruuvi", "Ruuvi", 20.1f + sinf(ph), 49, 90, -66);
  { Lock l; for (auto &x : S.sensors) if (!strcmp(x.addr, "demo:ruuvi")) x.hpa = 1016 - (s % 600) * 0.012f; }
  Lock l;
  for (auto &x : S.sensors) {
    if (!strcmp(x.addr, "demo:inside")) x.role = R_INSIDE;
    if (!strcmp(x.addr, "demo:outside")) x.role = R_OUTSIDE;
    if (!strcmp(x.addr, "demo:fridge")) x.role = R_FRIDGE;
    if (!strcmp(x.addr, "demo:ruuvi")) x.role = R_OTHER;
  }
}

float hpa_trend(const Sensor &s) {
  if (s.hpa_n < 7 || isnan(s.hpa)) return NAN;               // at least 1 h of samples
  return s.hpa - s.hpa_hist[0];
}

void sens_tick() {
  static uint32_t last_p = 0;
  if (millis() - last_p > (demo_on ? 5000 : 600000) || !last_p) {      // pressure samples every 10 min
    last_p = millis();
    Lock l;
    for (auto &x : S.sensors) {
      if (isnan(x.hpa)) continue;
      if (x.hpa_n < 19) x.hpa_hist[x.hpa_n++] = x.hpa;
      else { memmove(x.hpa_hist, x.hpa_hist + 1, 18 * sizeof(float)); x.hpa_hist[18] = x.hpa; }
    }
  }
  static int last_yday = -1;
  static bool was_demo = false;
  if (was_demo != demo_on) { Lock l; S.sensors.clear(); S.sens_ver++; was_demo = demo_on; }
  if (demo_on) demo(millis() / 1000);
  struct tm t;
  if (clock_now(t) && t.tm_yday != last_yday) {         // min / max per calendar day
    if (last_yday >= 0) { Lock l; for (auto &s : S.sensors) { s.tmin = s.tmax = s.t; } S.sens_ver++; }
    last_yday = t.tm_yday;
  }
}

}  // namespace app
