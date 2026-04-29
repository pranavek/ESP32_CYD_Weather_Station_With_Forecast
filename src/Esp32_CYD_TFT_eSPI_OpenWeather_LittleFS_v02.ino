/*
  The sketch is displaying a nice Internet Weather Station with 3 4 days forecast on an
  ESP32-Cheap Yellow Display (CYD) with 2.8-inch TFT display and 320x240 pixels resolution.

  The weather data is retrieved from OpenWeatherMap.org service and you need to get your own 
  (free) API key before running the sketch.

  Please add the key, your Wi-Fi credentials, the location coordinates for the weather information and 
  additional data about your Timezone in the settings file: 'All_Settings.h'.

  The code is not written by me, but from Daniel Eichhorn (https://blog.squix.ch) and was 
  adapted by Bodmer as an example for his OpenWeather library (https://github.com/Bodmer/OpenWeather/).

  I changed lines of code to crrect some issues, for more information see: https://github.com/Bodmer/OpenWeather/issues/26
  1):
  Search for: String date = "Updated: " + strDate(local_time); 
  Change to:  String date = "Updated: " + strDate(now());
  2)
  Search for: if ( today && id/100 == 8 && (forecast->dt[0] < forecast->sunrise || forecast->dt[0] > forecast->sunset)) id += 1000;
  Change to:  if ( today && id/100 == 8 && (now() < forecast->sunrise || now() > forecast->sunset)) id += 1000;

  The code is optimized to run on the Cheap Yellow Display ('CYD') with a screen resolution of 320 x 240 pixels in Landscape
  orientation, that is the 'ESP32-2432S028R' variant.

  The weather icons and fonts are stored in ESP32's LittleFS filesystem, so you need to upload the files in the 'data' subfolder
  first before uploading the code and starting the device.
  See 'Upload_To_LittleFS.md' for more details about this.

              >>>       IMPORTANT TO PREVENT CRASHES      <<<
  >>>>>>  Set LittleFS to at least 1.5Mbytes before uploading files  <<<<<<  

  The sketch is using the TFT_eSPI library by Bodmer (https://github.com/Bodmer/TFT_eSPI), so please select the correct
  'User_Setups' file in the library folder. I prepare two files (see my GitHub Repository) that should work:
  For older device with one USB connector (chip driver ILI9341) use: 
    Setup801_ESP32_CYD_ILI9341_240x320.h
  New devices with two USB connectors (chip driver ST7789) require:
    Setup805_ESP32_CYD_ST7789_240x320.h

  Original by Daniel Eichhorn, see license at end of file.

*/

/*
  This sketch uses font files created from the Noto family of fonts as bitmaps
  generated from these fonts may be freely distributed:
  https://www.google.com/get/noto/

  A processing sketch to create new fonts can be found in the Tools folder of TFT_eSPI
  https://github.com/Bodmer/TFT_eSPI/tree/master/Tools/Create_Smooth_Font/Create_font
  New fonts can be generated to include language specific characters. The Noto family
  of fonts has an extensive character set coverage.

  Json streaming parser (do not use IDE library manager version) to use is here:
  https://github.com/Bodmer/JSON_Decoder
*/

#define SERIAL_MESSAGES  // For serial output weather reports
//#define SCREEN_SERVER   // For dumping screen shots from TFT
//#define RANDOM_LOCATION // Test only, selects random weather location every refresh
//#define FORMAT_LittleFS   // Wipe LittleFS and all files!

const char* PROGRAM_VERSION = "ESP32 CYD OpenWeatherMap LittleFS V02";

#include <FS.h>
#include <LittleFS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

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


// check All_Settings.h for adapting to your needs
#include "All_Settings.h"

#include <JSON_Decoder.h>  // https://github.com/Bodmer/JSON_Decoder

#include <OpenWeather.h>  // Latest here: https://github.com/Bodmer/OpenWeather

