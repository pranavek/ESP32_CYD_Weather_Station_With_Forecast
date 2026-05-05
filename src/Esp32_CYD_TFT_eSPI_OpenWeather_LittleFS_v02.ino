/*
  Internet Weather Station for the ESP32 "Cheap Yellow Display" (CYD) — 2.8" TFT, 320x240.

  Weather data is fetched from Open-Meteo (https://open-meteo.com/) — no API key required.
  Configure Wi-Fi credentials, location lat/lon, units, and timezone in 'All_Settings.h'.

  Originally by Daniel Eichhorn (https://blog.squix.ch); adapted by Bodmer for the
  OpenWeatherMap library, then ported to Open-Meteo. See license at end of file.

  The code is optimized for the 'ESP32-2432S028R' CYD variant (240x320, portrait orientation).

  Weather icons and fonts live on LittleFS in the 'data/' subfolder — upload the filesystem
  image before flashing the sketch (see README). LittleFS partition must be >= 1.5 MB.

  Fonts were generated from the Noto family (https://www.google.com/get/noto/).
  JSON parsing uses ArduinoJson (https://arduinojson.org/).
*/

#define SERIAL_MESSAGES  // For serial output weather reports
//#define SCREEN_SERVER   // For dumping screen shots from TFT
//#define RANDOM_LOCATION // Test only, selects random weather location every refresh
//#define FORMAT_LittleFS   // Wipe LittleFS and all files!

const char* PROGRAM_VERSION = "ESP32 CYD Open-Meteo LittleFS V03";

#include <FS.h>
#include <LittleFS.h>

#define AA_FONT_SMALL "fonts/NSBold15"  // 15 point Noto sans serif bold
#define AA_FONT_LARGE "fonts/NSBold36"  // 36 point Noto sans serif bold
/***************************************************************************************
**                          Load the libraries and settings
***************************************************************************************/
#include <Arduino.h>

#include <SPI.h>
#include <TFT_eSPI.h>  // https://github.com/Bodmer/TFT_eSPI

// Additional functions
#include "GfxUi.h"  // Attached to this sketch

// Choose library to load
#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif defined(ARDUINO_ARCH_MBED) || defined(ARDUINO_ARCH_RP2040)
#if defined(ARDUINO_RASPBERRY_PI_PICO_W)
#include <WiFi.h>
#else
#include <WiFiNINA.h>
#endif
#else  // ESP32
#include <WiFi.h>
#endif


// User-facing settings.
#include "All_Settings.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>  // https://arduinojson.org/

#include "NTP_Time.h"  // Attached to this sketch, see that tab for library needs
// Time zone correction library: https://github.com/JChristensen/Timezone


/***************************************************************************************
**                          Define the globals and class instances
***************************************************************************************/

TFT_eSPI tft = TFT_eSPI();  // Invoke custom library

// Open-Meteo response, parsed into the fields we use. Heap-allocated each
// updateData() call and freed at the end, like the old OW_forecast.
struct OMForecast {
  // Current conditions
  uint16_t code;
  float    temp;
  float    feels_like;
  uint8_t  humidity;
  float    pressure;       // hPa
  float    wind_speed;
  uint16_t wind_deg;
  uint8_t  clouds;         // %
  uint32_t visibility;     // m

  // Hourly forecast — 16h ahead, 1h spacing. Indices 0/3/6/9 give the next
  // four 3-hour-spaced slots used by the bottom forecast strip.
  static const int HOURLY_COUNT = 16;
  time_t   hourly_dt[HOURLY_COUNT];
  uint16_t hourly_code[HOURLY_COUNT];
  float    hourly_temp[HOURLY_COUNT];
  uint8_t  hourly_pop[HOURLY_COUNT];      // %

  // Daily forecast — today (index 0) + next 4 days
  static const int DAILY_COUNT = 5;
  time_t   daily_dt[DAILY_COUNT];
  uint16_t daily_code[DAILY_COUNT];
  float    daily_max[DAILY_COUNT];
  float    daily_min[DAILY_COUNT];
  uint8_t  daily_pop_max[DAILY_COUNT];    // %
  time_t   daily_sunrise[DAILY_COUNT];
  time_t   daily_sunset[DAILY_COUNT];
};

OMForecast* forecast;

boolean booted = true;

float prevPressure = 0.0f;

GfxUi ui = GfxUi(&tft);  // Jpeg and bmpDraw functions

long lastDownloadUpdate = millis();

// ─── Cached forecast data — populated each updateData(), used by all page draws ───
// (forecast pointer is deleted after fetch; pages need data without it.)
struct SlotCache {
  time_t   dt;
  float    temp;
  uint16_t id;
  float    pop;
};
struct DayCache {
  uint8_t  dow;       // 1..7 (Sun..Sat)
  float    high;
  float    low;
  uint16_t id;        // representative id (mid-day slot)
  float    pop;       // max pop across the day
};

SlotCache slotCache[4];
DayCache  dayCache[4];
time_t    cachedSunrise = 0;
time_t    cachedSunset  = 0;
uint8_t   cachedHumidity = 0;
uint8_t   cachedClouds = 0;
uint8_t   cachedMoonIcon = 0;
uint8_t   cachedMoonPhaseIdx = 0;
bool      cacheValid = false;


