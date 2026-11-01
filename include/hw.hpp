// CYD ESP32-2432S028 hardware: ILI9342 320x240 panel, XPT2046 touch, backlight, RGB LED.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9342 _panel;
  lgfx::Bus_SPI _bus;

 public:
  LGFX() {
    auto b = _bus.config();
    b.spi_host = HSPI_HOST;
    b.spi_mode = 0;
    b.freq_write = 55000000;   // MicroPython ran at 20 MHz; 40 MHz safe, 55 tested
    b.freq_read = 16000000;
    b.spi_3wire = false;
    b.use_lock = true;
    b.dma_channel = SPI_DMA_CH_AUTO;
    b.pin_sclk = 14;
    b.pin_mosi = 13;
    b.pin_miso = 12;
    b.pin_dc = 2;
    _bus.config(b);
    _panel.setBus(&_bus);

    auto p = _panel.config();
    p.pin_cs = 15;
    p.pin_rst = -1;            // no reset GPIO, init does a software reset
    p.pin_busy = -1;
    p.offset_rotation = 0;
    p.readable = true;
    p.invert = false;
    p.rgb_order = false;
    p.dlen_16bit = false;
    p.bus_shared = false;
    _panel.config(p);
    setPanel(&_panel);
  }
};

// ---- touch: XPT2046 on its own pins, bit-banged exactly like the calibrated MicroPython SoftSPI ----
namespace touch {
constexpr int SCK = 25, MOSI = 32, MISO = 39, CS = 33, IRQ = 36;

inline void begin() {
  pinMode(SCK, OUTPUT); pinMode(MOSI, OUTPUT); pinMode(CS, OUTPUT);
  pinMode(MISO, INPUT); pinMode(IRQ, INPUT);
  digitalWrite(CS, HIGH); digitalWrite(SCK, LOW);
}

inline uint16_t cmd(uint8_t c) {
  uint8_t out[3] = {c, 0, 0}, in[3] = {0, 0, 0};
  digitalWrite(CS, LOW);
  for (int i = 0; i < 3; i++) {
    for (int bit = 7; bit >= 0; bit--) {
      digitalWrite(MOSI, (out[i] >> bit) & 1);
      delayMicroseconds(1);
      digitalWrite(SCK, HIGH);
      in[i] = (in[i] << 1) | digitalRead(MISO);
      delayMicroseconds(1);
      digitalWrite(SCK, LOW);
    }
  }
  digitalWrite(CS, HIGH);
  return ((in[1] << 8) | in[2]) >> 3;
}

// calibrated 2026-10-30: axes swapped vs display
inline bool read(int16_t &sx, int16_t &sy) {
  if (digitalRead(IRQ)) return false;
  uint16_t xs[7], ys[7];
  int n = 0;
  for (; n < 7; n++) {
    if (digitalRead(IRQ)) break;
    xs[n] = cmd(0xD0);
    ys[n] = cmd(0x90);
  }
  if (n < 3) return false;
  std::sort(xs, xs + n); std::sort(ys, ys + n);
  int rx = xs[n / 2], ry = ys[n / 2];
  int x = (ry - 172) * 320 / 3651, y = (rx - 321) * 240 / 3473;
  sx = x < 0 ? 0 : x > 319 ? 319 : x;
  sy = y < 0 ? 0 : y > 239 ? 239 : y;
  return true;
}
}  // namespace touch

// ---- RGB LED (active low) + backlight PWM ----
namespace led {
constexpr int R = 4, G = 16, B = 17;
inline void begin() { for (int p : {R, G, B}) { pinMode(p, OUTPUT); digitalWrite(p, HIGH); } }
inline void set(bool r, bool g, bool b) { digitalWrite(R, !r); digitalWrite(G, !g); digitalWrite(B, !b); }
}

namespace backlight {
constexpr int PIN = 21, CH = 0;
inline void begin() { ledcSetup(CH, 5000, 8); ledcAttachPin(PIN, CH); ledcWrite(CH, 255); }
inline void set(uint8_t v) { ledcWrite(CH, v); }
}

// ---- other CYD parts: microSD (own VSPI bus), light sensor, speaker plug, CN1 side plug (I2C for add-ons) ----
namespace sdcard { constexpr int SCK = 18, MISO = 19, MOSI = 23, CS = 5; }
namespace ldr { constexpr int PIN = 34; }                       // analog, higher = darker
namespace speaker {
constexpr int PIN = 26, CH = 2;
inline void begin() { ledcSetup(CH, 2000, 8); ledcAttachPin(PIN, CH); ledcWrite(CH, 0); }
inline void tone(int hz) { if (hz) { ledcWriteTone(CH, hz); ledcWrite(CH, 96); } else ledcWrite(CH, 0); }
}
