// microSD logger (own task, core 0, low priority so SD stalls never touch the UI or the radio):
//  /camper/min-YYYY-MM-DD.csv  one row per minute with everything (store.cpp)
//  /camper/days.csv            one row per solar day (sunrise to sunrise)
//  /camper/clock.txt           last known time: keeps the clock going without internet (marked estimated)
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <sys/time.h>
#include "internal.hpp"
#include "hw.hpp"

namespace app {

static SPIClass sd_spi(VSPI);
static QueueHandle_t dayq, evq;
struct Ev { char t[72]; };

bool clock_now(struct tm &t) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;
  localtime_r(&now, &t);
  return true;
}
void fmt_date(char *out, size_t n, const struct tm &t) { snprintf(out, n, "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday); }

void log_event(const char *fmt, ...) {
  if (!evq) return;
  Ev e; va_list a; va_start(a, fmt); vsnprintf(e.t, sizeof e.t, fmt, a); va_end(a);
  Serial.printf("event: %s\n", e.t);
  xQueueSend(evq, &e, 0);
}
static void write_event(const Ev &e) {
  struct tm t; char ts[20] = "unknown time";
  if (clock_now(t)) snprintf(ts, sizeof ts, "%04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  bool est; { Lock l; est = S.clock_est; }
  bool fresh = !SD.exists("/camper/events.csv");
  File f = SD.open("/camper/events.csv", FILE_APPEND);
  if (!f) return;
  if (fresh) f.println("time,event");
  f.printf("%s,%s%s%s\n", ts, e.t, demo_on ? " (demo)" : "", est ? " (time guessed)" : "");
  f.close();
}
static void write_bank() {
  struct tm t;
  if (!clock_now(t)) return;
  char row[96];
  {
    Lock l; const Bank &b = S.bank;
    if (!b.data || millis() - b.at > 60000) return;
    snprintf(row, sizeof row, "%02d:%02d,%.1f,%d,%d,%d,%d,%d,%d,%d\n", t.tm_hour, t.tm_min, b.soc, b.in_w, b.out_w, b.ac_out_w, b.ac, b.dc, b.usb, b.light_mode);
  }
  char path[40], date[11]; fmt_date(date, sizeof date, t);
  snprintf(path, sizeof path, "/camper/bank-%s.csv", date);
  bool fresh = !SD.exists(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) return;
  if (fresh) f.print("time,soc,in_w,out_w,ac_out_w,ac,dc,usb,light\n");
  f.print(row);
  f.close();
}

void logger_day_closed(const DayRec &d) { if (dayq) xQueueSend(dayq, &d, 0); }   // called with the lock held: no locking here

static void add_day_ram(const DayRec &d) {
  Lock l;
  S.days.push_back(d);
  if (S.days.size() > 60) S.days.erase(S.days.begin());
  S.days_ver++;
}

void logger_demo_days() {             // 14 made-up days so the history screens can be judged now
  Lock l;
  S.days.clear(); S.days.reserve(15);
  time_t now = time(nullptr); if (now < 1700000000) now = 1793577600;
  for (int i = 14; i >= 1; i--) {
    time_t d = now - i * 86400; struct tm t; localtime_r(&d, &t);
    DayRec r{};
    fmt_date(r.date, sizeof r.date, t);
    float sun = 0.35f + 0.65f * ((i * 37) % 10) / 10.0f;
    r.yield_10wh = 40 + sun * 95; r.peak_w = 70 + sun * 140;
    r.ah_in = r.yield_10wh * 0.62f; r.ah_load = 9 + (i % 4) * 2.5f;
    r.vmin_cv = 1205 + (i * 13) % 40; r.vmax_cv = sun > 0.6f ? 1440 : 1395;
    r.bulk_m = 180 + sun * 120; r.abs_m = sun > 0.5f ? 90 : 20; r.float_m = sun > 0.6f ? 240 : 0;
    S.days.push_back(r);
  }
  S.days_ver++;
}

// ---- days.csv
static const char *DAYS = "/camper/days.csv";
static void load_days() {
  File f = SD.open(DAYS, FILE_READ);
  if (!f) return;
  std::vector<DayRec> v;
  char line[128];
  while (f.available()) {
    int n = f.readBytesUntil('\n', line, sizeof line - 1); line[n] = 0;
    if (line[0] < '0' || line[0] > '9') continue;     // header
    DayRec r{}; float y, ain, ald, vmin, vmax; unsigned pk, b, a, fl;
    if (sscanf(line, "%10[^,],%f,%u,%f,%f,%f,%f,%u,%u,%u", r.date, &y, &pk, &ain, &ald, &vmin, &vmax, &b, &a, &fl) == 10) {
      r.yield_10wh = y * 100 + 0.5f; r.peak_w = pk; r.ah_in = ain; r.ah_load = ald;
      r.vmin_cv = vmin * 100 + 0.5f; r.vmax_cv = vmax * 100 + 0.5f; r.bulk_m = b; r.abs_m = a; r.float_m = fl;
      v.push_back(r);
      if (v.size() > 60) v.erase(v.begin());
    }
  }
  f.close();
  Lock l; S.days = v; S.days_ver++;
}
static void write_day(const DayRec &d) {
  bool fresh = !SD.exists(DAYS);
  File f = SD.open(DAYS, FILE_APPEND);
  if (!f) return;
  if (fresh) f.println("date,yield_kwh,peak_w,ah_in,ah_load,vbat_min,vbat_max,bulk_min,abs_min,float_min");
  f.printf("%s,%.2f,%u,%.1f,%.1f,%.2f,%.2f,%u,%u,%u\n", d.date, d.yield_10wh / 100.0f, d.peak_w, d.ah_in, d.ah_load,
           d.vmin_cv / 100.0f, d.vmax_cv / 100.0f, d.bulk_m, d.abs_m, d.float_m);
  f.close();
}

// ---- clock without internet. Best source wins: internet (NTP) > the WiFi's own Date header (also behind a
// campsite login page) > the phone page (the phone's clock) > set by hand > guessed from the last time on SD.
// A guessed clock is shown as such and nothing is logged with it (wrong dates mixed up the trip diary).
static const char *CLK_NAMES[] = {"not set", "guessed", "set by hand", "from phone", "from wifi", "internet"};
const char *clock_src_name(uint8_t s) { return CLK_NAMES[s < 6 ? s : 0]; }
static volatile bool clock_dirty = false;
void clock_set(long e, uint8_t src) {
  if (e < 1700000000L || e > 4000000000L) return;
  uint8_t cur; { Lock l; cur = S.clock_src; }
  if (src != CLK_HAND && src < cur) return;
  long d = e - (long)time(nullptr);
  if (src != CLK_NET) { timeval tv{(time_t)e, 0}; settimeofday(&tv, nullptr); }
  { Lock l; S.clock_ok = true; S.clock_est = false; S.clock_src = src; }
  clock_dirty = true;
  if (cur == CLK_NONE) log_event("clock %s", clock_src_name(src));
  else if (cur < CLK_HAND || labs(d) > 90) log_event("clock %s (moved %+ld min)", clock_src_name(src), d / 60);
}
static void restore_clock() {
  if (time(nullptr) > 1700000000) return;
  File f = SD.open("/camper/clock.txt", FILE_READ);
  if (!f) return;
  long e = f.parseInt();
  f.close();
  if (e < 1700000000) return;
  timeval tv{(time_t)(e + 30), 0};                     // + roughly the boot time; the time switched off is unknown
  settimeofday(&tv, nullptr);
  Lock l; if (S.clock_src == CLK_NONE) { S.clock_ok = true; S.clock_est = true; S.clock_src = CLK_SD; }
}
static void save_clock() {
  time_t now = time(nullptr);
  if (now < 1700000000) return;
  File f = SD.open("/camper/clock.txt", FILE_WRITE);
  if (!f) return;
  f.printf("%ld\n", (long)now);
  f.close();
}

static void sd_info() {
  uint64_t used = SD.usedBytes(), total = SD.totalBytes();
  Lock l;
  S.sd_used_kb = used / 1024; S.sd_total_mb = total / (1024 * 1024);
  snprintf(S.sd_msg, sizeof S.sd_msg, "%.1f MB used of %.1f GB", used / 1048576.0, total / 1073741824.0);
}

// after a soft restart (update, restore) the card can still be in the middle of a command: clock it out
static void sd_unstick() {
  pinMode(sdcard::CS, OUTPUT); digitalWrite(sdcard::CS, HIGH);
  sd_spi.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 20; i++) sd_spi.transfer(0xFF);
  digitalWrite(sdcard::CS, LOW);
  for (int i = 0; i < 1100; i++) sd_spi.transfer(0xFF);   // finishes a block read in progress
  digitalWrite(sdcard::CS, HIGH);
  for (int i = 0; i < 4; i++) sd_spi.transfer(0xFF);
  sd_spi.endTransaction();
}
void sd_restart() { { Lock l; S.sd_ok = false; } delay(250); SD.end(); delay(50); ESP.restart(); }

static void task(void *) {
  sd_spi.begin(sdcard::SCK, sdcard::MISO, sdcard::MOSI, sdcard::CS);
  bool ok = false;
  for (int round = 0; round < 3 && !ok; round++) {
    sd_unstick();
    for (uint32_t hz : {20000000u, 8000000u, 4000000u}) {   // some cards only start at a slow clock
      ok = SD.begin(sdcard::CS, sd_spi, hz, "/sd", 2);
      Serial.printf("SD begin %u Hz: %s type %d\n", (unsigned)hz, ok ? "ok" : "fail", ok ? (int)SD.cardType() : -1);
      if (ok) break;
      SD.end();
      delay(100);
    }
  }
  if (ok) {
    SD.mkdir("/camper");
    { Lock l; S.sd_ok = true; }
    sd_info();
    store_auto_restore();
    restore_clock();
    trips_check();
    load_days();
    weather_load_cache();
  } else { Lock l; S.sd_ok = false; strcpy(S.sd_msg, "no card (FAT32, max 32 GB)"); }
  places_begin();

  int last_min = -1; uint32_t last_clock = 0, last_info = 0;
  bool was_demo = false;
  for (;;) {
    DayRec d;
    while (xQueueReceive(dayq, &d, 0) == pdTRUE) {
      if (!demo_on && S.sd_ok) write_day(d);
      add_day_ram(d);
    }
    if (was_demo != demo_on) {
      was_demo = demo_on;
      if (demo_on) logger_demo_days(); else if (S.sd_ok) load_days(); else { Lock l; S.days.clear(); S.days_ver++; }
    }
    struct tm tn;
    if (S.sd_ok && !demo_on && !S.clock_est && clock_now(tn) && tn.tm_min != last_min) { last_min = tn.tm_min; store_minute(tn); }
    if (S.sd_ok) store_service();

    Ev ev;
    while (xQueueReceive(evq, &ev, 0) == pdTRUE) if (S.sd_ok) write_event(ev);
    if (S.sd_ok && (clock_dirty || millis() - last_clock > 60000)) {
      bool tick = millis() - last_clock > 60000;
      clock_dirty = false; last_clock = millis(); save_clock();
      if (tick && !demo_on && !S.clock_est) write_bank();
    }
    if (S.sd_ok && millis() - last_info > 600000) { last_info = millis(); sd_info(); }
    delay(200);
  }
}

}  // namespace app
#include "driver/sdspi_host.h"
void sd_probe() {
  using namespace app;
  pinMode(sdcard::CS, OUTPUT); digitalWrite(sdcard::CS, HIGH);
  pinMode(sdcard::MISO, INPUT_PULLUP);
  SPI.begin(sdcard::SCK, sdcard::MISO, sdcard::MOSI, -1);
  SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 10; i++) SPI.transfer(0xFF);              // 80 clocks, card wakes up
  digitalWrite(sdcard::CS, LOW);
  uint8_t cmd0[] = {0x40, 0, 0, 0, 0, 0x95};
  for (uint8_t b : cmd0) SPI.transfer(b);
  int r = 0xFF;
  for (int i = 0; i < 16 && r == 0xFF; i++) r = SPI.transfer(0xFF);
  digitalWrite(sdcard::CS, HIGH);
  SPI.transfer(0xFF);
  SPI.endTransaction();
  Serial.printf("sdprobe CMD0 reply 0x%02X (0x01 = card answers, 0xFF = no card / no contact)\n", r);
  bool ok = SD.begin(sdcard::CS, SPI, 4000000, "/sd2", 2);
  Serial.printf("sdprobe default SPI begin: %s type %d size %llu MB\n", ok ? "ok" : "fail", ok ? (int)SD.cardType() : -1, ok ? SD.cardSize() / 1048576 : 0ULL);
  SPI.end();
}
void sd_format() {
  using namespace app;
  Serial.println("sdformat: formatting, this can take a minute...");
  SPI.begin(sdcard::SCK, sdcard::MISO, sdcard::MOSI, -1);
  bool ok = SD.begin(sdcard::CS, SPI, 8000000, "/sd2", 2, true);    // format_if_empty
  Serial.printf("sdformat: %s type %d size %llu MB\n", ok ? "ok" : "FAILED", ok ? (int)SD.cardType() : -1, ok ? SD.cardSize() / 1048576 : 0ULL);
  SD.end(); SPI.end();
  delay(500);
  ESP.restart();
}
namespace app {
void logger_begin() {
  dayq = xQueueCreate(4, sizeof(DayRec));
  evq = xQueueCreate(10, sizeof(Ev));
  xTaskCreatePinnedToCore(task, "sdlog", 6144, nullptr, 0, &task_sd, 0);
}

}  // namespace app
