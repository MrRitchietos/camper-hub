// Fossibot (BrightEMS / SYDPOWER) power station over BLE GATT, after ylianst/esp-fbot:
// service 0xA002, write 0xC304, notify 0xC305. Read 80 input registers: 11 04 0000 0050 + CRC16/Modbus (high
// byte first). Reply: 11 04 .. registers from byte 6, big endian. No pairing. Only one phone/board at a time.
#include <NimBLEDevice.h>
#include "internal.hpp"

namespace app {

static NimBLEAddress fb_addr;
static volatile bool fb_found = false;
static NimBLEClient *cli = nullptr;
static NimBLERemoteCharacteristic *wr = nullptr;
static uint8_t buf[200]; static size_t blen = 0;
static uint32_t last_poll = 0, last_try = 0, last_rx = 0;
void bank_reset_timers();
// write queue: the F1200 has none, so writes go out >= 300 ms apart, settings are re-read after each one
struct W { uint16_t reg, val; };
static W wq[8]; static volatile int wq_n = 0;
static portMUX_TYPE wq_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t last_write = 0, last_set_read = 0, reread_at = 0;

static uint16_t crc16(const uint8_t *d, int n) {
  uint16_t c = 0xFFFF;
  for (int i = 0; i < n; i++) { c ^= d[i]; for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xA001 : c >> 1; }
  return c;
}
static void send(uint8_t fn, uint16_t reg, uint16_t val) {
  uint8_t f[8] = {0x11, fn, (uint8_t)(reg >> 8), (uint8_t)reg, (uint8_t)(val >> 8), (uint8_t)val, 0, 0};
  uint16_t c = crc16(f, 6);
  f[6] = c >> 8; f[7] = c & 0xFF;
  if (wr) wr->writeValue(f, 8, false);
}

// guards from the research: never factory reset / debug regs, never 0 on the idle shutdown (bricked an F2400)
static bool allowed(uint16_t reg, uint16_t v) {
  switch (reg) {
    case 20: return v >= 1 && v <= 20;
    case 24: case 25: case 26: case 56: case 57: return v <= 1;
    case 27: return v <= 3;
    case 59: case 60: case 61: return v <= 1440;
    case 62: return v <= 600;
    case 66: return v <= 500;
    case 67: return v >= 600 && v <= 1000;
    case 68: return v >= 5 && v <= 1440;
    default: return false;
  }
}
bool bank_set(uint16_t reg, uint16_t val) {
  if (!allowed(reg, val)) return false;
  if (demo_on) {
    Lock l; Bank &b = S.bank;
    switch (reg) {
      case 24: b.usb = val; break; case 25: b.dc = val; break; case 26: b.ac = val; break;
      case 27: b.light_mode = val; b.light = val; break; case 20: b.s_dc_amp = val; break; case 56: b.s_key = val; break;
      case 57: b.s_silent = val; break; case 59: b.s_usb_sb = val; break; case 60: b.s_ac_sb = val; break;
      case 61: b.s_dc_sb = val; break; case 62: b.s_screen = val; break; case 66: b.s_floor = val; break;
      case 67: b.s_ceil = val; break; case 68: b.s_idle = val; break;
    }
    b.ver++;
    return true;
  }
  portENTER_CRITICAL(&wq_mux);
  bool ok = wq_n < 8;
  if (ok) wq[wq_n++] = {reg, val};
  portEXIT_CRITICAL(&wq_mux);
  return ok;
}
void bank_switch(int output, bool on) {
  if (output == 3) { bank_set(27, on ? 1 : 0); return; }
  bool cur; { Lock l; bool st[] = {S.bank.usb, S.bank.dc, S.bank.ac}; cur = st[output]; }
  if (cur != on) bank_set(24 + output, on ? 1 : 0);      // AC/DC toggle on ANY write on some firmware: only write a change
}

void bank_seen(const void *p) {
  if (fb_found || demo_on) return;
  auto *d = (const NimBLEAdvertisedDevice *)p;
  std::string n = d->haveName() ? d->getName() : std::string();
  bool named = n.rfind("FOSSIBOT", 0) == 0 || n.rfind("POWER", 0) == 0 || n.rfind("SYDPOWER", 0) == 0;
  if (!named && !d->isAdvertisingService(NimBLEUUID((uint16_t)0xA002))) return;
  fb_addr = d->getAddress();
  fb_found = true;
  Lock l;
  S.bank.seen = true; S.bank.rssi = d->getRSSI();
  strlcpy(S.bank.addr, fb_addr.toString().c_str(), sizeof S.bank.addr);
  strlcpy(S.bank.name, n.length() ? n.c_str() : "Power station", sizeof S.bank.name);
  strcpy(S.bank.status, "found, connecting");
  S.bank.ver++;
}

static void parse_settings() {
  if (blen < 6 + 80 * 2) return;
  auto reg = [&](int i) { return (uint16_t)(buf[6 + 2 * i] << 8 | buf[7 + 2 * i]); };
  Lock l;
  Bank &b = S.bank;
  b.s_aclvl = reg(13); b.s_dc_amp = reg(20); b.s_key = reg(56); b.s_silent = reg(57);
  b.s_usb_sb = reg(59); b.s_ac_sb = reg(60); b.s_dc_sb = reg(61); b.s_screen = reg(62);
  b.s_floor = reg(66); b.s_ceil = reg(67); b.s_idle = reg(68);
  for (int i = 0; i < 8; i++) { b.serial[i * 2] = reg(72 + i) >> 8; b.serial[i * 2 + 1] = reg(72 + i) & 0xFF; }
  b.serial[16] = 0;
  for (int i = 0; i < 16; i++) if (b.serial[i] && (b.serial[i] < 32 || b.serial[i] > 126)) b.serial[i] = '?';
  b.s_ok = true; b.ver++;
}
static void parse() {
  int count = 80;
  if (blen < (size_t)(6 + count * 2)) return;
  auto reg = [&](int i) { return (uint16_t)(buf[6 + 2 * i] << 8 | buf[7 + 2 * i]); };
  Lock l;
  Bank &b = S.bank;
  b.ac_level = reg(2); b.car_w = reg(9) / 10.0f;
  b.grid_v = reg(21) / 10.0f; b.grid_hz = reg(22) >= 1000 ? reg(22) / 100.0f : reg(22) / 10.0f;
  b.light_mode = reg(25);
  b.usb_a1 = reg(30) / 10.0f; b.usb_a2 = reg(31) / 10.0f; b.usb_c1 = reg(34) / 10.0f; b.usb_c2 = reg(35) / 10.0f;
  b.cap_ah = reg(54) / 10.0f;
  uint16_t bms = reg(48);
  b.charging = bms & 0x8000; b.discharging = bms & 0x4000; b.full = bms & 0x1000;
  b.fault = reg(43) || reg(44) || reg(45) || reg(46) || reg(50) || reg(51) || (bms & 0x3F);
  b.in_ac = reg(3); b.in_dc = reg(4); b.in_w = reg(6);
  b.ac_out_w = reg(20); b.out_w = reg(39);
  b.ac_v = reg(18) / 10.0f;
  uint16_t fl = reg(41);
  b.usb = fl & 512; b.dc = fl & 1024; b.ac = fl & 2048; b.light = fl & 4096;
  b.soc = reg(56) / 10.0f;
  b.to_full_min = reg(58); b.remain_min = reg(59);
  b.data = true; b.at = millis(); b.ver++;
  strcpy(b.status, "connected");
}
static void on_notify(NimBLERemoteCharacteristic *, uint8_t *d, size_t n, bool) {
  last_rx = millis();
  if (n >= 2 && d[0] == 0x11 && (d[1] & 0x7F) == 0x06) { reread_at = millis() + 600; return; }   // write echo
  if (n >= 2 && d[0] == 0x11 && (d[1] & 0x80)) return;                                         // refused
  if (n >= 2 && d[0] == 0x11 && (d[1] == 0x04 || d[1] == 0x03)) blen = 0;     // start of a reply
  if (blen + n > sizeof buf) blen = 0;
  memcpy(buf + blen, d, n); blen += n;
  if (buf[1] == 0x04) parse(); else if (buf[1] == 0x03) parse_settings();
}

static void set_status(const char *s, bool conn) {
  static bool was = false;
  if (conn != was) { was = conn; log_event("power bank %s", conn ? "connected" : "disconnected"); }
  Lock l; strlcpy(S.bank.status, s, sizeof S.bank.status); S.bank.connected = conn; S.bank.ver++; }

static void demo() {
  float s = millis() / 1000.0f;
  Lock l;
  Bank &b = S.bank;
  b.seen = b.connected = b.data = true; strcpy(b.name, "FOSSIBOT F1200"); strcpy(b.addr, "demo"); strcpy(b.status, "demo");
  b.in_dc = 60 + 40 * sinf(s * 0.05f); b.in_ac = 0; b.in_w = b.in_dc;
  b.ac_out_w = b.ac ? 38 + 10 * sinf(s * 0.3f) : 0;
  b.out_w = b.ac_out_w + (b.usb ? 12 : 0) + (b.dc ? 8 : 0) + (b.light ? 3 : 0);
  if (b.soc < 0) {
    b.soc = 72; b.ac = b.usb = true; b.s_ok = true; b.s_dc_amp = 8; b.s_key = true; b.s_usb_sb = 0; b.s_ac_sb = 960;
    b.s_dc_sb = 0; b.s_screen = 180; b.s_floor = 0; b.s_ceil = 1000; b.s_idle = 480; b.s_aclvl = 2; strcpy(b.serial, "F1200DEMO0001");
    b.cap_ah = 39.6f;
  }
  b.usb_a1 = b.usb ? 5.1f : 0; b.usb_c1 = b.usb ? 6.9f : 0; b.car_w = b.dc ? 8 : 0;
  b.charging = b.in_w > b.out_w; b.discharging = !b.charging;
  b.soc = constrain(b.soc + (b.in_w - b.out_w) / 1024.0f * 100 / 3600 * 20, 1.0f, 100.0f);
  b.ac_v = b.ac ? 230 : 0;
  int net = b.in_w - b.out_w;
  b.to_full_min = net > 0 ? (100 - b.soc) * 10.24f * 60 / net : 0;
  b.remain_min = net < 0 ? b.soc * 10.24f * 60 / -net : 0;
  b.at = millis(); b.ver++;
}

void bank_service() {
  uint32_t now = millis();
  if (demo_on) { if (now - last_poll > 1000) { last_poll = now; demo(); } return; }
  if (!fb_found) return;
  if (!cli || !cli->isConnected()) {
    wr = nullptr;
    if (now - last_try < 15000) return;
    last_try = now;
    set_status("connecting...", false);
    if (!cli) { cli = NimBLEDevice::createClient(); cli->setConnectTimeout(6000); }
    auto *sc = NimBLEDevice::getScan();
    sc->stop();
    bool ok = cli->connect(fb_addr);
    sc->start(0, false, true);
    if (!ok) { set_status("not reachable (app connected?)", false); return; }
    NimBLERemoteService *svc = cli->getService(NimBLEUUID((uint16_t)0xA002));
    NimBLERemoteCharacteristic *n = svc ? svc->getCharacteristic(NimBLEUUID((uint16_t)0xC305)) : nullptr;
    wr = svc ? svc->getCharacteristic(NimBLEUUID((uint16_t)0xC304)) : nullptr;
    if (!n || !wr || !n->subscribe(true, on_notify)) { set_status("unknown model (no A002 service)", false); cli->disconnect(); wr = nullptr; return; }
    set_status("connected, reading...", true);
    last_rx = now;
  }
  if (wq_n && now - last_write > 300) {
    W w; portENTER_CRITICAL(&wq_mux); w = wq[0]; memmove(wq, wq + 1, (wq_n - 1) * sizeof(W)); wq_n--; portEXIT_CRITICAL(&wq_mux);
    send(0x06, w.reg, w.val);
    last_write = now; reread_at = now + 800; last_poll = now - 1500;
    return;
  }
  if (now - last_write < 300) return;
  if ((reread_at && now > reread_at) || !last_set_read || now - last_set_read > 30000) {
    reread_at = 0; last_set_read = now; last_write = now; send(0x03, 0, 80); return;
  }
  if (now - last_poll > 2000) { last_poll = now; last_write = now; send(0x04, 0, 80); }
  if (now - last_rx > 20000) { set_status("no answer, reconnecting", false); cli->disconnect(); }
}

}  // namespace app
