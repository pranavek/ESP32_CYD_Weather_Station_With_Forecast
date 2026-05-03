#pragma once

// Forward declarations of the Timezone objects defined in NTP_Time.h.
// NTP_Time.h itself defines globals + functions and is included only by the
// main .ino translation unit; other source files use this header instead so
// they can resolve `timezoneByName` without pulling in those definitions.

#include <Arduino.h>
#include <Timezone.h>

extern Timezone UK;
extern Timezone euCET;
extern Timezone ausET;
extern Timezone usET;
extern Timezone usCT;
extern Timezone usMT;
extern Timezone usAZ;
extern Timezone usPT;

inline Timezone* timezoneByName(const char* name) {
  if (!name || !*name)        return &usCT;
  if (!strcmp(name, "UK"))    return &UK;
  if (!strcmp(name, "euCET")) return &euCET;
  if (!strcmp(name, "ausET")) return &ausET;
  if (!strcmp(name, "usET"))  return &usET;
  if (!strcmp(name, "usCT"))  return &usCT;
  if (!strcmp(name, "usMT"))  return &usMT;
  if (!strcmp(name, "usAZ"))  return &usAZ;
  if (!strcmp(name, "usPT"))  return &usPT;
  return &usCT;
}
