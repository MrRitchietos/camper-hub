# Camper hub

Firmware for the ESP32-2432S028 board (the "Cheap Yellow Display", 320x240 touch screen) as the main screen in a camper van.

## What it shows

* Solar charger: Victron SmartSolar over Bluetooth (Instant Readout), battery state, today and past days
* Power bank: Fossibot F1200 (BrightEMS) with switches for USB, 12V, AC and light, plus its settings
* Weather: forecast from Open-Meteo, saved on the SD card for offline use, place can be pinned to a town
* Thermometers: Bluetooth sensors (BTHome, pvvx firmware, RuuviTag) for inside, outside, fridge and freezer, with alarms
* Level: RuuviTag on the floor, shows how many cm to raise each wheel
* Checklists for leaving and arriving
* Trip diary, one line per day on the SD card
* Screensaver with the van under the sky or your own photos
* Phone page: open the board's address in a browser to see everything and change settings. User `camper`, the password is shown and can be changed in Tools > Settings > Phone login. Switch the page off in Settings to save memory

## SD card

Everything is saved in the folder `camper`: a log line per minute, the trip diary, events, the last forecast, a settings backup and the screensaver photos (`camper/photos`).

## Updates

Tap Tools > Settings > Update (or "Check now" on the phone page): the board restarts, looks for a new release in this repository and installs `firmware.bin`. A version that does not start is rolled back. It can also be installed from the phone page or from `camper/update.bin` on the SD card.

## Building

PlatformIO: `pio run -t upload`. An optional `include/secrets.hpp` can hold a first WiFi network:

```
#pragma once
#define HOME_SSID "my wifi"
#define HOME_PASS "my password"
```

Release builds leave it out: `tools/release.sh 1.2.0`.