// Carousel state
uint8_t        currentPage    = 0;
unsigned long  lastPageCycle  = 0;

/***************************************************************************************
**                          Declare prototypes
***************************************************************************************/
void updateData();
void drawProgress(uint8_t percentage, String text);
void drawTime();
void drawCurrentWeather();
void drawForecast();
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex);
const char* getMeteoconIcon(uint16_t code, time_t when);
const char* weatherCodeLabel(uint16_t code);
void drawAstronomy();
void drawSeparator(uint16_t y);
void fillSegment(int x, int y, int start_angle, int sub_angle, int r, unsigned int colour);
String strDate(time_t unixTime);
String strTime(time_t unixTime);
void printWeather(void);
bool fetchOpenMeteo(OMForecast* f);
int leftOffset(String text, String sub);
int rightOffset(String text, String sub);
int splitIndex(String text);
void cacheForecastData(void);
uint8_t moon_phase(int year, int month, int day, double hour, int* ip);
void drawBottomSections(void);
void drawQuote(void);
void drawDailyForecast(void);
void setBacklight(uint8_t level);
void updateBrightness(bool nightMode);

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  // Stop further decoding as image is running off bottom of screen
  if (y >= tft.height()) return 0;

  // This function will clip the image block rendering automatically at the TFT boundaries
  tft.pushImage(x, y, w, h, bitmap);

  // Return 1 to decode next block
  return 1;
}

/***************************************************************************************
**                          Setup
***************************************************************************************/
void setup() {
  Serial.begin(250000);
  delay(500);
  Serial.println(PROGRAM_VERSION);

  tft.begin();
  tft.setRotation(2);  // 180° portrait
  tft.fillScreen(TFT_BLACK);

  // PWM backlight on TFT_BL — fixed level from SCREEN_BRIGHTNESS.
  ledcSetup(0, 5000, 8);          // channel 0, 5 kHz, 8-bit
  ledcAttachPin(TFT_BL, 0);
  setBacklight(SCREEN_BRIGHTNESS);

  if (!LittleFS.begin()) {
    Serial.println("Flash FS initialisation failed!");
    while (1) yield();  // Stay here twiddling thumbs waiting
  }
  Serial.println("\nFlash FS available!");

// Enable if you want to erase LittleFS, this takes some time!
// then disable and reload sketch to avoid reformatting on every boot!
#ifdef FORMAT_LittleFS
  tft.setTextDatum(BC_DATUM);  // Bottom Centre datum
  tft.drawString("Formatting LittleFS, so wait!", 120, 195);
  LittleFS.format();
#endif

  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tft_output);
  TJpgDec.setSwapBytes(true);  // May need to swap the jpg colour bytes (endianess)

  // Draw splash screen
  if (LittleFS.exists("/splash/OpenWeather.jpg") == true) {
    TJpgDec.drawFsJpg(0, 40, "/splash/OpenWeather.jpg", LittleFS);
  }

  delay(2000);

  // Clear bottom section of screen
  tft.fillRect(0, 206, 240, 320 - 206, TFT_BLACK);

  tft.loadFont(AA_FONT_SMALL, LittleFS);
  tft.setTextDatum(BC_DATUM);  // Bottom Centre datum
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

  tft.drawString("Original by: blog.squix.org", 120, 260);
  tft.drawString("Adapted by: Bodmer", 120, 280);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);

  delay(2000);

  tft.fillRect(0, 206, 240, 320 - 206, TFT_BLACK);

  tft.drawString("Connecting to WiFi", 120, 240);
  tft.setTextPadding(240);  // Pad next drawString() text to full width to over-write old text

// Call once for ESP32 and ESP8266
#if !defined(ARDUINO_ARCH_MBED)
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
#if defined(ARDUINO_ARCH_MBED) || defined(ARDUINO_ARCH_RP2040)
    if (WiFi.status() != WL_CONNECTED) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif
    delay(500);
  }
  Serial.println();

  tft.setTextDatum(BC_DATUM);
  tft.setTextPadding(240);        // Pad next drawString() text to full width to over-write old text
  tft.drawString(" ", 120, 220);  // Clear line above using set padding width
  tft.drawString("Fetching weather data...", 120, 240);

  // Fetch the time
  udp.begin(localPort);
  syncTime();

  tft.unloadFont();
}

