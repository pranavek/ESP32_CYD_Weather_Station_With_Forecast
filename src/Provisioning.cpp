#include "Provisioning.h"

#include <Arduino.h>

#include "Config.h"

namespace {

constexpr const char* kFrameOpen  = "<<PROV ";
constexpr const char  kFrameClose = '>';
constexpr size_t      kMaxLine    = 768;

// Reads a line up to '\n', stripping CR. Returns false on timeout or overflow.
bool readSerialLine(String& out, uint32_t timeoutMs) {
  out = "";
  uint32_t deadline = millis() + timeoutMs;
  while (millis() < deadline) {
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\r') continue;
      if (c == '\n') return true;
      if (out.length() >= kMaxLine) {
        out = "";
        return false;
      }
      out += c;
    }
    delay(5);
  }
  return false;
}

// Extracts the JSON payload from `<<PROV {...}>>`. Returns empty on mismatch.
String extractJson(const String& line) {
  if (!line.startsWith(kFrameOpen)) return "";
  if (line.length() < strlen(kFrameOpen) + 3) return "";  // need at least "<<PROV X>>"
  if (line.charAt(line.length() - 1) != kFrameClose) return "";
  if (line.charAt(line.length() - 2) != kFrameClose) return "";
  // payload is between "<<PROV " and ">>"
  size_t start = strlen(kFrameOpen);
  size_t end   = line.length() - 2;
  return line.substring(start, end);
}

void drawWaitingScreen(TFT_eSPI& tft) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextFont(4);
  tft.drawString("Waiting for setup",      120, 120);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(2);
  tft.drawString("Open the webflasher",    120, 170);
  tft.drawString("over USB to provision",  120, 190);
}

}  // namespace

namespace Provisioning {

void run(TFT_eSPI& tft) {
  drawWaitingScreen(tft);
  Serial.println("<<PROV READY>>");

  String line;
  while (true) {
    if (!readSerialLine(line, /*timeoutMs=*/60000)) {
      // Heartbeat so the host can tell the device is alive.
      Serial.println("<<PROV READY>>");
      continue;
    }

    if (line == "<<PROV PING>>") {
      Serial.println("<<PROV PONG>>");
      continue;
    }

    String payload = extractJson(line);
    if (payload.length() == 0) {
      Serial.println("<<PROV ERR frame>>");
      continue;
    }

    if (Config::applyJson(payload.c_str(), payload.length())) {
      Serial.println("<<PROV OK>>");
      Serial.flush();
      delay(500);
      ESP.restart();
    } else {
      Serial.println("<<PROV ERR json>>");
    }
  }
}

void pollWipe() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (buf == "<<PROV WIPE>>") {
        Serial.println("<<PROV WIPED>>");
        Config::wipe();
        Serial.flush();
        delay(200);
        ESP.restart();
      }
      buf = "";
    } else {
      if (buf.length() < 64) buf += c;
      else buf = "";
    }
  }
}

}  // namespace Provisioning