#include "NTP_Time.h"  // Attached to this sketch, see that tab for library needs
// Time zone correction library: https://github.com/JChristensen/Timezone

/***************************************************************************************
**                          Define the globals and class instances
***************************************************************************************/

TFT_eSPI tft = TFT_eSPI();  // Invoke custom library

OW_Weather ow;  // Weather forecast library instance

OW_forecast* forecast;

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
float     cachedWindGust = 0.0f;
float     cachedVisibility = 0.0f;
float     cachedDewPoint = 0.0f;
uint8_t   cachedMoonIcon = 0;
uint8_t   cachedMoonPhaseIdx = 0;
bool      cacheValid = false;

// New API data
uint8_t   airQualityIndex = 0;     // 1–5 (OWM scale); 0 = unknown
float     uvIndex = -1.0f;         // -1 = unknown
char      nwsAlert[40] = "";       // empty = no active alert

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
const char* getMeteoconIcon(uint16_t id, bool today);
void drawAstronomy();
void drawSeparator(uint16_t y);
void fillSegment(int x, int y, int start_angle, int sub_angle, int r, unsigned int colour);
String strDate(time_t unixTime);
String strTime(time_t unixTime);
void printWeather(void);
int leftOffset(String text, String sub);
int rightOffset(String text, String sub);
int splitIndex(String text);
int getNextSlotIndex(void);
void cacheForecastData(void);
uint8_t moon_phase(int year, int month, int day, double hour, int* ip);
void fetchAirQuality(void);
void fetchUVIndex(void);
void fetchNWSAlerts(void);
void drawBottomSections(void);
void drawHumanComfortDetails(void);
void drawDailyForecast(void);
void setBacklight(uint8_t level);

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

  // PWM backlight on TFT_BL — controls brightness via SCREEN_BRIGHTNESS
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

  setBacklight(nightMode ? 0 : SCREEN_BRIGHTNESS);

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
  forecast = new OW_forecast;

#ifdef RANDOM_LOCATION  // Randomly choose a place on Earth to test icons etc
  String latitude = "";
  latitude = (random(180) - 90);
  String longitude = "";
  longitude = (random(360) - 180);
  Serial.print("Lat = ");
  Serial.print(latitude);
  Serial.print(", Lon = ");
  Serial.println(longitude);