/***************************************************************************************
**                          Loop
***************************************************************************************/
void loop() {
  time_t local_t = TIMEZONE.toLocal(now(), &tz1_Code);
  uint8_t h = hour(local_t);
  uint8_t m = minute(local_t);
  bool nightMode = !booted &&
                   ((h < NIGHT_ON_HOUR) || (h == NIGHT_OFF_HOUR && m >= 59));

  updateBrightness(nightMode);

  // Weather update — timer not advanced during night so wake-up fetch is immediate
  if (booted || (millis() - lastDownloadUpdate > 1000UL * UPDATE_INTERVAL_SECS)) {
    if (!nightMode) {
      updateData();
      lastDownloadUpdate = millis();
    }
  }

  // Clock update — skip draw and NTP sync during night
  if (booted || minute() != lastMinute) {
    lastMinute = minute();
    if (!nightMode) {
      drawTime();
      syncTime();
    }
#ifdef SCREEN_SERVER
    screenServer();
#endif
  }

  // Carousel — cycle bottom sections every 15 seconds
  if (!nightMode && cacheValid && (millis() - lastPageCycle >= 15000UL)) {
    lastPageCycle = millis();
    currentPage = (currentPage + 1) % PAGE_COUNT;
    tft.loadFont(AA_FONT_SMALL, LittleFS);
    tft.fillRect(0, 154, 240, 166, TFT_BLACK);
    drawBottomSections();
    tft.unloadFont();
  }

  booted = false;
}

/***************************************************************************************
**                          Fetch the weather data  and update screen
***************************************************************************************/
// Update the Internet based information and update screen
void updateData() {
  // booted = true;  // Test only
  // booted = false; // Test only

  if (booted) drawProgress(20, "Updating time...");
  else fillSegment(22, 22, 0, (int)(20 * 3.6), 16, TFT_NAVY);

  if (booted) drawProgress(50, "Updating conditions...");
  else fillSegment(22, 22, 0, (int)(50 * 3.6), 16, TFT_NAVY);

  // Create the structure that holds the retrieved weather
  forecast = new OMForecast;

  String lat = latitude;
  String lon = longitude;

#ifdef RANDOM_LOCATION  // Randomly choose a place on Earth to test icons etc
  lat = String(random(180) - 90);
  lon = String(random(360) - 180);
  Serial.print("Lat = ");
  Serial.print(lat);
  Serial.print(", Lon = ");
  Serial.println(lon);
#endif

  bool parsed = fetchOpenMeteo(forecast);

  if (parsed) Serial.println("Data points received");
  else Serial.println("Failed to get data points");

  //Serial.print("Free heap = "); Serial.println(ESP.getFreeHeap(), DEC);

  printWeather();  // For debug, turn on output with #define SERIAL_MESSAGES

  if (booted) {
    drawProgress(100, "Done...");
    delay(2000);
    tft.fillScreen(TFT_BLACK);
  } else {
    fillSegment(22, 22, 0, 360, 16, TFT_NAVY);
    fillSegment(22, 22, 0, 360, 22, TFT_BLACK);
  }

  if (parsed) {
    cacheForecastData();    // snapshot needed values before forecast is deleted

    tft.loadFont(AA_FONT_SMALL, LittleFS);
    drawCurrentWeather();
    tft.fillRect(0, 154, 240, 166, TFT_BLACK);  // clear bottom region for fresh page
    drawBottomSections();
    tft.unloadFont();

    // Update the temperature here so we don't need to keep
    // loading and unloading font which takes time
    tft.loadFont(AA_FONT_LARGE, LittleFS);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);

    // Font ASCII code 0xB0 is a degree symbol, but o used instead in small font
    tft.setTextPadding(tft.textWidth(" -88"));  // Max width of values

    String weatherText = "";
    weatherText = String(forecast->temp, 0);     // Make it integer temperature
    tft.drawString(weatherText, 215, 95);        //  + "°" symbol is big... use o in small font
    tft.unloadFont();
  } else {
    Serial.println("Failed to get weather");
  }

  // Delete to free up space
  delete forecast;
  forecast = nullptr;

}

/***************************************************************************************
**                          Update progress bar
***************************************************************************************/
void drawProgress(uint8_t percentage, String text) {
  tft.loadFont(AA_FONT_SMALL, LittleFS);
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(240);
  tft.drawString(text, 120, 260);

  ui.drawProgressBar(10, 269, 240 - 20, 15, percentage, TFT_WHITE, TFT_BLUE);

  tft.setTextPadding(0);
  tft.unloadFont();
}

/***************************************************************************************
**                          Draw the clock digits
***************************************************************************************/
void drawTime() {
  // Date — redraws every minute so midnight crossover is always correct
  tft.loadFont(AA_FONT_SMALL, LittleFS);
  time_t local_time = TIMEZONE.toLocal(now(), &tz1_Code);
  String date = String(dayShortStr(weekday(local_time))) + "  " +
                String(monthShortStr(month(local_time))) + " " +
                String(day(local_time)) + "  " +
                String(year(local_time));
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" Www  Mmm 44  4444 "));
  tft.drawString(date, 120, 16);
  tft.unloadFont();

  tft.loadFont(AA_FONT_LARGE, LittleFS);

  // Convert UTC to local time, returns zone code in tz1_Code, e.g "GMT"
  local_time = TIMEZONE.toLocal(now(), &tz1_Code);

  String timeNow = "";

  if (hour(local_time) < 10) timeNow += "0";
  timeNow += hour(local_time);
  timeNow += ":";
  if (minute(local_time) < 10) timeNow += "0";
  timeNow += minute(local_time);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" 44:44 "));  // String width + margin
  tft.drawString(timeNow, 120, 53);

  drawSeparator(51);

  tft.setTextPadding(0);

  tft.unloadFont();

}

