// Hooks between the background modules (not used by the UI).
#pragma once
#include <string>
#include <time.h>
#include "state.hpp"

namespace app {
void places_begin(); void place_service(); bool place_pinned(float &lat, float &lon, char *name, size_t n);
// victron.cpp
void vic_begin();
void vic_parse(const std::string &mfg, const char *addr, int rssi);
void vic_tick();                                   // 1 s
void vic_demo_reset();
// sensors.cpp
void sens_begin();
void sens_parse(uint16_t uuid, const std::string &data, const char *addr, int rssi);
void sens_tick();                                  // 1 s
void sens_ruuvi(const std::string &mfg, const char *addr, int rssi);
void level_accel(const char *addr, int ax, int ay, int az);
// fossibot.cpp (radio task)
void bank_seen(const void *nimble_dev);           // every advert: is it a BrightEMS station?
void bank_service();                                // connect, poll every 2 s, reconnect
// weather.cpp
void weather_fetch();                              // blocking HTTP, radio task
void weather_demo();
void weather_load_cache();                         // logger, after the SD is mounted
void weather_warnings(Wx &x);
// logger.cpp: day records handed over from the victron tick
void logger_day_closed(const DayRec &d);
void logger_demo_days();
// store.cpp / web.cpp (logger task)
void store_minute(const struct tm &t);
void store_service();
bool store_auto_restore();
void web_start_task();
void sd_restart();
extern TaskHandle_t task_radio, task_sd, task_web;
void ota_check(); void ota_boot_guard(); void ota_boot_ok();
extern char ota_msg[48]; extern volatile bool ota_done;                                  // unmount the card, then restart
// time helpers
bool clock_now(struct tm &t);                      // false until NTP or the SD-restored clock set it
void fmt_date(char *out, size_t n, const struct tm &t);   // YYYY-MM-DD
}  // namespace app
