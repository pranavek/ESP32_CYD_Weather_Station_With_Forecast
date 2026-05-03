#pragma once

#include <Arduino.h>
#include <Timezone.h>

namespace Config {

// Reads NVS into an in-memory cache. Falls back to compile-time defaults
// from All_Settings.h for any key that has not yet been provisioned.
void begin();

bool isProvisioned();
void wipe();           // clears the NVS namespace and resets cache to defaults

const char* wifiSsid();
const char* wifiPassword();
const char* apiKey();
const char* latitude();
const char* longitude();
Timezone*   timezone();
uint8_t     brightness();   // 0–255

// Parses a JSON object produced by the webflasher and persists every valid
// field to NVS. Keys: ssid, pass, api_key, lat, lon, tz, brightness.
// On success: marks the device as provisioned and returns true.
// On failure: rolls back any partial writes and returns false.
bool applyJson(const char* json, size_t len);

}  // namespace Config