/***************************************************************************************
**                          Draw the current weather
***************************************************************************************/
void drawCurrentWeather() {
  String weatherText = "None";
  String weatherIcon = "";

  weatherIcon = getMeteoconIcon(forecast->code, now());

  ui.drawBmp("/icon/" + weatherIcon + ".bmp", 0, 53);

  // Weather text — derived from the WMO weather code (Open-Meteo doesn't ship a label).
  weatherText = weatherCodeLabel(forecast->code);

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);

  int splitPoint = 0;
  int xpos = 235;
  splitPoint = splitIndex(weatherText);

  tft.setTextPadding(xpos - 100);  // xpos - icon width
  tft.drawString(splitPoint ? weatherText.substring(0, splitPoint) : weatherText, xpos, 69);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("feels " + String(forecast->feels_like, 0) + "o", xpos, 86);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(0);
  if (units == "metric") tft.drawString("oC", 237, 95);
  else tft.drawString("oF", 237, 95);

  //Temperature large digits added in updateData() to save swapping font here

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  weatherText = String(forecast->wind_speed, 0);

  if (units == "metric") weatherText += " m/s";
  else weatherText += " mph";

  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(tft.textWidth("888 m/s"));  // Max string length?
  tft.drawString(weatherText, 124, 136);

  float curPressure = forecast->pressure;
  if (units == "imperial") {
    weatherText = String(curPressure, 2) + " in";
  } else {
    weatherText = String(curPressure, 0) + " hPa";
    if (prevPressure > 0.0f) {
      if      (curPressure > prevPressure + 0.5f) weatherText += "^";
      else if (curPressure < prevPressure - 0.5f) weatherText += "v";
    }
  }
  prevPressure = curPressure;

  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(tft.textWidth(" 8888hPa^"));
  tft.drawString(weatherText, 230, 136);

  int windAngle = (forecast->wind_deg + 22.5) / 45;
  if (windAngle > 7) windAngle = 0;
  String wind[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
  ui.drawBmp("/wind/" + wind[windAngle] + ".bmp", 101, 86);

  drawSeparator(153);

  tft.setTextDatum(TL_DATUM);  // Reset datum to normal
  tft.setTextPadding(0);       // Reset padding width to none
}

/***************************************************************************************
**                          Draw the 4 forecast columns
***************************************************************************************/
// draws the four forecast columns (next four 3-hour slots from now) using cached data
void drawForecast() {
  drawForecastDetail(8,   171, 0);
  drawForecastDetail(66,  171, 1);
  drawForecastDetail(124, 171, 2);
  drawForecastDetail(182, 171, 3);
  drawSeparator(171 + 69);
}

/***************************************************************************************
**                          Draw 1 forecast column at x, y
***************************************************************************************/
// helper for the forecast columns — uses cached slot data so it works during page cycling
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t slotIndex) {
  if (slotIndex >= 4) return;
  SlotCache& s = slotCache[slotIndex];
  if (s.dt == 0) return;

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth("00:00"));
  tft.drawString(strTime(s.dt), x + 25, y);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" -88 "));
  tft.drawString(String(s.temp, 0), x + 25, y + 17);

  String weatherIcon = getMeteoconIcon(s.id, s.dt);
  ui.drawBmp("/icon50/" + weatherIcon + ".bmp", x, y + 18);

  tft.setTextPadding(0);
}

/***************************************************************************************
**                          Draw Sun rise/set, Moon, cloud cover and humidity
***************************************************************************************/
void drawAstronomy() {
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" Last qtr "));

  tft.drawString(moonPhase[cachedMoonPhaseIdx], 120, 319);
  ui.drawBmp("/moon/moonphase_L" + String(cachedMoonIcon) + ".bmp", 120 - 30, 318 - 16 - 60);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(0);
  tft.drawString(sunStr, 40, 270);

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" 88:88 "));

  String rising = strTime(cachedSunrise) + " ";
  int dt = rightOffset(rising, ":");
  tft.drawString(rising, 40 + dt, 290);

  String setting = strTime(cachedSunset) + " ";
  dt = rightOffset(setting, ":");
  tft.drawString(setting, 40 + dt, 305);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString(cloudStr, 195, 260);

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth(" 100%"));
  tft.drawString(String(cachedClouds) + "%", 210, 277);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString(humidityStr, 195, 300 - 2);

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(tft.textWidth("100%"));
  tft.drawString(String(cachedHumidity) + "%", 210, 315);

  tft.setTextPadding(0);
}

