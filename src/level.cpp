// Level from a RuuviTag (Bluetooth, RAWv2 = data format 5: 3-axis acceleration in mG). No wires: lay the tag
// flat somewhere in the camper. Pitch + = front up, roll + = right side up. "Zero" stores the mounting offset.
// The first RuuviTag heard is used, or the one picked in Settings (cycles through the tags heard).
#include <Arduino.h>
#include <Preferences.h>
#include "internal.hpp"

namespace app {

static Preferences lprefs;
static float off_p = 0, off_r = 0, fp = 0, fr = 0;
static char use_addr[18] = "";
static uint32_t last_seen = 0;
static bool primed = false;

bool level_begin() {
  lprefs.begin("level", false);
  off_p = lprefs.getFloat("p", 0); off_r = lprefs.getFloat("r", 0);
  strlcpy(use_addr, lprefs.getString("tag", "").c_str(), sizeof use_addr);
  return true;
}

void level_accel(const char *addr, int ax, int ay, int az) {   // radio task, every RuuviTag advert
  if (demo_on) return;
  if (!use_addr[0]) { strlcpy(use_addr, addr, sizeof use_addr); lprefs.putString("tag", use_addr); }
  if (strcmp(addr, use_addr)) return;
  float x = ax, y = ay;
  for (int k = 0; k < cfg.lvl_rot; k++) { float t = x; x = -y; y = t; }     // tag turned in 90 degree steps
  float p = atan2f(x, sqrtf(y * y + (float)az * az)) * 57.2958f;
  float r = atan2f(y, sqrtf(x * x + (float)az * az)) * 57.2958f;
  if (!primed) { fp = p; fr = r; primed = true; } else { fp += (p - fp) * 0.5f; fr += (r - fr) * 0.5f; }
  last_seen = millis();
  Lock l; S.lvl_present = true; S.pitch = fp - off_p; S.roll = fr - off_r; strlcpy(S.lvl_tag, use_addr, sizeof S.lvl_tag);
}

void level_next_tag() {       // Settings: use the next RuuviTag heard
  Lock l;
  std::vector<const Sensor *> tags;
  for (auto &s : S.sensors) if (!strcmp(s.kind, "Ruuvi")) tags.push_back(&s);
  if (tags.empty()) return;
  int i = 0;
  while (i < (int)tags.size() && strcmp(tags[i]->addr, use_addr)) i++;
  strlcpy(use_addr, tags[(i + 1) % tags.size()]->addr, sizeof use_addr);
  lprefs.putString("tag", use_addr);
  primed = false; S.lvl_present = false; strlcpy(S.lvl_tag, use_addr, sizeof S.lvl_tag);
}

void level_poll() {           // UI: demo motion + drop the level when the tag goes quiet
  if (demo_on) {
    float s = millis() / 1000.0f;
    Lock l; S.lvl_present = true; S.pitch = 1.6f * sinf(s * 0.31f) + 0.4f; S.roll = -0.9f + 1.1f * sinf(s * 0.23f + 1);
    strcpy(S.lvl_tag, "demo");
    return;
  }
  if (S.lvl_present && millis() - last_seen > 30000) { Lock l; S.lvl_present = false; }
}

void level_zero() {
  off_p = fp; off_r = fr;
  lprefs.putFloat("p", off_p); lprefs.putFloat("r", off_r);
}

}  // namespace app