#endif

  bool parsed = ow.getForecast(forecast, api_key, latitude, longitude, units, language);

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
    weatherText = String(forecast->temp[0], 0);  // Make it integer temperature
    tft.drawString(weatherText, 215, 95);        //  + "°" symbol is big... use o in small font
    tft.unloadFont();
  } else {
    Serial.println("Failed to get weather");
  }

  // Delete to free up space
  delete forecast;
  forecast = nullptr;

  // Fetch supplementary data (after forecast is freed to keep peak heap low)
  if (parsed) {
    fetchAirQuality();
    fetchUVIndex();
#ifdef NWS_ALERTS
    fetchNWSAlerts();
#endif
  }
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

  String currentSummary = forecast->main[0];
  currentSummary.toLowerCase();

  weatherIcon = getMeteoconIcon(forecast->id[0], true);

  ui.drawBmp("/icon/" + weatherIcon + ".bmp", 0, 53);

  // Weather Text
  if (language == "en")
    weatherText = forecast->main[0];
  else
    weatherText = forecast->description[0];

  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);

  int splitPoint = 0;
  int xpos = 235;
  splitPoint = splitIndex(weatherText);

  tft.setTextPadding(xpos - 100);  // xpos - icon width
  tft.drawString(splitPoint ? weatherText.substring(0, splitPoint) : weatherText, xpos, 69);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("feels " + String(forecast->feels_like[0], 0) + "o", xpos, 86);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(0);
  if (units == "metric") tft.drawString("oC", 237, 95);
  else tft.drawString("oF", 237, 95);

  //Temperature large digits added in updateData() to save swapping font here

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  weatherText = String(forecast->wind_speed[0], 0);

  if (units == "metric") weatherText += " m/s";
  else weatherText += " mph";

  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(tft.textWidth("888 m/s"));  // Max string length?
  tft.drawString(weatherText, 124, 136);

  float curPressure = forecast->pressure[0];
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

  int windAngle = (forecast->wind_deg[0] + 22.5) / 45;
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
  tft.setTextPadding(tft.textWidth("-88oC"));
  tft.drawString(String(s.temp, 0) + (units == "metric" ? "oC" : "oF"), x + 25, y + 17);

  String weatherIcon = getMeteoconIcon(s.id, false);
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
const char* getMeteoconIcon(uint16_t id, bool today) {
  // if ( today && id/100 == 8 && (forecast->dt[0] < forecast->sunrise || forecast->dt[0] > forecast->sunset)) id += 1000;
  if (today && id / 100 == 8 && cachedSunrise > 0 && (now() < cachedSunrise || now() > cachedSunset)) id += 1000;
  // see issue https://github.com/Bodmer/OpenWeather/issues/26
  if (id / 100 == 2) return "thunderstorm";
  if (id / 100 == 3) return "drizzle";
  if (id / 100 == 4) return "unknown";
  if (id == 500) return "lightRain";
  else if (id == 511) return "sleet";
  else if (id / 100 == 5) return "rain";
  if (id >= 611 && id <= 616) return "sleet";
  else if (id / 100 == 6) return "snow";
  if (id / 100 == 7) return "fog";
  if (id == 800) return "clear-day";
  if (id == 801) return "partly-cloudy-day";
  if (id == 802) return "cloudy";
  if (id == 803) return "cloudy";
  if (id == 804) return "cloudy";
  if (id == 1800) return "clear-night";
  if (id == 1801) return "partly-cloudy-night";
  if (id == 1802) return "cloudy";
  if (id == 1803) return "cloudy";
  if (id == 1804) return "cloudy";

  return "unknown";
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
**                          Get 3 hourly index at start of next day
***************************************************************************************/
int getNextSlotIndex(void) {
  time_t t = now();
  for (int i = 0; i < MAX_DAYS * 8; i++) {
    if (forecast->dt[i] > t) return i;
  }
  return 0;
}

/***************************************************************************************
**                          Backlight PWM helper
***************************************************************************************/
void setBacklight(uint8_t level) {
  ledcWrite(0, level);  // channel 0 (attached to TFT_BL in setup)
}

/***************************************************************************************
**                          Cache forecast data before forecast pointer is freed
***************************************************************************************/
void cacheForecastData(void) {
  // Slot cache: next 4 three-hour slots
  int start = getNextSlotIndex();
  for (int i = 0; i < 4; i++) {
    int idx = start + i;
    if (idx < MAX_DAYS * 8) {
      slotCache[i].dt   = forecast->dt[idx];
      slotCache[i].temp = forecast->temp[idx];
      slotCache[i].id   = forecast->id[idx];
      slotCache[i].pop  = forecast->pop[idx];
    } else {
      slotCache[i].dt = 0;
    }
  }

  // Day cache: aggregate by calendar day, skip today, take next 4 days
  for (int d = 0; d < 4; d++) {
    dayCache[d].dow = 0;
    dayCache[d].high = -1000.0f;
    dayCache[d].low  =  1000.0f;
    dayCache[d].id   = 0;
    dayCache[d].pop  = 0.0f;
  }
  time_t nowLocal = TIMEZONE.toLocal(now(), &tz1_Code);
  int todayDay = day(nowLocal);
  int filled = 0;
  int lastDayKey = todayDay;
  int dayBestSlotIdx = -1;
  uint8_t dayBestHourDist = 24;
  for (int i = 0; i < MAX_DAYS * 8 && filled < 4; i++) {
    time_t local = TIMEZONE.toLocal(forecast->dt[i], &tz1_Code);
    int dKey = day(local);
    if (dKey == todayDay) continue;
    if (filled == 0 || dKey != lastDayKey) {
      // commit previous day's representative id from best slot
      if (filled > 0 && dayBestSlotIdx >= 0) {
        dayCache[filled - 1].id = forecast->id[dayBestSlotIdx];
      }
      if (filled >= 4) break;
      lastDayKey = dKey;
      dayCache[filled].dow = weekday(local);  // 1..7
      dayBestSlotIdx = i;
      dayBestHourDist = abs((int)hour(local) - 13);
      filled++;
    } else {
      uint8_t hd = abs((int)hour(local) - 13);
      if (hd < dayBestHourDist) {
        dayBestHourDist = hd;
        dayBestSlotIdx = i;
      }
    }
    DayCache& dc = dayCache[filled - 1];
    if (forecast->temp_max[i] > dc.high) dc.high = forecast->temp_max[i];
    if (forecast->temp_min[i] < dc.low)  dc.low  = forecast->temp_min[i];
    if (forecast->pop[i]      > dc.pop)  dc.pop  = forecast->pop[i];
  }
  // commit last day's icon id
  if (filled > 0 && dayBestSlotIdx >= 0) {
    dayCache[filled - 1].id = forecast->id[dayBestSlotIdx];
  }

  // Singleton fields
  cachedSunrise    = forecast->sunrise;
  cachedSunset     = forecast->sunset;
  cachedHumidity   = forecast->humidity[0];
  cachedClouds     = forecast->clouds_all[0];
  cachedWindGust   = forecast->wind_gust[0];
  cachedVisibility = forecast->visibility[0];

  // Magnus-Tetens dew-point approximation (close enough for display)
  float T = forecast->temp[0];
  float RH = forecast->humidity[0];
  // Convert if imperial: API returns °F under "imperial"; display in same unit
  cachedDewPoint = T - ((100.0f - RH) / 5.0f);

  // Moon
  time_t local0 = TIMEZONE.toLocal(forecast->dt[0], &tz1_Code);
  int ip;
  cachedMoonIcon     = moon_phase(year(local0), month(local0), day(local0), hour(local0), &ip);
  cachedMoonPhaseIdx = ip;

  cacheValid = true;
}

/***************************************************************************************
**                          Tiny JSON value extractor (no external dep)
***************************************************************************************/
// Return integer value following the first occurrence of `keyPattern` in `body`.
// keyPattern should include the quotes, e.g. "\"aqi\":"
static int extractIntAfter(const String& body, const char* keyPattern, int dflt) {
  int p = body.indexOf(keyPattern);
  if (p < 0) return dflt;
  p += strlen(keyPattern);
  while (p < (int)body.length() && (body[p] == ' ' || body[p] == ':')) p++;
  int sign = 1;
  if (p < (int)body.length() && body[p] == '-') { sign = -1; p++; }
  int val = 0;
  bool any = false;
  while (p < (int)body.length() && body[p] >= '0' && body[p] <= '9') {
    val = val * 10 + (body[p] - '0');
    p++;
    any = true;
  }
  return any ? sign * val : dflt;
}

static float extractFloatAfter(const String& body, const char* keyPattern, float dflt) {
  int p = body.indexOf(keyPattern);
  if (p < 0) return dflt;
  p += strlen(keyPattern);
  while (p < (int)body.length() && (body[p] == ' ' || body[p] == ':' || body[p] == '[')) p++;
  int end = p;
  while (end < (int)body.length() && (isdigit(body[end]) || body[end] == '-' || body[end] == '.' || body[end] == 'e' || body[end] == 'E' || body[end] == '+')) end++;
  if (end == p) return dflt;
  return body.substring(p, end).toFloat();
}

static String extractStringAfter(const String& body, const char* keyPattern) {
  int p = body.indexOf(keyPattern);
  if (p < 0) return "";
  p += strlen(keyPattern);
  while (p < (int)body.length() && (body[p] == ' ' || body[p] == ':')) p++;
  if (p >= (int)body.length() || body[p] != '"') return "";
  p++;
  int end = body.indexOf('"', p);
  if (end < 0) return "";
  return body.substring(p, end);
}

/***************************************************************************************
**                          Fetch OWM Air Pollution (free, same key)
***************************************************************************************/
void fetchAirQuality(void) {
  WiFiClient client;
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/air_pollution?lat=" + latitude +
               "&lon=" + longitude + "&appid=" + api_key;
  http.setTimeout(5000);
  if (!http.begin(client, url)) { Serial.println("AQ: begin failed"); return; }
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    int aqi = extractIntAfter(body, "\"aqi\":", 0);
    if (aqi >= 1 && aqi <= 5) airQualityIndex = (uint8_t)aqi;
    Serial.print("AQI: "); Serial.println(airQualityIndex);
  } else {
    Serial.print("AQ HTTP: "); Serial.println(code);
  }
  http.end();
}

