#include "Config.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "All_Settings.h"
#include "Timezones.h"

namespace {

constexpr const char* kNamespace = "cyd-weather";

constexpr const char* kKeyProv      = "prov";
constexpr const char* kKeySsid      = "ssid";
constexpr const char* kKeyPass      = "pass";
constexpr const char* kKeyApiKey    = "api_key";
constexpr const char* kKeyLat       = "lat";
constexpr const char* kKeyLon       = "lon";
constexpr const char* kKeyTz        = "tz";
constexpr const char* kKeyBright    = "bright";

// In-memory cache. Strings are stored as String so getters can return c_str()
// pointers that remain valid for the device lifetime.
struct Cache {
  String   ssid;
  String   pass;
  String   apiKey;
  String   lat;
  String   lon;
  String   tzName;
  Timezone* tz = nullptr;
  uint8_t  brightness = SCREEN_BRIGHTNESS;
  bool     provisioned = false;
};

Cache cache;
Preferences prefs;

void loadFromPrefs() {
  prefs.begin(kNamespace, /*readOnly=*/true);
  cache.provisioned = prefs.getBool(kKeyProv, false);
  cache.ssid       = prefs.getString(kKeySsid,    WIFI_SSID);
  cache.pass       = prefs.getString(kKeyPass,    WIFI_PASSWORD);
  cache.apiKey     = prefs.getString(kKeyApiKey,  api_key);
  cache.lat        = prefs.getString(kKeyLat,     latitude);
  cache.lon        = prefs.getString(kKeyLon,     longitude);
  cache.tzName     = prefs.getString(kKeyTz,      TIMEZONE_NAME);
  cache.brightness = prefs.getUChar(kKeyBright,   SCREEN_BRIGHTNESS);
  prefs.end();
  cache.tz = timezoneByName(cache.tzName.c_str());
}

}  // namespace

namespace Config {

void begin() {
  loadFromPrefs();
}

bool isProvisioned() {
  return cache.provisioned;
}

void wipe() {
  prefs.begin(kNamespace, /*readOnly=*/false);
  prefs.clear();
  prefs.end();
  cache = Cache{};
  cache.tz = timezoneByName(TIMEZONE_NAME);
}

const char* wifiSsid()     { return cache.ssid.c_str(); }
const char* wifiPassword() { return cache.pass.c_str(); }
const char* apiKey()       { return cache.apiKey.c_str(); }
const char* latitude()     { return cache.lat.c_str(); }
const char* longitude()    { return cache.lon.c_str(); }
Timezone*   timezone()     { return cache.tz; }
uint8_t     brightness()   { return cache.brightness; }

bool applyJson(const char* json, size_t len) {
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, json, len);
  if (err) return false;

  // Validate before persisting anything.
  const char* ssid = doc["ssid"]    | (const char*)nullptr;
  const char* pass = doc["pass"]    | (const char*)nullptr;
  const char* api  = doc["api_key"] | (const char*)nullptr;
  const char* lat  = doc["lat"]     | (const char*)nullptr;
  const char* lon  = doc["lon"]     | (const char*)nullptr;
  const char* tz   = doc["tz"]      | (const char*)nullptr;

  if (!ssid || !*ssid) return false;
  if (!pass)            return false;   // empty password allowed for open WiFi
  if (!api  || !*api)   return false;
  if (!lat  || !*lat)   return false;
  if (!lon  || !*lon)   return false;
  if (!tz   || !*tz)    return false;

  // Brightness can be missing; default to SCREEN_BRIGHTNESS.
  int bright = doc["brightness"] | (int)SCREEN_BRIGHTNESS;
  if (bright < 0 || bright > 255) return false;

  // Float-parse lat/lon as a sanity check.
  char* endLat = nullptr;
  char* endLon = nullptr;
  strtod(lat, &endLat);
  strtod(lon, &endLon);
  if (endLat == lat || endLon == lon) return false;

  // Persist atomically — Preferences has no transactions, so we write all
  // keys then the provisioned flag last so a crash mid-write leaves the
  // device in the unprovisioned state.
  prefs.begin(kNamespace, /*readOnly=*/false);
  prefs.putString(kKeySsid,   ssid);
  prefs.putString(kKeyPass,   pass);
  prefs.putString(kKeyApiKey, api);
  prefs.putString(kKeyLat,    lat);
  prefs.putString(kKeyLon,    lon);
  prefs.putString(kKeyTz,     tz);
  prefs.putUChar (kKeyBright, (uint8_t)bright);
  prefs.putBool  (kKeyProv,   true);
  prefs.end();

  loadFromPrefs();
  return true;
}

}  // namespace Config