/***************************************************************************************
**                          Get the icon file name from the index number
***************************************************************************************/
// Maps an Open-Meteo WMO weather code to a bitmap name in /icon/ or /icon50/.
// Day/night detection only matters for clear & partly-cloudy codes (0/1/2);
// everything else uses one icon regardless of time of day.
const char* getMeteoconIcon(uint16_t code, time_t when) {
  bool isNight = false;
  if (code <= 2 && cachedSunrise > 0) {
    // Compare seconds-of-day in UTC. When local sunset crosses midnight UTC
    // the day window wraps (sr > ss), so handle that case explicitly —
    // otherwise daytime hours after the ss-wrap test as night.
    auto sod = [](time_t t) { return hour(t) * 3600 + minute(t) * 60 + second(t); };
    long s = sod(when), sr = sod(cachedSunrise), ss = sod(cachedSunset);
    bool isDay = (sr <= ss) ? (s >= sr && s <= ss)
                            : (s >= sr || s <= ss);
    isNight = !isDay;
  }
  switch (code) {
    case 0:  // Clear sky
    case 1:  // Mainly clear
      return isNight ? "clear-night" : "clear-day";
    case 2:  // Partly cloudy
      return isNight ? "partly-cloudy-night" : "partly-cloudy-day";
    case 3:  // Overcast
      return "cloudy";
    case 45: case 48:                       // Fog / rime fog
      return "fog";
    case 51: case 53: case 55:              // Drizzle
      return "drizzle";
    case 56: case 57:                       // Freezing drizzle
    case 66: case 67:                       // Freezing rain
      return "sleet";
    case 61:                                // Slight rain
    case 80: case 81:                       // Slight/moderate rain showers
      return "lightRain";
    case 63: case 65:                       // Moderate / heavy rain
    case 82:                                // Violent rain showers
      return "rain";
    case 71: case 73: case 75: case 77:     // Snow / snow grains
    case 85: case 86:                       // Snow showers
      return "snow";
    case 95: case 96: case 99:              // Thunderstorm (with hail)
      return "thunderstorm";
    default:
      return "unknown";
  }
}

// Short text label per WMO code, used where OWM previously supplied
// `main`/`description`. Kept under ~14 chars so splitIndex() can wrap on a space.
const char* weatherCodeLabel(uint16_t code) {
  switch (code) {
    case 0:  return "Clear";
    case 1:  return "Mainly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45: return "Fog";
    case 48: return "Rime fog";
    case 51: return "Light drizzle";
    case 53: return "Drizzle";
    case 55: return "Heavy drizzle";
    case 56:
    case 57: return "Frz drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66:
    case 67: return "Frz rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80:
    case 81: return "Rain shower";
    case 82: return "Heavy shower";
    case 85:
    case 86: return "Snow shower";
    case 95: return "Thunder";
    case 96:
    case 99: return "Thunder hail";
    default: return "Unknown";
  }
}

/***************************************************************************************
**                          Draw screen section separator line
***************************************************************************************/
// if you don't want separators, comment out the tft-line
void drawSeparator(uint16_t y) {
  tft.drawFastHLine(10, y, 240 - 2 * 10, 0x4228);
}

/***************************************************************************************
**                          Determine place to split a line line
***************************************************************************************/
// determine the "space" split point in a long string
int splitIndex(String text) {
  uint16_t index = 0;
  while ((text.indexOf(' ', index) >= 0) && (index <= text.length() / 2)) {
    index = text.indexOf(' ', index) + 1;
  }
  if (index) index--;
  return index;
}

/***************************************************************************************
**                          Right side offset to a character
***************************************************************************************/
// Calculate coord delta from end of text String to start of sub String contained within that text
// Can be used to vertically right align text so for example a colon ":" in the time value is always
// plotted at same point on the screen irrespective of different proportional character widths,
// could also be used to align decimal points for neat formatting
int rightOffset(String text, String sub) {
  int index = text.indexOf(sub);
  return tft.textWidth(text.substring(index));
}

/***************************************************************************************
**                          Left side offset to a character
***************************************************************************************/
// Calculate coord delta from start of text String to start of sub String contained within that text
// Can be used to vertically left align text so for example a colon ":" in the time value is always
// plotted at same point on the screen irrespective of different proportional character widths,
// could also be used to align decimal points for neat formatting
int leftOffset(String text, String sub) {
  int index = text.indexOf(sub);
  return tft.textWidth(text.substring(0, index));
}

/***************************************************************************************
**                          Draw circle segment
***************************************************************************************/
// Draw a segment of a circle, centred on x,y with defined start_angle and subtended sub_angle
// Angles are defined in a clockwise direction with 0 at top
// Segment has radius r and it is plotted in defined colour
// Can be used for pie charts etc, in this sketch it is used for wind direction
#define DEG2RAD 0.0174532925  // Degrees to Radians conversion factor
#define INC 2                 // Minimum segment subtended angle and plotting angle increment (in degrees)
void fillSegment(int x, int y, int start_angle, int sub_angle, int r, unsigned int colour) {
  // Calculate first pair of coordinates for segment start
  float sx = cos((start_angle - 90) * DEG2RAD);
  float sy = sin((start_angle - 90) * DEG2RAD);
  uint16_t x1 = sx * r + x;
  uint16_t y1 = sy * r + y;

  // Draw colour blocks every INC degrees
  for (int i = start_angle; i < start_angle + sub_angle; i += INC) {

    // Calculate pair of coordinates for segment end
    int x2 = cos((i + 1 - 90) * DEG2RAD) * r + x;
    int y2 = sin((i + 1 - 90) * DEG2RAD) * r + y;

    tft.fillTriangle(x1, y1, x2, y2, x, y, colour);

    // Copy segment end to segment start for next segment
    x1 = x2;
    y1 = y2;
  }
}

