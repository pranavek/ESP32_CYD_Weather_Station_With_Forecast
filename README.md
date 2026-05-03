# ESP32 Cheap Yellow Display (CYD) Weather Station

Based on the original by [AndroidCrypto](https://github.com/AndroidCrypto/ESP32_CYD_Weather_Station_With_Forecast), extended with a 2-page carousel, night mode, and motivational quotes.

## Features

- **Current conditions** — weather icon (100×100 px), description, feels-like temperature, large temperature readout, wind speed + compass direction, barometric pressure with trend arrow
- **Live clock** — HH:MM updated every minute via NTP (timezone + DST aware); header shows current date as `Www  Mmm D  YYYY`
- **2-page bottom carousel** — cycles every 15 seconds between Page 1 (hourly forecast + astronomy) and Page 2 (4-day forecast + random quote)
- **Fixed brightness** — set via `#define SCREEN_BRIGHTNESS` in [src/All_Settings.h](src/All_Settings.h) (0–255, default 150)
- **Night mode** — backlight cuts off at `NIGHT_OFF_HOUR:59` and restores at `NIGHT_ON_HOUR:00`
- **Auto-refresh** — weather data fetched every 30 minutes; uses only the free OpenWeatherMap forecast endpoint

## Display Layout

The bottom section cycles every 15 seconds between two pages.

**Page 1 — Hourly forecast + Astronomy**
```
┌─────────────────────────────┐  ↑
│  Tue  Apr 28  2026          │  Header (date + time)
│         14:32               │
├─────────────────────────────┤
│  [icon]  Partly Cloudy  18° │  Current weather
│  feels 16°    ↗  1013 hPa  │
│          3 m/s              │
├─────────────────────────────┤
│ 15:00 18:00 21:00 00:00     │  Next 4 × 3-hour slots
│  [i]  [i]   [i]   [i]      │
├─────────────────────────────┤
│ Sun 05:42       ☽  Waxing   │  Astronomy
│     20:11   Cloud 23%       │
│             Humidity 61%    │
└─────────────────────────────┘  ↓ (320 px)
```

**Page 2 — 4-day forecast + Quote**
```
┌─────────────────────────────┐  ↑
│  Tue  Apr 28  2026          │  Header (date + time)
│         14:32               │
├─────────────────────────────┤
│  [icon]  Partly Cloudy  18° │  Current weather
│  feels 16°    ↗  1013 hPa  │
│          3 m/s              │
├─────────────────────────────┤
│  Mon   Tue   Wed   Thu      │  4-day forecast (icon + hi/lo)
│  [i]   [i]   [i]   [i]     │
│  22/14 21/13 19/11 20/12   │
├─────────────────────────────┤
│  "Fortune favors bold."     │  Random motivational quote
└─────────────────────────────┘  ↓ (320 px)
```

## Getting Started

### 1. Get an OpenWeatherMap API key

Sign up for a free account at [openweathermap.org](https://openweathermap.org/). The free tier allows up to 1000 requests/day (~40/hour), which is well above the 30-minute update interval used here.

### 2. Configure `All_Settings.h`

Edit [src/All_Settings.h](src/All_Settings.h):

```cpp
#define WIFI_SSID      "your-network-name"
#define WIFI_PASSWORD  "your-password"

const String api_key = "your-openweathermap-api-key";

// Set to at least 4 decimal places for accuracy
const String latitude  = "40.749778527083656";
const String longitude = "-73.98629815117553";

#define TIMEZONE usCT   // see NTP_Time.h for all available zones

const String units = "metric";  // or "imperial"

#define SCREEN_BRIGHTNESS 150   // 0–255 fixed PWM level
```

**Available timezone references** (defined in [src/NTP_Time.h](src/NTP_Time.h)):

| Reference | Zone |
|-----------|------|
| `UK`      | United Kingdom (London/Belfast) |
| `euCET`   | Central European Time (Frankfurt/Paris) |
| `ausET`   | Australia Eastern (Sydney/Melbourne) |
| `usET`    | US Eastern (New York/Detroit) |
| `usCT`    | US Central (Chicago/Houston) |
| `usMT`    | US Mountain (Denver/Salt Lake City) |
| `usAZ`    | Arizona (Mountain, no DST) |
| `usPT`    | US Pacific (Los Angeles/Las Vegas) |

To add a timezone not listed, add `TimeChangeRule` pairs to `NTP_Time.h` following the existing pattern.

### 3. Install required libraries

PlatformIO pulls these automatically from `lib_deps` in `platformio.ini`:

| Library | Version | Source |
|---------|---------|--------|
| TFT_eSPI | 2.4.3+ | [Bodmer/TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) |
| OpenWeather | Feb 16 2023+ | [Bodmer/OpenWeather](https://github.com/Bodmer/OpenWeather) |
| JSON_Decoder | — | **GitHub only** — [Bodmer/JSON_Decoder](https://github.com/Bodmer/JSON_Decoder) ⚠️ do not use the IDE library manager version |
| TJpg_Decoder | 1.1.0+ | [Bodmer/TJpg_Decoder](https://github.com/Bodmer/TJpg_Decoder) |
| Timezone | 1.2.4 | [JChristensen/Timezone](https://github.com/JChristensen/Timezone) |
| Time | — | Arduino Library Manager (PaulStoffregen) |

> **Note:** Compiler warnings about AVR/ESP32 architecture mismatch from the Timezone library are benign and can be ignored.

### 4. Upload assets and flash

The sketch and filesystem live in separate flash partitions — uploads are independent.

> ⚠️ On a **brand new device**, upload the filesystem at least once before powering up — the sketch will hang on boot with "Flash FS initialisation failed!" if LittleFS has never been written.

**Step 1 — upload the filesystem** (only needed once, or when `data/` assets change):
- VS Code: PlatformIO sidebar → Project Tasks → `esp32-cyd-st7789` → Platform → **Upload Filesystem Image**
- CLI: `pio run --target uploadfs`

**Step 2 — flash the sketch** (any time code changes):
- VS Code: Project Tasks → **Upload**
- CLI: `pio run --target upload`

## Using PlatformIO (VS Code)

[PlatformIO](https://docs.platformio.org) is an alternative to Arduino IDE that integrates directly into VS Code.

### Project structure

```
esp32-cyd-weather/
├── platformio.ini
├── src/          ← all sketch source files
└── data/         ← LittleFS assets (icons, fonts, splash)
```

### `platformio.ini`

```ini
[env:esp32-cyd-st7789]
platform = espressif32
framework = arduino
board = esp32dev

board_build.filesystem = littlefs
; "default.csv" gives exactly 1.5 MB to LittleFS — meets the minimum
; Switch to "no_ota" for 2 MB if you add extra assets
board_build.partitions = default.csv

monitor_speed = 250000

lib_deps =
    bodmer/TFT_eSPI @ ^2.4.3
    https://github.com/Bodmer/OpenWeather    ; GitHub only — not in PlatformIO registry
    https://github.com/Bodmer/JSON_Decoder   ; GitHub only — do not use registry version
    bodmer/TJpg_Decoder @ ^1.1.0
    JChristensen/Timezone @ ^1.2.4
    PaulStoffregen/Time
```

> If you hit display issues swap `bodmer/TFT_eSPI` for `https://github.com/AndroidCrypto/TFT_eSPI`.

### TFT_eSPI driver configuration

With PlatformIO you configure TFT_eSPI via `build_flags` instead of copying header files into the library. Add to `platformio.ini` under `[env:esp32-cyd-st7789]`:

```ini
build_flags =
    -DUSER_SETUP_LOADED
    -DILI9341_2_DRIVER        ; older CYD (single USB) — swap for -DST7789_DRIVER on newer dual-USB
    -DTFT_WIDTH=240
    -DTFT_HEIGHT=320
    -DTFT_BL=21
    -DTFT_BACKLIGHT_ON=HIGH
    -DTFT_MISO=12
    -DTFT_MOSI=13
    -DTFT_SCLK=14
    -DTFT_CS=15
    -DTFT_DC=2
    -DTFT_RST=-1
    -DTOUCH_CS=33
    -DLOAD_GLCD -DLOAD_FONT2 -DLOAD_FONT4 -DLOAD_FONT6 -DLOAD_FONT7 -DLOAD_FONT8
    -DLOAD_GFXFF -DSMOOTH_FONT
    -DSPI_FREQUENCY=55000000
    -DSPI_READ_FREQUENCY=20000000
    -DSPI_TOUCH_FREQUENCY=2500000
    -DUSE_HSPI_PORT
```

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| White/inverted colours | Confirm `-DTFT_INVERSION_OFF` is in build_flags for the ST7789 panel |
| Blank/garbled display | Verify the correct TFT_eSPI driver is set in `platformio.ini` build_flags |
| Hangs on "Flash FS initialisation failed!" | LittleFS partition too small, or filesystem not yet uploaded (run Step 1 above) |
| Weather data shows "Failed to get data points" | Check API key and lat/long in `All_Settings.h`; verify WiFi via Serial monitor at 250000 baud |
| Corrupted filesystem after failed upload | Uncomment `#define FORMAT_LittleFS` in the main `.ino`, flash once to wipe, then re-comment and re-upload |
| Time shows wrong zone | Ensure `#define TIMEZONE` in `All_Settings.h` matches a zone defined in `NTP_Time.h` |
| Brightness too dim or too bright | Adjust `#define SCREEN_BRIGHTNESS` in `All_Settings.h` (0–255) and re-flash |

## Debug Options

Uncomment these defines in the main `.ino` to enable additional output:

```cpp
#define SERIAL_MESSAGES  // Print full forecast data to serial monitor (on by default)
#define SCREEN_SERVER    // Enable TCP screenshot server
#define RANDOM_LOCATION  // Pick a random location each refresh (icon testing)
#define FORMAT_LittleFS  // Wipe and reformat LittleFS on boot — re-comment immediately after use!
```

## Development Environment

```
VS Code with PlatformIO IDE extension
espressif32 platform (arduino-esp32 boards 3.2.0)  https://github.com/espressif/arduino-esp32
```

## Credits

Original sketch by [Daniel Eichhorn](https://blog.squix.ch), adapted by [Bodmer](https://github.com/Bodmer/OpenWeather) for the OpenWeather library. Extended for the CYD platform with moon phase display, barometric pressure, cloud cover, humidity, and forecast strip by [AndroidCrypto](https://github.com/AndroidCrypto). Further extended with a 2-page carousel, night mode, feels-like temperature, pressure trend arrow, and motivational quotes by [Pranav E K](https://github.com/pranavek).
