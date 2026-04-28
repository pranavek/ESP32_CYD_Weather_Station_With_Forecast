# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash

This project is structured for **PlatformIO (VS Code)**. `platformio.ini` is at the project root. For Arduino IDE 2 usage see the README.

**Before building:**
1. Copy `Setup801_ESP32_CYD_ILI9341_240x320.h` or `Setup805_ESP32_CYD_ST7789_240x320.h` into the `User_Setups/` folder inside the TFT_eSPI library, then activate the correct one in `User_Setup_Select.h`.
2. Upload the `data/` folder contents to LittleFS **before** flashing the sketch. Use the `arduino-littlefs-upload` plugin (Ctrl+Shift+P → "Upload LittleFS to Pico/ESP8266/ESP32" in Arduino IDE 2).
3. In Arduino board settings, set LittleFS partition to **at least 1.5 MB** or the sketch will crash on boot.

**Required libraries (install via Arduino Library Manager):**
- TFT_eSPI 2.4.3+ (or the forked version at github.com/AndroidCrypto/TFT_eSPI)
- OpenWeather (Bodmer)
- JSON_Decoder (Bodmer — do **not** use the IDE library manager version; install from GitHub)
- TJpg_Decoder 1.1.0+
- Timezone 1.2.4 (JChristensen)
- Time (PaulStoffregen)

**Board:** arduino-esp32 boards 3.2.0+; target `ESP32 Dev Module` (or equivalent CYD board).

**Serial monitor:** 250000 baud.

## Configuration

All user-facing settings live in `All_Settings.h`:
- WiFi credentials (`WIFI_SSID`, `WIFI_PASSWORD`)
- OpenWeatherMap API key (`api_key`) — free tier, up to 1000 requests/day
- Location as decimal lat/long (`latitude`, `longitude`)
- Timezone (`TIMEZONE`) — pick a zone reference defined in `NTP_Time.h` (e.g. `euCET`, `usET`, `usPT`, `UK`, `ausET`)
- Units (`"metric"` or `"imperial"`)
- Update interval (`UPDATE_INTERVAL_SECS`, default 15 min)
- Localisation strings (`shortDOW`, `sunStr`, `cloudStr`, `humidityStr`, `moonPhase`)

To add a timezone not already defined, add `TimeChangeRule` pairs to `NTP_Time.h` following the existing pattern, then reference the new `Timezone` object name in `All_Settings.h`.

## Architecture

The sketch is a single-threaded Arduino loop split across several files that compile as one translation unit:

```
setup()  →  WiFi connect → NTP sync → initial updateData()
loop()   →  every UPDATE_INTERVAL_SECS: updateData()
         →  every minute: drawTime() + NTP re-sync
```

**`Esp32_CYD_TFT_eSPI_OpenWeather_LittleFS_v02.ino`** — main file. Owns the `TFT_eSPI tft`, `OW_Weather ow`, and `OW_forecast* forecast` globals. All draw functions reference these globals directly. `forecast` is heap-allocated at the start of `updateData()` and deleted at the end to reclaim ~4 KB of RAM.

**`All_Settings.h`** — included by the `.ino`; all `#define`s and `const` settings. Edit this file for any deployment change.

**`NTP_Time.h`** — included by the `.ino`; defines all timezone rules, NTP UDP logic, and the `syncTime()` function. Also declares `lastMinute` and `tz1_Code` used in the main loop.

**`GfxUi.h/.cpp`** — the `GfxUi` class wraps `TFT_eSPI` to provide `drawBmp()` (reads 24-bit BMP from LittleFS) and `drawProgressBar()`. `BUFFPIXEL 32` is tuned for LittleFS SPI pipeline; increase to 80 only for SD card use.

**`MoonPhase.ino`** — compiled as part of the sketch (Arduino multi-file `.ino`). Provides `moon_phase(y, m, d, h, &ip)` returning icon index 0–23 and phase name index 0–7.

**`ScreenGrabClient.ino` / `ScreenGrabServer.ino`** — compiled in but inactive unless `#define SCREEN_SERVER` is uncommented in the main `.ino`.

## Display Layout (320×240, landscape)

The display is treated as **portrait 240×320** in the code (`tft.setRotation(0)`). Pixel coordinates reference this orientation:

| Region | Y range | Content |
|--------|---------|---------|
| Header | 0–53 | Date ("Updated: …") + time (HH:MM large font) |
| Current weather | 53–153 | 100×100 icon, weather text, temp (large), wind speed+dir, pressure |
| Forecast strip | 153–240 | 4 × 50×50 icons with day abbreviation and min/max temps |
| Astronomy | 240–320 | Sunrise/sunset, moon phase icon + name, cloud cover, humidity |

Weather icons are BMP files in `data/icon/` (100×100) and `data/icon50/` (50×50). Icon filename is determined by `getMeteoconIcon(id, today)` which maps OpenWeatherMap condition IDs; night-time clear/partly-cloudy is detected by offsetting the ID by +1000 when `now() < sunrise`.

## Debug Flags

Uncomment in the main `.ino` to enable:
- `#define SERIAL_MESSAGES` — dumps full forecast data to serial (already on by default)
- `#define SCREEN_SERVER` — enables TCP screenshot server via `ScreenGrabServer.ino`
- `#define RANDOM_LOCATION` — picks random lat/long each refresh (icon/layout testing)
- `#define FORMAT_LittleFS` — wipes and reformats LittleFS on boot (re-comment immediately after use)
