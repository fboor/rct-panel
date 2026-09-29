// rct-panel: RCT Power data wall panel on the Guition ESP32-S3 4848S040.
//
// Boot order: display -> GUI -> Wi-Fi. Wi-Fi provisioning never blocks the
// GUI: networkSetup()/networkUpdate() bring up the saved network in the
// background (or the "RCT-Panel" provisioning AP when no network is
// reachable) while the LVGL loop keeps rendering.
//
// SPDX-License-Identifier: MIT
#include <Arduino.h>

#include "config/Configuration.h"
#include "display/Display.h"
#include "display/Touch.h"
#include "gui/GuiApp.h"
#include "rct/RctClient.h"

#define RCT_POLL_MS 10000

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\nRCT Power Panel boot"));

  if (!displayInit()) {
    Serial.println(F("FATAL: display init failed, halting"));
    while (1) {
      delay(10);
    }
  }
  guiSetup();
  guiSetSplashText("Starting ...");

  // Non-blocking: background connect attempt to the saved network, or
  // provisioning AP if none is reachable (see Configuration.cpp).
  networkSetup();

  guiSetSplashText("Connecting to RCT ...");
  guiStartApp();
}

static bool networkReady = false; // usable Wi-Fi link established at least once
static bool timeStarted = false;  // SNTP kicked off once the link is up

void loop() {
  static uint32_t lastTick = 0;
  uint32_t now = millis();
  lv_tick_inc(now - lastTick); // monotonic-ish; provisioning is absorbed
  lastTick = now;

  displayLooper(); // lv_timer_handler() -> flush -> esp_lcd

  // Pump the Wi-Fi state machine on every loop. This runs the captive portal's
  // web/DNS servers via WiFiManager::process() whenever the panel is in the
  // provisioning state - including manual re-entry from the Service page
  // (restartProvisioning()), where networkReady is already true from an earlier
  // connect. Gating this on !networkReady starved the portal's HTTP server:
  // the softAP/DHCP (driver handled) kept working, but http://192.168.4.1
  // never answered. networkUpdate() returns immediately in WIFI_READY.
  networkReady = networkUpdate();
  if (networkReady && !timeStarted) {
    // Wall clock for the "next calibration" countdown on the Gerät page;
    // becomes valid a few seconds after the link is up (non-blocking).
    timeStarted = true;
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org",
                 "de.pool.ntp.org");
  }
  if (networkReady) {
    wifiReconnectLoop();
  }

  static uint32_t lastRct = 0;
  if (now - lastRct >= RCT_POLL_MS) {
    lastRct = now;
    rctParse();
  }
}