/***************************************************************************************
**                          Open-Meteo HTTPS fetch + JSON parse
***************************************************************************************/
bool fetchOpenMeteo(OMForecast* f) {
  // Build the request URL. timeformat=unixtime gives us time_t directly;
  // timezone=auto lets the API bucket daily.* by local-day boundaries for the
  // configured lat/lon (sunrise/sunset remain absolute unix moments either way).
  String url = "https://api.open-meteo.com/v1/forecast"
               "?latitude=" + latitude +
               "&longitude=" + longitude +
               "&current=temperature_2m,apparent_temperature,relative_humidity_2m,pressure_msl,wind_speed_10m,wind_direction_10m,cloud_cover,weather_code,visibility"
               "&hourly=temperature_2m,weather_code,precipitation_probability"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,precipitation_probability_max"
               "&timeformat=unixtime"
               "&timezone=auto"
               "&forecast_days=5";
  if (units == "imperial") {
    url += "&temperature_unit=fahrenheit&wind_speed_unit=mph";
  } else {
    url += "&wind_speed_unit=ms";
  }

  WiFiClientSecure client;
  client.setInsecure();   // Open-Meteo uses LE certs; skipping validation matches
                          // typical CYD weather-display practice.
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    Serial.println("HTTPClient.begin() failed");
    return false;
  }
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP GET failed: %d\n", httpCode);
    http.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    return false;
  }

  // Current
  JsonObject cur = doc["current"];
  f->code        = cur["weather_code"]         | 0;
  f->temp        = cur["temperature_2m"]       | 0.0f;
  f->feels_like  = cur["apparent_temperature"] | 0.0f;
  f->humidity    = cur["relative_humidity_2m"] | 0;
  f->pressure    = cur["pressure_msl"]         | 0.0f;
  f->wind_speed  = cur["wind_speed_10m"]       | 0.0f;
  f->wind_deg    = cur["wind_direction_10m"]   | 0;
  f->clouds      = cur["cloud_cover"]          | 0;
  f->visibility  = cur["visibility"]           | 0;

  // Hourly — find first index where time > now, then capture HOURLY_COUNT entries.
  JsonArray h_time = doc["hourly"]["time"];
  JsonArray h_code = doc["hourly"]["weather_code"];
  JsonArray h_temp = doc["hourly"]["temperature_2m"];
  JsonArray h_pop  = doc["hourly"]["precipitation_probability"];
  time_t nowT = now();
  int startIdx = 0;
  for (int i = 0; i < (int)h_time.size(); i++) {
    if ((time_t)h_time[i].as<long>() > nowT) { startIdx = i; break; }
  }
  for (int i = 0; i < OMForecast::HOURLY_COUNT; i++) {
    int j = startIdx + i;
    if (j >= (int)h_time.size()) {
      f->hourly_dt[i] = 0;
      continue;
    }
    f->hourly_dt[i]   = (time_t)h_time[j].as<long>();
    f->hourly_code[i] = h_code[j].as<int>();
    f->hourly_temp[i] = h_temp[j].as<float>();
    f->hourly_pop[i]  = h_pop[j].isNull() ? 0 : (uint8_t)h_pop[j].as<int>();
  }

  // Daily — index 0 is today; cacheForecastData() skips it for the 4-day strip.
  JsonArray d_time    = doc["daily"]["time"];
  JsonArray d_code    = doc["daily"]["weather_code"];
  JsonArray d_max     = doc["daily"]["temperature_2m_max"];
  JsonArray d_min     = doc["daily"]["temperature_2m_min"];
  JsonArray d_sunrise = doc["daily"]["sunrise"];
  JsonArray d_sunset  = doc["daily"]["sunset"];
  JsonArray d_pop     = doc["daily"]["precipitation_probability_max"];
  for (int i = 0; i < OMForecast::DAILY_COUNT; i++) {
    if (i >= (int)d_time.size()) {
      f->daily_dt[i] = 0;
      continue;
    }
    f->daily_dt[i]      = (time_t)d_time[i].as<long>();
    f->daily_code[i]    = d_code[i].as<int>();
    f->daily_max[i]     = d_max[i].as<float>();
    f->daily_min[i]     = d_min[i].as<float>();
    f->daily_sunrise[i] = (time_t)d_sunrise[i].as<long>();
    f->daily_sunset[i]  = (time_t)d_sunset[i].as<long>();
    f->daily_pop_max[i] = d_pop[i].isNull() ? 0 : (uint8_t)d_pop[i].as<int>();
  }
  return true;
}

/***************************************************************************************
**                          Backlight PWM helper
***************************************************************************************/
void setBacklight(uint8_t level) {
  ledcWrite(0, level);  // channel 0 (attached to TFT_BL in setup)
}

void updateBrightness(bool nightMode) {
  setBacklight(nightMode ? 0 : SCREEN_BRIGHTNESS);
}

