# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash

This project is **PlatformIO-first**. `platformio.ini` is at the project root, all source lives in `src/`, all filesystem assets in `data/`. Arduino IDE 2 is a supported alternative — see README for that path.

**PlatformIO workflow:**
1. Open the folder in VS Code with the PlatformIO extension. Libraries listed in `lib_deps` are pulled automatically.
2. Run **PlatformIO: Upload Filesystem Image** (`pio run -t uploadfs`) to flash the `data/` folder to LittleFS **before** flashing the sketch on a fresh device. Skipping this causes a boot crash.
3. Run **PlatformIO: Upload** (`pio run -t upload`) to flash the firmware.
4. Serial monitor: 250000 baud (`pio device monitor`).

**Display setup is configured entirely via `build_flags` in `platformio.ini`** — `-DUSER_SETUP_LOADED` disables TFT_eSPI's setup-file lookup, so do **not** edit `User_Setup_Select.h` or copy `Setup801*.h`/`Setup805*.h` into the library when building with PlatformIO. Those files exist for Arduino IDE users only.

**Active env**: `[env:esp32-cyd-st7789]` targets the **2432S028R dual-USB CYD** (USB-C + Micro-B, ST7789 panel). The commented-out `[env:esp32-cyd-ili9341]` is for the older single-USB Micro-B-only board.

**Partitioning**: `board_build.partitions = default.csv` gives ~1.5 MB LittleFS, which is the minimum the assets require.

## Display Driver Gotchas (ST7789)

The dual-USB CYD panel needs:
- `-DTFT_RGB_ORDER=0` — sets MADCTL BGR bit (0x08). **Without this, yellow renders as cyan** (R↔B swap).
- `-DTFT_INVERSION_OFF` — gives a black background. With `INVERSION_ON` the background is white.

**Important: `-DTFT_BGR` is a no-op for the ST7789 driver.** That flag is only consumed by ILI9341 code paths in TFT_eSPI. The ST7789 driver reads `TFT_RGB_ORDER` (see `TFT_Drivers/ST7789_Defines.h` → `TFT_MAD_COLOR_ORDER` → `ST7789_Rotation.h`). Do not "fix" colour issues by toggling `TFT_BGR` on this board — it has zero effect. See commit `a4617ed`.

If `TFT_HEIGHT==320 && TFT_WIDTH==240`, `CGRAM_OFFSET` is **not** auto-defined, so `TFT_RGB_ORDER` must be set explicitly.

## Configuration

