// rct-panel: RCT Power data wall panel on the Guition ESP32-S3 4848S040.
//
// SPDX-License-Identifier: MIT
#include <Arduino.h>

#include "config/Configuration.h"
#include "display/Display.h"
#include "display/Touch.h"
#include "gui/GuiApp.h"
#include "rct/RctClient.h"

#define RCT_POLL_MS 5000

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\nRCT Power Panel boot"));

  if (!displayInit()) {
    Serial.println(F("FATAL: display init failed"));
  }
  guiSetup();
  guiSetSplashText("Starting ...");

  // WiFi + config portal (blocking; shows the RCT-Panel AP on first boot).
  guiSetSplashText("WiFi setup ...");
  setupConfigPortal();

  guiSetSplashText("Connecting to RCT ...");
  guiStartApp();
}

void loop() {
  static uint32_t lastTick = 0;
  uint32_t now = millis();
  lv_tick_inc(now - lastTick); // monotonic-ish; portal delay is absorbed
  lastTick = now;

  displayLooper(); // lv_timer_handler() -> flush -> esp_lcd

  wifiReconnectLoop();

  static uint32_t lastRct = 0;
  if (now - lastRct >= RCT_POLL_MS) {
    lastRct = now;
    rctParse();
  }
}