/***************************************************************************************
**                          Cache forecast data before forecast pointer is freed
***************************************************************************************/
void cacheForecastData(void) {
  // Slot cache: next 4 three-hour-spaced slots, sampled from the 1h-spaced
  // hourly array at indices 0/3/6/9 (fetchOpenMeteo already aligned index 0
  // to "first hour after now").
  for (int i = 0; i < 4; i++) {
    int idx = i * 3;
    if (idx < OMForecast::HOURLY_COUNT && forecast->hourly_dt[idx] != 0) {
      slotCache[i].dt   = forecast->hourly_dt[idx];
      slotCache[i].temp = forecast->hourly_temp[idx];
      slotCache[i].id   = forecast->hourly_code[idx];
      slotCache[i].pop  = forecast->hourly_pop[idx] / 100.0f;
    } else {
      slotCache[i].dt = 0;
    }
  }

  // Day cache: skip today (index 0), take next 4 days (indices 1..4).
  for (int d = 0; d < 4; d++) {
    int srcIdx = d + 1;
    if (srcIdx < OMForecast::DAILY_COUNT && forecast->daily_dt[srcIdx] != 0) {
      time_t local = TIMEZONE.toLocal(forecast->daily_dt[srcIdx], &tz1_Code);
      dayCache[d].dow  = weekday(local);
      dayCache[d].high = forecast->daily_max[srcIdx];
      dayCache[d].low  = forecast->daily_min[srcIdx];
      dayCache[d].id   = forecast->daily_code[srcIdx];
      dayCache[d].pop  = forecast->daily_pop_max[srcIdx] / 100.0f;
    } else {
      dayCache[d].dow = 0;
    }
  }

  // Singleton fields
  cachedSunrise    = forecast->daily_sunrise[0];
  cachedSunset     = forecast->daily_sunset[0];
  cachedHumidity   = forecast->humidity;
  cachedClouds     = forecast->clouds;
  // Moon — basis is "now" since the current block reflects current conditions.
  time_t local0 = TIMEZONE.toLocal(now(), &tz1_Code);
  int ip;
  cachedMoonIcon     = moon_phase(year(local0), month(local0), day(local0), hour(local0), &ip);
  cachedMoonPhaseIdx = ip;

  cacheValid = true;
}

/***************************************************************************************
**                          Page dispatcher
***************************************************************************************/
void drawBottomSections(void) {
  if (!cacheValid) return;
  switch (currentPage) {
    case 0:  // Hourly forecast + Astronomy
      drawForecast();
      drawAstronomy();
      break;
    case 1:  // 4-day forecast + random quote
      drawDailyForecast();
      drawSeparator(240);
      drawQuote();
      break;
  }
}

/***************************************************************************************
**                          Page 2 — Random quote
***************************************************************************************/
void drawQuote(void) {
  static const char* quotes[] = {
    "Be your own hero.",
    "Growth requires change.",
    "Progress over perfection.",
    "Keep moving forward.",
    "Trust the process.",
    "Learn from everything.",
    "Rise above it.",
    "Own your story.",
    "Make it happen.",
    "Less talk, more action.",
    "Dream big, act.",
    "Fortune favors bold.",
    "Seize the day.",
    "Begin the work.",
    "Success takes time.",
    "Stay hungry, stay humble.",
    "This too passes.",
    "Simple is beautiful.",
    "Choose kindness always.",
    "Breathe and release.",
    "Focus on good.",
    "Live and let live.",
    "Mind over matter.",
    "Silence is powerful.",
    "Integrity is everything.",
    "Courage over comfort.",
    "Stay true, stay you.",
    "Lead with heart.",
    "Grit defines you.",
    "Strength in softness.",
  };
  const uint8_t QUOTE_COUNT = sizeof(quotes) / sizeof(quotes[0]);
  uint8_t idx = (uint8_t)(esp_random() % QUOTE_COUNT);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextPadding(230);
  tft.drawString(quotes[idx], 120, 280);
  tft.setTextPadding(0);
}

