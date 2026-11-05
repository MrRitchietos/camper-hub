// Level from a RuuviTag (Bluetooth, RAWv2 = data format 5: 3-axis acceleration in mG). No wires: put the tag
// anywhere in the camper that does not move (flat, on a wall, upside down: all fine).
// Pitch + = front up, roll + = right side up.
//
// Why a calibration: inside the tag the sensor axes are not lined up with the case, and the sensor itself is off
// by up to about 2 degrees. So the level works with the gravity VECTOR, not with the tag's own x/y:
//  1. measure where the camper is parked                                  -> gravity A
//  2. turn the camper around, wheels on the same spots, measure again    -> gravity B
//     the ground slope flips with the camper, the tag error does not: (A + B) / 2 = "level" for this camper
//     (skip = this spot is level, then A is "level")
//  3. raise only the front a bit (ramps), measure                         -> gravity C
//     the change C - B points to the front in the tag's own axes, so the tag may be turned any way
//     (skip = use the arrow direction from Settings > Level tag turned)
// Stored in NVS "level": u (level, 3 floats), f (front, 3 floats).
#include <Arduino.h>
#include <Preferences.h>
#include "internal.hpp"

namespace app {

struct V3 { float x, y, z; };
static V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 mul(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static float len(V3 a) { return sqrtf(dot(a, a)); }
static V3 unit(V3 a) { float l = len(a); return l > 1e-6f ? mul(a, 1 / l) : V3{0, 0, 1}; }

static Preferences lprefs;
static char use_addr[18] = "";
static uint32_t last_seen = 0;

// shared between the radio task (samples) and the UI (calibration): all under the state lock
static V3 filt{0, 0, 1}; static bool primed = false;
static V3 cal_u{0, 0, 1}, cal_f{1, 0, 0}; static bool have_u = false, have_f = false;
static constexpr int NS = 24;             // last samples, for a still measurement
static V3 ring[NS]; static uint32_t ring_at[NS]; static int ring_n = 0, ring_i = 0;
static V3 meas[3]; static bool meas_ok[3];

bool level_begin() {
  lprefs.begin("level", false);
  strlcpy(use_addr, lprefs.getString("tag", "").c_str(), sizeof use_addr);
  float b[3];
  if (lprefs.getBytes("u", b, sizeof b) == sizeof b) { cal_u = unit({b[0], b[1], b[2]}); have_u = true; }
  if (lprefs.getBytes("f", b, sizeof b) == sizeof b) { cal_f = unit({b[0], b[1], b[2]}); have_f = true; }
  if (lprefs.isKey("p")) { lprefs.remove("p"); lprefs.remove("r"); }       // old angle zero (before 1.2.0): not valid any more
  Lock l; S.lvl_cal = have_u ? (have_f ? 2 : 1) : 0;
  return true;
}

// front axis without step 3: the tag's x axis (turned in 90 degree steps by Settings), flattened onto the level
static V3 front_axis(V3 u) {
  V3 f = have_f ? cal_f : V3{1, 0, 0};
  if (!have_f) for (int k = 0; k < cfg.lvl_rot; k++) f = {f.y, -f.x, f.z};
  V3 p = sub(f, mul(u, dot(f, u)));
  if (len(p) < 0.3f) p = sub(V3{0, 1, 0}, mul(u, dot(V3{0, 1, 0}, u)));      // tag on a wall with x up: use y
  return unit(p);
}

static void angles() {           // under lock: filt -> S.pitch / S.roll
  V3 u = have_u ? cal_u : V3{0, 0, filt.z < 0 ? -1.0f : 1.0f};
  V3 f = front_axis(u), r = cross(f, u);                 // right = front x up
  float gx = dot(filt, f), gy = dot(filt, r), gz = dot(filt, u);
  S.lvl_moved = have_u && gz < 0.985f;                  // more than 10 degree off: the tag itself was moved
  S.pitch = atan2f(gx, sqrtf(gy * gy + gz * gz)) * 57.2958f;
  S.roll = atan2f(gy, sqrtf(gx * gx + gz * gz)) * 57.2958f;
}

// Live value for levelling. The standard RuuviTag firmware measures only every 2.57 s (each value is sent twice,
// 1285 ms apart) and sends one raw sample (4 mG steps, about +-8 mG noise). So: the mean of the readings of the
// last 3 s (at least the last 3), and a real change starts the mean again, so the screen jumps right away
// instead of creeping. A faster tag (Ruuvi test firmware: a new value every 0.44 s) just averages more.
static constexpr int LW = 8;
static V3 lw[LW]; static uint32_t lw_at[LW]; static int lw_n = 0;
static V3 last_dev{0, 0, 0};
static uint32_t gap_ms = 2570, last_new = 0;

void level_accel(const char *addr, int ax, int ay, int az, int seq) {   // radio task, every RuuviTag advert
  if (demo_on) return;
  bool fresh_tag = false;
  if (!use_addr[0]) { strlcpy(use_addr, addr, sizeof use_addr); fresh_tag = true; }
  if (strcmp(addr, use_addr)) return;
  if (fresh_tag) lprefs.putString("tag", use_addr);
  static int last_seq = -1;
  if (seq >= 0 && seq == last_seq) return;               // the same measurement sent again
  last_seq = seq;
  V3 g{(float)ax, (float)ay, (float)az};
  float m = len(g);
  if (m < 700 || m > 1300) return;                       // bumped or moving: not gravity alone
  V3 n = mul(g, 1 / m);
  uint32_t now = millis();
  Lock l;
  ring[ring_i] = n; ring_at[ring_i] = now; ring_i = (ring_i + 1) % NS; if (ring_n < NS) ring_n++;
  if (last_new && now - last_new < 10000) gap_ms = (gap_ms * 3 + (now - last_new)) / 4;
  last_new = now;
  V3 dev = sub(n, filt);
  if (!primed || len(dev) > 0.025f) lw_n = 0;            // moved more than 1.4 degree: show the new spot at once
  else if (len(dev) > 0.010f && len(last_dev) > 0.010f && dot(dev, last_dev) > 0 && lw_n > 1) {   // twice the same way: keep the last 2
    lw[0] = lw[lw_n - 1]; lw_at[0] = lw_at[lw_n - 1]; lw_n = 1;
  }
  last_dev = primed ? dev : V3{0, 0, 0};
  int keep = 0;                                          // drop readings older than 3 s, but keep the last 3
  for (int i = 0; i < lw_n; i++) if (lw_n - i <= 2 || now - lw_at[i] < 3000) { lw[keep] = lw[i]; lw_at[keep] = lw_at[i]; keep++; }
  lw_n = keep;
  if (lw_n == LW) { memmove(lw, lw + 1, (LW - 1) * sizeof(V3)); memmove(lw_at, lw_at + 1, (LW - 1) * sizeof(uint32_t)); lw_n--; }
  lw[lw_n] = n; lw_at[lw_n] = now; lw_n++;
  V3 sum{0, 0, 0}; for (int i = 0; i < lw_n; i++) sum = add(sum, lw[i]);
  filt = unit(sum); primed = true;
  S.lvl_n++; S.lvl_gap = gap_ms;
  last_seen = millis();
  S.lvl_present = true; strlcpy(S.lvl_tag, use_addr, sizeof S.lvl_tag);
  angles();
}

// UI: average of the readings taken since the tap on Measure (never older ones, from while the van was moving),
// at least 10 and none more than 1.4 degree off the average (else the van is not standing still yet)
int level_measure(int step, uint32_t since) {    // 0 ok, 1 not enough readings yet, 2 moving, 3 front not raised enough
  if (step < 0 || step > 2) return 1;
  Lock l;
  V3 sum{0, 0, 0}; int n = 0;
  for (int i = 0; i < ring_n; i++) if ((int32_t)(ring_at[i] - since) >= 0) { sum = add(sum, ring[i]); n++; }
  if (n < 10) return 1;
  V3 avg = unit(sum);
  for (int i = 0; i < ring_n; i++) if ((int32_t)(ring_at[i] - since) >= 0 && len(sub(ring[i], avg)) > 0.025f) return 2;
  if (step == 2) {
    V3 u = meas_ok[1] ? unit(add(meas[0], meas[1])) : meas[0];
    V3 ref = meas_ok[1] ? meas[1] : meas[0];
    V3 d = sub(avg, ref); d = sub(d, mul(u, dot(d, u)));
    if (len(d) < 0.017f) return 3;                       // needs about 1 degree
  }
  meas[step] = avg; meas_ok[step] = true;
  return 0;
}
void level_cal_start() { Lock l; meas_ok[0] = meas_ok[1] = meas_ok[2] = false; }
void level_cal_skip(int step) { Lock l; if (step >= 0 && step < 3) meas_ok[step] = false; }

void level_cal_save() {          // after the steps: store what was measured
  float bu[3], bf[3]; bool fu = false, ff = false;
  {
    Lock l;
    if (!meas_ok[0]) return;
    cal_u = meas_ok[1] ? unit(add(meas[0], meas[1])) : meas[0]; have_u = true;
    if (meas_ok[2]) {
      V3 ref = meas_ok[1] ? meas[1] : meas[0], d = sub(meas[2], ref);
      cal_f = unit(sub(d, mul(cal_u, dot(d, cal_u)))); have_f = true; ff = true;   // up gains a front part when the front rises
    }
    bu[0] = cal_u.x; bu[1] = cal_u.y; bu[2] = cal_u.z; bf[0] = cal_f.x; bf[1] = cal_f.y; bf[2] = cal_f.z; fu = true;
    S.lvl_cal = have_f ? 2 : 1;
    angles();
  }
  if (fu) lprefs.putBytes("u", bu, sizeof bu);
  if (ff) lprefs.putBytes("f", bf, sizeof bf);
  log_event("level calibrated (%s, %s)", meas_ok[1] ? "turned around" : "level spot", ff ? "front measured" : "front from the arrow");
}

void level_cal_clear() {
  { Lock l; have_u = have_f = false; S.lvl_cal = 0; angles(); }
  lprefs.remove("u"); lprefs.remove("f");
  log_event("level calibration removed");
}

void level_raw(char *out, size_t n) {       // serial "lvl": state + noise of the last samples (mG)
  Lock l;
  V3 m{0, 0, 0}; for (int i = 0; i < ring_n; i++) m = add(m, ring[i]);
  m = mul(m, ring_n ? 1.0f / ring_n : 0); float sd = 0;
  for (int i = 0; i < ring_n; i++) sd += dot(sub(ring[i], m), sub(ring[i], m));
  sd = ring_n > 1 ? sqrtf(sd / (ring_n - 1)) * 1000 : 0;
  snprintf(out, n, "lvl tag=%s present=%d cal=%d filt=%.4f,%.4f,%.4f u=%.4f,%.4f,%.4f f=%.4f,%.4f,%.4f pitch=%.2f roll=%.2f samples=%d noise=%.1fmG",
           use_addr, S.lvl_present, S.lvl_cal, filt.x, filt.y, filt.z, cal_u.x, cal_u.y, cal_u.z, cal_f.x, cal_f.y, cal_f.z, S.pitch, S.roll, ring_n, sd);
}

void level_next_tag() {       // Settings: use the next RuuviTag heard
  char next[18] = "";
  {
    Lock l;
    std::vector<const Sensor *> tags;
    for (auto &s : S.sensors) if (!strcmp(s.kind, "Ruuvi")) tags.push_back(&s);
    if (tags.empty()) return;
    int i = 0;
    while (i < (int)tags.size() && strcmp(tags[i]->addr, use_addr)) i++;
    strlcpy(next, tags[(i + 1) % tags.size()]->addr, sizeof next);
    if (!strcmp(next, use_addr)) return;
    strlcpy(use_addr, next, sizeof use_addr);
    primed = false; ring_n = 0; lw_n = 0; S.lvl_present = false; strlcpy(S.lvl_tag, use_addr, sizeof S.lvl_tag);
  }
  lprefs.putString("tag", next);
}

volatile uint32_t level_open_at = 0;   // radio: listen all the time while the Level page is open
void level_poll() {           // UI: demo motion + drop the level when the tag goes quiet
  level_open_at = millis();
  if (demo_on) {
    float s = millis() / 1000.0f;
    Lock l; S.lvl_present = true; S.pitch = 1.6f * sinf(s * 0.31f) + 0.4f; S.roll = -0.9f + 1.1f * sinf(s * 0.23f + 1);
    strcpy(S.lvl_tag, "demo");
    return;
  }
  Lock l;
  if (S.lvl_present && millis() - last_seen > 30000) S.lvl_present = false;
}

void level_rot_changed() { Lock l; angles(); }

}  // namespace app
