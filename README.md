# Camper hub

Firmware for the ESP32-2432S028 board (the "Cheap Yellow Display", 320x240 touch screen) as the main screen in a camper van. It reads the solar charger, the power bank and Bluetooth thermometers, shows the weather, helps you park level, keeps a trip diary and can be used from your phone.

<p>
<img src="docs/screens/home.png" width="320" alt="Home">
<img src="docs/screens/home-day.png" width="320" alt="Home in the day look">
</p>

The screenshots are taken from the board in demo mode (Tools > Settings > Demo), so every page shows values without real devices.

## Pages

**Home.** Six tiles: battery, solar, power bank, inside, outside and weather. Tap a tile to open its page.

**Power.** Live solar power, charger state, battery voltage and current, today's yield and the last 12 hours. Today lists the totals of the day, Days shows the last two weeks.

<p>
<img src="docs/screens/solar.png" width="260" alt="Solar">
<img src="docs/screens/today.png" width="260" alt="Today">
<img src="docs/screens/days.png" width="260" alt="Days">
</p>

**Climate.** Thermometers for inside, outside, fridge and freezer, and the forecast for 48 hours and 7 days. The forecast is kept on the SD card, so it still shows without internet. The place follows the board's location or can be pinned to a town. The inside sensor also shows the dew point and warns when the air is damp or the windows will mist up.

<p>
<img src="docs/screens/climate.png" width="260" alt="Climate">
<img src="docs/screens/forecast.png" width="260" alt="Forecast">
</p>

**Bank.** Fossibot F1200 power bank: charge level, power in and out, time left, switches for USB, 12V, AC and light, and its own settings. The board remembers your own unit, so a neighbour's power bank on a campsite is never used. Bank settings > Forget pairs a new one.

<p>
<img src="docs/screens/bank.png" width="260" alt="Bank">
<img src="docs/screens/banksettings.png" width="260" alt="Power bank settings">
</p>

**Level.** A RuuviTag shows how level the van stands and how many cm to raise each wheel. Fix the tag anywhere it cannot move: flat, on a wall or upside down, turned any way. Then tap Calibrate and follow the 3 steps:

1. Park and measure.
2. Turn the camper around with the wheels on the same spots and measure again. The slope of the ground flips with the van, the error of the tag does not, so no level ground is needed. On ground you know is level, skip this step.
3. Raise only the front a bit (front wheels on the ramps) and measure. This tells the board where the front is. Skip it to use the arrow direction from Settings.

Each step uses only the readings taken after you tap Measure (10 of them) and starts again when the van moves. Moving the tag later shows "Tag moved? Calibrate".

How fast it updates: the standard RuuviTag firmware measures every 2.57 s (each value is sent twice). The screen shows every new value at once and jumps straight to a new position, and the small arrow in the corner lights up at each reading. While the Level page is open the board listens to Bluetooth 90 % of the time, so no reading is missed. For real time levelling, install Ruuvi's official test firmware (a new value every 0.44 s): it uses more battery.

**Tools.** Checklists for leaving and arriving (tap to tick, hold to remove), the trip diary with one line per day, and all settings.

In Settings and Power bank settings only the value on the right (the rounded button) reacts to a tap, so scrolling with your thumb on the left never changes anything. Everywhere on the screen a touch only counts as a tap when the finger stays put: a swipe, or a touch that stops a list that is still scrolling, never changes a setting. Forgetting the power bank needs a second tap within 4 seconds.

<p>
<img src="docs/screens/level.png" width="260" alt="Level">
<img src="docs/screens/calibrate.png" width="260" alt="Calibrate the level">
<img src="docs/screens/checklist.png" width="260" alt="Checklist">
<img src="docs/screens/settings.png" width="260" alt="Settings">
<img src="docs/screens/clock.png" width="260" alt="Set the clock">
<img src="docs/screens/solar-day.png" width="260" alt="Solar in the day look">
</p>

## Day and night look

Automatic by default: the light look from sunrise to sunset (times from the forecast), the dark look at night. It can also be set to always day or always night in Tools > Settings > Day / night look.

## Screensaver

After a while the screen shows the van under the sky with the clock, battery, solar and outside temperature, or your own photos from the SD card. Photos can be added from the phone page; they are made smaller on the phone before they are sent.

## Phone page

Open the board's address in a browser to see everything and change settings, the checklists, the power bank and the files on the SD card.

* Turn it on or off in Tools > Settings > Phone page (off saves memory)
* User `camper`, the password is shown and can be changed in Tools > Settings > Phone login
* Without campsite WiFi: turn on Own WiFi, join the network `CamperHub` (same password) and open `http://192.168.4.1`

## Devices

* Victron SmartSolar charger over Bluetooth (Instant Readout). Enter its key once: VictronConnect, the charger, gear icon, three dots, Product info, Instant readout details. The key is then locked: a new one is only accepted after you remove the old one on purpose (Settings > Victron key > Remove key)
* Fossibot F1200 power bank (BrightEMS app protocol)
* Thermometers: Xiaomi LYWSD03MMC with pvvx firmware, Shelly BLU H&T and other BTHome sensors, RuuviTag. Tap a sensor to set it as inside, outside, fridge or freezer. A fridge or freezer that is too warm, or a sensor that goes quiet, sounds an alarm
* RuuviTag for the level page (it also gives temperature, humidity and air pressure for the storm warning)

## Clock without internet

The board has no clock battery, so after a power cut it needs the time from somewhere. It takes the best source it can get:

1. internet time (NTP)
2. the time a WiFi sends with every web reply, which also works on a campsite WiFi that blocks NTP or wants a login first
3. your phone: opening the phone page sets the clock from the phone by itself
4. by hand: Tools > Settings > Clock, or tap the clock at the top when it shows "time? tap"

Until then it carries on from the last time saved on the SD card, marked as a guess. Nothing is logged with a guessed time, so the trip diary never gets wrong dates.

## SD card

Everything is saved in the folder `camper`:

* a log line per minute with all values (`min-DATE.csv`)
* the trip diary (`trips.csv`)
* events, alarms and the last forecast
* a daily settings backup, put back by itself on an empty board
* the screensaver photos (`camper/photos`)

## Updates

Tap Tools > Settings > Update (or "Check now" on the phone page). The board restarts, looks for a new release in this repository and installs `firmware.bin`. A version that keeps crashing is rolled back to the previous one. A firmware file can also be installed from the phone page or from `camper/update.bin` on the SD card.

## Building

PlatformIO: `pio run -t upload`. An optional `include/secrets.hpp` can hold a first WiFi network:

```
#pragma once
#define HOME_SSID "my wifi"
#define HOME_PASS "my password"
```

Release builds leave it out: `tools/release.sh 1.2.0`.

The serial console (115200 baud) has a few test commands, for example `demo` (toggle demo mode), `heap`, `tasks` and `shot` (a screenshot in the exact colours the board draws), plus `ls` and `cat <file>` to read the SD card logs, `fps` (how much of the screen is redrawn while idle), `lvl` (level sensor state and noise), `ruuvi` (print every RuuviTag reading), `clock` (time and where it came from) and `tap x y` / `swipe x0 y0 x1 y1 ms` to test touch handling.
