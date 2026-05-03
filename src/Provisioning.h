#pragma once

#include <TFT_eSPI.h>

namespace Provisioning {

// Blocking. Shows a "waiting for setup" screen on `tft` and reads framed
// lines from Serial until a `<<PROV {...json...}>>` line is received and
// successfully applied via Config::applyJson, after which the device reboots.
//
// Call only when Config::isProvisioned() is false.
void run(TFT_eSPI& tft);

// Non-blocking serial poll. Reads one line at a time during the main loop and
// dispatches `<<PROV PING>>`, `<<PROV WIPE>>`, and `<<PROV {…json…}>>` config
// blobs without going through Provisioning::run. Reboots on WIPE or successful
// config-apply.
void poll();

}  // namespace Provisioning
