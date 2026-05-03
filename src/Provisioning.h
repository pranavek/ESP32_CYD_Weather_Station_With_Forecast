#pragma once

#include <TFT_eSPI.h>

namespace Provisioning {

// Blocking. Shows a "waiting for setup" screen on `tft` and reads framed
// lines from Serial until a `<<PROV {...json...}>>` line is received and
// successfully applied via Config::applyJson, after which the device reboots.
//
// Call only when Config::isProvisioned() is false.
void run(TFT_eSPI& tft);

// Non-blocking poll for `<<PROV WIPE>>` from Serial during normal operation.
// Wipes NVS and restarts the device when the command is received.
void pollWipe();

}  // namespace Provisioning
