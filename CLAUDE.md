# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash

This project is **PlatformIO-first**. `platformio.ini` is at the project root, all source lives in `src/`, all filesystem assets in `data/`. Arduino IDE 2 is a supported alternative — see README for that path.

**End-user path:** open the webflasher page (GitHub Pages, served from `docs/flasher/`), fill in WiFi/API/location/brightness, and click Connect. ESP Web Tools flashes bootloader + partitions + firmware + LittleFS in one Web Serial pass; the page then sends a `<<PROV …>>` JSON blob over the same connection so the device persists settings to NVS and reboots. No `uploadfs` step is required. Chrome/Edge only — Firefox and Safari lack Web Serial.

**Developer / local path (PlatformIO):**
1. Open the folder in VS Code with the PlatformIO extension. Libraries listed in `lib_deps` are pulled automatically.
2. Run **PlatformIO: Upload Filesystem Image** (`pio run -t uploadfs`) — only when developing locally. End users on the webflasher do not need this.
3. Run **PlatformIO: Upload** (`pio run -t upload`) to flash the firmware.
4. Serial monitor: 250000 baud (`pio device monitor`). On a freshly flashed device the firmware enters provisioning mode and emits `<<PROV READY>>`; paste a `<<PROV {…}>>` line to provision without using the webflasher.

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

User-facing settings (WiFi, OpenWeatherMap API key, lat/long, timezone, brightness) are **persisted in NVS** and entered through the webflasher page at [docs/flasher/index.html](docs/flasher/index.html). The values in [src/All_Settings.h](src/All_Settings.h) are compile-time **defaults only** — used as fallbacks when NVS has not been provisioned. Do not direct end users to edit that file.

The runtime cache lives in [src/Config.h](src/Config.h) / [src/Config.cpp](src/Config.cpp) (Preferences-backed, reads once at boot). [src/Provisioning.cpp](src/Provisioning.cpp) implements the `<<PROV …>>` serial framing used by the webflasher to write a JSON config blob; it also handles `<<PROV WIPE>>` for resetting NVS without re-flashing.

Compile-time-only knobs that remain in `All_Settings.h`:
- Update interval (`UPDATE_INTERVAL_SECS`, default 30 min)
- Night hours (`NIGHT_OFF_HOUR`, `NIGHT_ON_HOUR`)
- Page count (`PAGE_COUNT`, default 2)
- Units (`"metric"` / `"imperial"`)
- Localisation strings (`shortDOW`, `sunStr`, `cloudStr`, `humidityStr`, `moonPhase`)

To add a timezone not already supported, add `TimeChangeRule` pairs to [src/NTP_Time.h](src/NTP_Time.h), `extern` it from [src/Timezones.h](src/Timezones.h), add a branch in `timezoneByName`, then add an `<option>` to the webflasher dropdown — all four sites must be updated together.

## Architecture

The sketch is a single-threaded Arduino loop split across several files in `src/` that compile as one translation unit:

```
setup()  →  WiFi connect → NTP sync → initial updateData()
loop()   →  every UPDATE_INTERVAL_SECS:  updateData() (refetch + recache + redraw)
         →  every minute:                drawTime() + NTP re-sync
         →  every 15 s (hardcoded):      cycle currentPage and redraw page body
```

Forecast data is heap-allocated as `OW_forecast* forecast` at the start of `updateData()` and **deleted at the end of the same call** to reclaim ~4 KB of RAM. Anything that needs forecast data outside that window must read from the cache structs (`slotCache[4]`, `dayCache[4]`, and the `cached*` scalars) populated by `cacheForecastData()`. Drawing functions invoked during page cycling read only from the cache — never from `forecast` directly.

Files:

- **[src/Esp32_CYD_TFT_eSPI_OpenWeather_LittleFS_v02.ino](src/Esp32_CYD_TFT_eSPI_OpenWeather_LittleFS_v02.ino)** — main sketch. Owns the `TFT_eSPI tft`, `OW_Weather ow`, and `OW_forecast* forecast` globals. Holds the carousel state (`currentPage`, `lastPageCycle`) and all draw functions.
- **[src/All_Settings.h](src/All_Settings.h)** — compile-time defaults only. Runtime overrides come from NVS via Config.
- **[src/Config.h](src/Config.h) / [src/Config.cpp](src/Config.cpp)** — Preferences-backed config cache. `Config::begin()` reads NVS into memory at boot; getters return cached pointers. `Config::applyJson` validates and persists a webflasher payload.
- **[src/Provisioning.h](src/Provisioning.h) / [src/Provisioning.cpp](src/Provisioning.cpp)** — implements the `<<PROV …>>` serial framing used by the webflasher. `run()` is the blocking first-boot loop; `pollWipe()` is the non-blocking main-loop poll for `<<PROV WIPE>>`.
- **[src/Timezones.h](src/Timezones.h)** — `extern` declarations for the Timezone objects defined in NTP_Time.h, plus the `timezoneByName` lookup used by Config. Lets non-`.ino` translation units reference timezones without pulling in NTP_Time.h's globals.
- **[src/NTP_Time.h](src/NTP_Time.h)** — included only by the `.ino`; defines the Timezone object instances, NTP UDP logic, and `syncTime()`. Do **not** include from other `.cpp` files (multiple-definition errors).
- **[src/GfxUi.h](src/GfxUi.h) / [src/GfxUi.cpp](src/GfxUi.cpp)** — the `GfxUi` class wraps `TFT_eSPI` to provide `drawBmp()` (reads 24-bit BMP from LittleFS) and `drawProgressBar()`. `BUFFPIXEL 32` is tuned for LittleFS SPI pipeline; increase to 80 only for SD card use.
- **[src/MoonPhase.ino](src/MoonPhase.ino)** — compiled as part of the sketch (Arduino multi-file `.ino`). Provides `moon_phase(y, m, d, h, &ip)` returning icon index 0–23 and phase name index 0–7.
- **[src/ScreenGrabClient.ino](src/ScreenGrabClient.ino) / [src/ScreenGrabServer.ino](src/ScreenGrabServer.ino)** — compiled in but inactive unless `#define SCREEN_SERVER` is uncommented in the main `.ino`.
- **[docs/flasher/index.html](docs/flasher/index.html) + [manifest.json](docs/flasher/manifest.json)** — static webflasher page, served from GitHub Pages by [.github/workflows/release.yml](.github/workflows/release.yml). The workflow builds firmware and the LittleFS image and copies them next to the manifest.

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

Weather icons are BMP files in `data/icon/` (100×100) and `data/icon50/` (50×50). The icon filename is determined by `getMeteoconIcon(id, today)` which maps OpenWeatherMap condition IDs; night-time clear/partly-cloudy is detected by offsetting the ID by +1000 when `now() < cachedSunrise || now() > cachedSunset`.

The 4 forecast slots show **3-hour windows** (OWM free 5-day endpoint), not hourly. A 3-hour block with any rain in it gets a 5xx id and a rain icon — this is OWM behaviour, not a bug. The full forecast (id, main, description, pop) is dumped to serial when `SERIAL_MESSAGES` is defined.

## Debug Flags

Uncomment in the main `.ino` to enable:
- `#define SERIAL_MESSAGES` — dumps full forecast data to serial (already on by default)
- `#define SCREEN_SERVER` — enables TCP screenshot server via `ScreenGrabServer.ino`
- `#define RANDOM_LOCATION` — picks random lat/long each refresh (icon/layout testing)
- `#define FORMAT_LittleFS` — wipes and reformats LittleFS on boot (re-comment immediately after use)