/***************************************************************************************
**                          4-day daily forecast columns
***************************************************************************************/
static const char* dowAbbrev(uint8_t dow) {
  // weekday() returns 1=Sun..7=Sat
  static const char* names[8] = { "?", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  if (dow < 1 || dow > 7) return "?";
  return names[dow];
}

void drawDailyForecast(void) {
  // 4 columns at x = 8, 66, 124, 182 — same layout as 3-hour strip
  for (int i = 0; i < 4; i++) {
    DayCache& d = dayCache[i];
    int x = 8 + i * 58;
    int y = 171;
    if (d.dow == 0) continue;

    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setTextPadding(tft.textWidth("Wed"));
    tft.drawString(dowAbbrev(d.dow), x + 25, y);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextPadding(tft.textWidth("-88/-88"));
    tft.drawString(String(d.high, 0) + "/" + String(d.low, 0), x + 25, y + 17);

    // Daily column — always render the day icon by sampling midday.
    String icon = getMeteoconIcon(d.id, (cachedSunrise + cachedSunset) / 2);
    ui.drawBmp("/icon50/" + icon + ".bmp", x, y + 18);
  }
  tft.setTextPadding(0);
}

/***************************************************************************************
**                          Print the weather info to the Serial Monitor
***************************************************************************************/
void printWeather(void) {
#ifdef SERIAL_MESSAGES
  if (!forecast) return;
  Serial.println("Weather from Open-Meteo\n");

  Serial.print("Latitude            : "); Serial.println(latitude);
  Serial.print("Longitude           : "); Serial.println(longitude);
  Serial.println();

  Serial.println("##############  Current  ##############");
  Serial.print("temp                : "); Serial.println(forecast->temp);
  Serial.print("feels_like          : "); Serial.println(forecast->feels_like);
  Serial.print("humidity            : "); Serial.println(forecast->humidity);
  Serial.print("pressure (hPa)      : "); Serial.println(forecast->pressure);
  Serial.print("wind_speed          : "); Serial.println(forecast->wind_speed);
  Serial.print("wind_deg            : "); Serial.println(forecast->wind_deg);
  Serial.print("cloud_cover (%)     : "); Serial.println(forecast->clouds);
  Serial.print("visibility (m)      : "); Serial.println(forecast->visibility);
  Serial.print("weather_code        : ");
  Serial.print(forecast->code); Serial.print(" (");
  Serial.print(weatherCodeLabel(forecast->code)); Serial.println(")");
  Serial.print("sunrise             : "); Serial.println(strTime(forecast->daily_sunrise[0]));
  Serial.print("sunset              : "); Serial.println(strTime(forecast->daily_sunset[0]));
  Serial.println();

  Serial.println("##############  Hourly (next 16h)  ##############");
  for (int i = 0; i < OMForecast::HOURLY_COUNT; i++) {
    if (forecast->hourly_dt[i] == 0) continue;
    Serial.print("dt:"); Serial.print(strTime(forecast->hourly_dt[i]));
    Serial.print("  temp:"); Serial.print(forecast->hourly_temp[i]);
    Serial.print("  code:"); Serial.print(forecast->hourly_code[i]);
    Serial.print(" ("); Serial.print(weatherCodeLabel(forecast->hourly_code[i])); Serial.print(")");
    Serial.print("  pop:"); Serial.println(forecast->hourly_pop[i]);
  }
  Serial.println();

  Serial.println("##############  Daily  ##############");
  for (int i = 0; i < OMForecast::DAILY_COUNT; i++) {
    if (forecast->daily_dt[i] == 0) continue;
    Serial.print("day:"); Serial.print(strDate(forecast->daily_dt[i]));
    Serial.print("  max:"); Serial.print(forecast->daily_max[i]);
    Serial.print("  min:"); Serial.print(forecast->daily_min[i]);
    Serial.print("  code:"); Serial.print(forecast->daily_code[i]);
    Serial.print(" ("); Serial.print(weatherCodeLabel(forecast->daily_code[i])); Serial.print(")");
    Serial.print("  pop:"); Serial.println(forecast->daily_pop_max[i]);
  }
#endif
}
/***************************************************************************************
**             Convert Unix time to a "local time" time string "12:34"
***************************************************************************************/
String strTime(time_t unixTime) {
  time_t local_time = TIMEZONE.toLocal(unixTime, &tz1_Code);

  String localTime = "";

  if (hour(local_time) < 10) localTime += "0";
  localTime += hour(local_time);
  localTime += ":";
  if (minute(local_time) < 10) localTime += "0";
  localTime += minute(local_time);

  return localTime;
}

/***************************************************************************************
**  Convert Unix time to a local date + time string "Oct 16 17:18", ends with newline
***************************************************************************************/
String strDate(time_t unixTime) {
  time_t local_time = TIMEZONE.toLocal(unixTime, &tz1_Code);

  String localDate = "";

  localDate += monthShortStr(month(local_time));
  localDate += " ";
  localDate += day(local_time);
  localDate += " " + strTime(unixTime);

  return localDate;
}

/**The MIT License (MIT)
  Copyright (c) 2015 by Daniel Eichhorn
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYBR_DATUM HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
  See more at http://blog.squix.ch
*/

//  Changes made by Bodmer:

//  Minor changes to text placement and auto-blanking out old text with background colour padding
//  Moon phase text added (not provided by OpenWeather)
//  Forecast text lines are automatically split onto two lines at a central space (some are long!)
//  Time is printed with colons aligned to tidy display
//  Min and max forecast temperatures spaced out
//  New smart splash startup screen and updated progress messages
//  Display does not need to be blanked between updates
//  Icons nudged about slightly to add wind direction + speed
//  Barometric pressure added

//  Adapted to use the OpenWeather library: https://github.com/Bodmer/OpenWeather
//  Moon phase/rise/set (not provided by OpenWeather) replace with  and cloud cover humidity
//  Created and added new 100x100 and 50x50 pixel weather icons, these are in the
//  sketch data folder, press Ctrl+K to view
//  Add moon icons, eliminate all downloads of icons (may lose server!)
//  Adapted to use anti-aliased fonts, tweaked coords
//  Added forecast for 4th day
//  Added cloud cover and humidity in lieu of Moon rise/set
//  Adapted to be compatible with ESP32