/***************************************************************************************
**                          Fetch UV index from Open-Meteo (free, no key)
***************************************************************************************/
void fetchUVIndex(void) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + latitude +
               "&longitude=" + longitude + "&hourly=uv_index&forecast_days=1&timezone=auto";
  http.setTimeout(5000);
  if (!http.begin(client, url)) { Serial.println("UV: begin failed"); return; }
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    // Take the value at the current hour index from the hourly array
    int arrStart = body.indexOf("\"uv_index\":[");
    if (arrStart >= 0) {
      arrStart += strlen("\"uv_index\":[");
      // Get current local hour (Open-Meteo returns 24 hourly values starting at 00:00 local)
      time_t local_t = TIMEZONE.toLocal(now(), &tz1_Code);
      int h = hour(local_t);
      int p = arrStart;
      for (int i = 0; i < h; i++) {
        int comma = body.indexOf(',', p);
        if (comma < 0) { p = -1; break; }
        p = comma + 1;
      }
      if (p > 0 && p < (int)body.length()) {
        int end = p;
        while (end < (int)body.length() && (isdigit(body[end]) || body[end] == '.' || body[end] == '-')) end++;
        uvIndex = body.substring(p, end).toFloat();
      }
    }
    Serial.print("UV: "); Serial.println(uvIndex);
  } else {
    Serial.print("UV HTTP: "); Serial.println(code);
  }
  http.end();
}

