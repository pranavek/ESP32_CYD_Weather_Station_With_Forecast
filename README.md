# ESP32 Cheap Yellow Display (CYD) Weather Station with 3-Day Forecast

Companion repository for the article [Create an Internet Weather Station with 3 days Forecast on an ESP32 Cheap Yellow Display ("CYD")](https://medium.com/@androidcrypto/create-an-internet-weather-station-with-3-days-forecast-on-an-esp32-cheap-yellow-display-cyd-15eb5c353b1d).


## Features

- **Current conditions** — weather icon (100×100 px), description text, temperature (large font), wind speed + compass direction icon, barometric pressure
- **4-day forecast strip** — day abbreviation, high/low temperatures, 50×50 px weather icon for each day
- **Astronomy panel** — sunrise/sunset times, moon phase icon (24 phases) + phase name, cloud cover %, humidity %
- **Live clock** — HH:MM updated every minute via NTP (timezone + DST aware)
- **Auto-refresh** — weather data fetched every 15 minutes (configurable); respects the free OpenWeatherMap tier of ~40 requests/hour

## Display Layout

```
┌─────────────────────────────┐  ↑
│  Updated: Apr 28  14:32     │  Header (date + time)
│         14:32               │
├─────────────────────────────┤
│  [icon]  Partly Cloudy  18° │  Current weather
│          3 m/s  ↗  1013 hPa │
├─────────────────────────────┤
│ MON  TUE  WED  THU          │  4-day forecast
│ [i]  [i]  [i]  [i]          │
│18 12 17 11 15 9 14 8        │
├─────────────────────────────┤
│ Sun  05:42        ☽  Waxing │  Astronomy
│      20:11    Cloud  23%    │
│               Humidity 61%  │
└─────────────────────────────┘  ↓ (320 px)
```

## Getting Started

### 1. Get an OpenWeatherMap API key

Sign up for a free account at [openweathermap.org](https://openweathermap.org/). The free tier allows up to 1000 requests/day (~40/hour), which is well above the 15-minute update interval used here.

### 2. Configure `All_Settings.h`

Edit `src/All_Settings.h`:

```cpp
#define WIFI_SSID      "your-network-name"
#define WIFI_PASSWORD  "your-password"

const String api_key  = "your-openweathermap-api-key";

// Set to at least 4 decimal places for accuracy
const String latitude  = "40.749778527083656";
const String longitude = "-73.98629815117553";

#define TIMEZONE euCET   // see NTP_Time.h for all available zones

const String units = "metric";  // or "imperial"
```

**Available timezone references** (defined in `NTP_Time.h`):

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

> ⚠️ Upload the filesystem **before** the sketch — the sketch halts on boot if LittleFS is empty.

**Step 1 — upload the filesystem (icons, fonts):**
- VS Code: PlatformIO sidebar → Project Tasks → `esp32-cyd-st7789` → Platform → **Upload Filesystem Image**
- CLI: `pio run --target uploadfs`

**Step 2 — flash the sketch:**
- VS Code: Project Tasks → **Upload**
- CLI: `pio run --target upload`

## Using PlatformIO (VS Code)

[PlatformIO](https://docs.platformio.org) is an alternative to Arduino IDE that integrates directly into VS Code.

### Project structure

This repository is already structured for PlatformIO:

```
esp32-cyd-weather/
├── platformio.ini
├── src/          ← all sketch source files
└── data/         ← LittleFS assets (icons, fonts, splash)
```

### `platformio.ini`

```ini
[env:esp32-cyd]
platform = espressif32
framework = arduino
board = esp32dev

; LittleFS filesystem
board_build.filesystem = littlefs

; "default" partition gives exactly 1.5 MB to LittleFS — meets the minimum
; Switch to "no_ota" for 2 MB if you add extra assets
board_build.partitions = default

monitor_speed = 250000

lib_deps =
    bodmer/TFT_eSPI @ ^2.4.3
    bodmer/OpenWeather
    https://github.com/Bodmer/JSON_Decoder   ; GitHub only — do not use registry version
    bodmer/TJpg_Decoder @ ^1.1.0
    JChristensen/Timezone @ ^1.2.4
    PaulStoffregen/Time
```

> If you hit display issues swap `bodmer/TFT_eSPI` for `https://github.com/AndroidCrypto/TFT_eSPI`.

### TFT_eSPI driver configuration

With PlatformIO you configure TFT_eSPI via `build_flags` instead of copying header files into the library. Add to `platformio.ini` under `[env:esp32-cyd]`:

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

### Upload filesystem and sketch

> ⚠️ Always upload the filesystem **before** the sketch — the sketch halts on boot if LittleFS is empty.

**Step 1 — upload the filesystem image:**

- VS Code: PlatformIO sidebar → Project Tasks → `esp32-cyd` → Platform → **Upload Filesystem Image**
- CLI: `pio run --target uploadfs`

**Step 2 — flash the sketch:**

- VS Code: Project Tasks → **Upload**
- CLI: `pio run --target upload`

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Blank/garbled display | Verify the correct TFT_eSPI setup header is active in `User_Setup_Select.h` |
| Hangs on "Flash FS initialisation failed!" | LittleFS partition too small, or data files not yet uploaded |
| Weather data shows "Failed to get data points" | Check API key and lat/long in `All_Settings.h`; verify WiFi connectivity via Serial monitor |
| Corrupted filesystem after failed upload | Uncomment `#define FORMAT_LittleFS` in the main `.ino`, flash once to wipe, then re-comment and re-upload data + sketch |
| Time shows wrong zone | Ensure `#define TIMEZONE` in `All_Settings.h` matches a zone defined in `NTP_Time.h` |

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
Arduino IDE 2.3.6 (Windows)
arduino-esp32 boards 3.2.0  https://github.com/espressif/arduino-esp32
```

## Credits

Original sketch by [Daniel Eichhorn](https://blog.squix.ch), adapted by [Bodmer](https://github.com/Bodmer/OpenWeather) for the OpenWeather library. Further adapted for the CYD platform and extended with moon phase display, 4th forecast day, barometric pressure, cloud cover, and humidity by AndroidCrypto.