All user-facing settings live in [src/All_Settings.h](src/All_Settings.h):
- WiFi credentials (`WIFI_SSID`, `WIFI_PASSWORD`)
- Location as decimal lat/long (`latitude`, `longitude`)
- Weather data is fetched from [Open-Meteo](https://open-meteo.com/) — no API key required (free tier: 10,000 requests/day for non-commercial use)
- Timezone (`TIMEZONE`) — pick a `Timezone` object defined in [src/NTP_Time.h](src/NTP_Time.h) (e.g. `usCT`, `usET`, `euCET`, `UK`, `ausET`)
- Units (`"metric"` or `"imperial"`)
- Update interval (`UPDATE_INTERVAL_SECS`, default 30 min)
- Brightness (`SCREEN_BRIGHTNESS` 0–255, fixed PWM level)
- Night hours (`NIGHT_OFF_HOUR`, `NIGHT_ON_HOUR`)
- Page count (`PAGE_COUNT`, default 2)
- Localisation strings (`shortDOW`, `sunStr`, `cloudStr`, `humidityStr`, `moonPhase`)

To add a timezone not already supported, add `TimeChangeRule` pairs to [src/NTP_Time.h](src/NTP_Time.h) following the existing pattern, then reference the new `Timezone` object name via `#define TIMEZONE …` in `All_Settings.h`.

## Architecture

The sketch is a single-threaded Arduino loop split across several files in `src/` that compile as one translation unit:

```
setup()  →  WiFi connect → NTP sync → initial updateData()
loop()   →  every UPDATE_INTERVAL_SECS:  updateData() (refetch + recache + redraw)
         →  every minute:                drawTime() + NTP re-sync
         →  every 15 s (hardcoded):      cycle currentPage and redraw page body
```

Forecast data is heap-allocated as `OMForecast* forecast` at the start of `updateData()` and **deleted at the end of the same call** to reclaim memory. Anything that needs forecast data outside that window must read from the cache structs (`slotCache[4]`, `dayCache[4]`, and the `cached*` scalars) populated by `cacheForecastData()`. Drawing functions invoked during page cycling read only from the cache — never from `forecast` directly.

The HTTP fetch happens in `fetchOpenMeteo(OMForecast*)` — it builds the Open-Meteo URL based on the configured units, performs an HTTPS GET via `WiFiClientSecure` (cert validation skipped via `setInsecure()`), and parses the response with ArduinoJson v7. The `OMForecast` struct holds three sections: `current` scalars, a 16-entry `hourly_*` array (1h spacing, index 0 aligned to first hour after `now()`), and a 5-entry `daily_*` array (today at index 0).

Files:

- **[src/Esp32_CYD_TFT_eSPI_OpenWeather_LittleFS_v02.ino](src/Esp32_CYD_TFT_eSPI_OpenWeather_LittleFS_v02.ino)** — main sketch. Owns the `TFT_eSPI tft` and `OMForecast* forecast` globals. Holds the carousel state (`currentPage`, `lastPageCycle`) and all draw functions.
- **[src/All_Settings.h](src/All_Settings.h)** — included by the `.ino`; all `#define`s and `const` settings. Edit this file for any deployment change.
- **[src/NTP_Time.h](src/NTP_Time.h)** — included by the `.ino`; defines all timezone rules, NTP UDP logic, and the `syncTime()` function. Also declares `lastMinute` and `tz1_Code` used in the main loop.
- **[src/GfxUi.h](src/GfxUi.h) / [src/GfxUi.cpp](src/GfxUi.cpp)** — the `GfxUi` class wraps `TFT_eSPI` to provide `drawBmp()` (reads 24-bit BMP from LittleFS) and `drawProgressBar()`. `BUFFPIXEL 32` is tuned for LittleFS SPI pipeline; increase to 80 only for SD card use.
- **[src/MoonPhase.ino](src/MoonPhase.ino)** — compiled as part of the sketch (Arduino multi-file `.ino`). Provides `moon_phase(y, m, d, h, &ip)` returning icon index 0–23 and phase name index 0–7.
- **[src/ScreenGrabClient.ino](src/ScreenGrabClient.ino) / [src/ScreenGrabServer.ino](src/ScreenGrabServer.ino)** — compiled in but inactive unless `#define SCREEN_SERVER` is uncommented in the main `.ino`.

## Display Layout (240×320 portrait)

The display is treated as **portrait 240×320** in the code (`tft.setRotation(0)`). The header (date + time) and the astronomy strip are the same on every page; only the middle band changes between pages.

**Common regions:**

| Region | Y range | Content |
|--------|---------|---------|
| Header | 0–53 | Date ("Updated: …") + time (HH:MM large font) |
| Astronomy | 240–320 | Sunrise/sunset, moon phase icon + name, cloud cover, humidity |

**Page 1** (current weather + 4 × 3-hour forecast):
| Region | Y range | Content |
|--------|---------|---------|
| Current weather | 53–153 | 100×100 icon, weather text, temp (large), wind, pressure |
| Forecast strip | 153–240 | 4 × 50×50 icons with HH:MM time and temp for the next four 3-hour slots |

**Page 2** (quote):
| Region | Y range | Content |
|--------|---------|---------|
| Quote area | 53–240 | Random motivational quote, refreshed each page cycle |

Weather icons are BMP files in `data/icon/` (100×100) and `data/icon50/` (50×50). The icon filename is determined by `getMeteoconIcon(code, when)` which maps Open-Meteo / WMO weather codes; for codes 0–2 (clear & partly cloudy) the function compares the seconds-of-day of `when` against today's sunrise/sunset to pick the day or night variant. The day window wraps correctly when sunset crosses midnight UTC. `weatherCodeLabel(code)` provides the short text label shown on screen (Open-Meteo doesn't ship one).

The 4 forecast slots are sampled from Open-Meteo's hourly array at 3-hour spacing (indices 0/3/6/9) so the visual cadence matches the prior 3-hour-window layout. Daily highs/lows come straight from `daily.temperature_2m_max/min` — no aggregation needed. Full per-section dumps go to serial when `SERIAL_MESSAGES` is defined.

## Debug Flags

Uncomment in the main `.ino` to enable:
- `#define SERIAL_MESSAGES` — dumps full forecast data to serial (already on by default)
- `#define SCREEN_SERVER` — enables TCP screenshot server via `ScreenGrabServer.ino`
- `#define RANDOM_LOCATION` — picks random lat/long each refresh (icon/layout testing)
- `#define FORMAT_LittleFS` — wipes and reformats LittleFS on boot (re-comment immediately after use)