/***************************************************************************************
**                          Fetch NWS active alerts (US only)
***************************************************************************************/
void fetchNWSAlerts(void) {
  nwsAlert[0] = '\0';
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.weather.gov/alerts/active?point=" + latitude + "," + longitude;
  http.setTimeout(5000);
  if (!http.begin(client, url)) { Serial.println("NWS: begin failed"); return; }
  http.addHeader("User-Agent", "ESP32WeatherDisplay/1.0");
  http.addHeader("Accept", "application/geo+json");
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    String evt = extractStringAfter(body, "\"event\":");
    if (evt.length() > 0) {
      strncpy(nwsAlert, evt.c_str(), sizeof(nwsAlert) - 1);
      nwsAlert[sizeof(nwsAlert) - 1] = '\0';
    }
    Serial.print("NWS: "); Serial.println(nwsAlert);
  } else {
    Serial.print("NWS HTTP: "); Serial.println(code);
  }
  http.end();
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
    case 1:  // 4-day forecast + gust/visibility/dew
      drawDailyForecast();
      drawSeparator(240);
      drawHumanComfortDetails();
      break;
  }
}

/***************************************************************************************
**                          Page 2 — Human comfort details (gust / visibility / dew)
***************************************************************************************/
void drawHumanComfortDetails(void) {
  // Three-column row at Y=250
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(0);
  tft.drawString("Gust",   45, 250);
  tft.drawString("Visibility", 120, 250);
  tft.drawString("Dew",   195, 250);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String gust = String(cachedWindGust, 0) + (units == "metric" ? " m/s" : " mph");
  tft.drawString(gust, 45, 268);

  // visibility — API returns metres; show km if >= 1000
  String vis;
  if (cachedVisibility >= 1000.0f) vis = String(cachedVisibility / 1000.0f, 1) + " km";
  else                              vis = String(cachedVisibility, 0) + " m";
  tft.drawString(vis, 120, 268);

  String dew = String(cachedDewPoint, 0) + (units == "metric" ? "oC" : "oF");
  tft.drawString(dew, 195, 268);

  // Alert banner — red if present, else blank
  tft.setTextDatum(TC_DATUM);
  if (nwsAlert[0] != '\0') {
    tft.fillRect(0, 295, 240, 20, TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextPadding(240);
    tft.drawString(String("! ") + nwsAlert, 120, 297);
    tft.setTextPadding(0);
  }
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

    String icon = getMeteoconIcon(d.id, false);
    ui.drawBmp("/icon50/" + icon + ".bmp", x, y + 18);
  }
  tft.setTextPadding(0);
}

