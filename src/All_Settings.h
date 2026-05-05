// User-facing settings. Edit this file before flashing for any deployment.

#define WIFI_SSID      "-your-router-name-"
#define WIFI_PASSWORD  "-your-router-password-"

// Pick a Timezone object defined in NTP_Time.h (e.g. usCT, usET, euCET, UK, ausET).
#define TIMEZONE usCT

// Update every 30 minutes — Open-Meteo free tier allows 10000 req/day for non-commercial use.
const int UPDATE_INTERVAL_SECS = 30UL * 60UL;

// Screen off hours — backlight cuts at NIGHT_OFF_HOUR:59, comes back at NIGHT_ON_HOUR:00
#define NIGHT_OFF_HOUR  23
#define NIGHT_ON_HOUR    6

// Fixed screen brightness, 0–255.
#define SCREEN_BRIGHTNESS 150

// Number of carousel pages for the bottom two sections
#define PAGE_COUNT 2

// "metric" or "imperial"
const String units = "metric";

// New York, Empire State Building — replace with your location.
const String latitude  = "40.749778527083656";  // -90 to 90 (negative south)
const String longitude = "-73.98629815117553";  // -180 to 180 (negative west)

// Short day-of-week abbreviations (change for your language).
const String shortDOW[8] = { "???", "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

// Localisation labels
const char sunStr[] = "Sun";
const char cloudStr[] = "Cloud";
const char humidityStr[] = "Humidity";
const String moonPhase[8] = { "New", "Waxing", "1st qtr", "Waxing", "Full", "Waning", "Last qtr", "Waning" };