/***************************************************************************************
**                          Print the weather info to the Serial Monitor
***************************************************************************************/
void printWeather(void) {
#ifdef SERIAL_MESSAGES
  Serial.println("Weather from OpenWeather\n");

  Serial.print("city_name           : ");
  Serial.println(forecast->city_name);
  Serial.print("sunrise             : ");
  Serial.println(strTime(forecast->sunrise));
  Serial.print("sunset              : ");
  Serial.println(strTime(forecast->sunset));
  Serial.print("Latitude            : ");
  Serial.println(ow.lat);
  Serial.print("Longitude           : ");
  Serial.println(ow.lon);
  // We can use the timezone to set the offset eventually...
  Serial.print("Timezone            : ");
  Serial.println(forecast->timezone);
  Serial.println();

  if (forecast) {
    Serial.println("###############  Forecast weather  ###############\n");
    for (int i = 0; i < (MAX_DAYS * 8); i++) {
      Serial.print("3 hourly forecast   ");
      if (i < 10) Serial.print(" ");
      Serial.print(i);
      Serial.println();
      Serial.print("dt (time)        : ");
      Serial.println(strTime(forecast->dt[i]));

      Serial.print("temp             : ");
      Serial.println(forecast->temp[i]);
      Serial.print("temp.min         : ");
      Serial.println(forecast->temp_min[i]);
      Serial.print("temp.max         : ");
      Serial.println(forecast->temp_max[i]);

      Serial.print("pressure         : ");
      Serial.println(forecast->pressure[i]);
      Serial.print("sea_level        : ");
      Serial.println(forecast->sea_level[i]);
      Serial.print("grnd_level       : ");
      Serial.println(forecast->grnd_level[i]);
      Serial.print("humidity         : ");
      Serial.println(forecast->humidity[i]);

      Serial.print("clouds           : ");
      Serial.println(forecast->clouds_all[i]);
      Serial.print("wind_speed       : ");
      Serial.println(forecast->wind_speed[i]);
      Serial.print("wind_deg         : ");
      Serial.println(forecast->wind_deg[i]);
      Serial.print("wind_gust        : ");
      Serial.println(forecast->wind_gust[i]);

      Serial.print("visibility       : ");
      Serial.println(forecast->visibility[i]);
      Serial.print("pop              : ");
      Serial.println(forecast->pop[i]);
      Serial.println();

      Serial.print("dt_txt           : ");
      Serial.println(forecast->dt_txt[i]);
      Serial.print("id               : ");
      Serial.println(forecast->id[i]);
      Serial.print("main             : ");
      Serial.println(forecast->main[i]);
      Serial.print("description      : ");
      Serial.println(forecast->description[i]);
      Serial.print("icon             : ");
      Serial.println(forecast->icon[i]);

      Serial.println();
    }
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
