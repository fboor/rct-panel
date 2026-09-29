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
#include "Diag.h"
#include "display/Display.h"
#include "display/Touch.h"
#include "gui/GuiApp.h"
#include "rct/RctClient.h"
#include "storage/sdlog.h"

#define RCT_POLL_MS 10000
#define SD_LOG_INTERVAL_MS 300000 // 5 min, aligned to the history sampler

// LVGL's own diagnostics were going nowhere: no print callback was registered,
// so every LV_LOG_ERROR and, more importantly, every failed assertion died
// silently. With LV_ASSERT_HANDLER set to `while(1);` a failed allocation
// therefore froze the panel with no output at all, which is exactly the class
// of bug that is impossible to diagnose from the outside. Forward everything
// LVGL has to say to the serial log.
static void lvLogPrint(lv_log_level_t level, const char *msg) {
  Serial.printf("[lv%d] %s", (int)level, msg);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\nRCT Power Panel boot"));
  // Started first, so a hang during the rest of the boot is reported too.
  diagStart();
  lv_log_register_print_cb(lvLogPrint);

  if (!displayInit()) {
    Serial.println(F("FATAL: display init failed, halting"));
    diagPhase("lcd.init");
    while (1) {
      delay(10);
    }
  }
  diagPhase("gui.init");
  guiSetup();
  guiSetSplashText("Starting ...");
  diagMem("gui.setup");

  // Non-blocking: background connect attempt to the saved network, or
  // provisioning AP if none is reachable (see Configuration.cpp).
  diagPhase("net.setup");
  networkSetup();

  diagPhase("gui.start");
  guiSetSplashText("Connecting to RCT ...");
  guiStartApp();
  diagMem("gui.start");

  diagPhase("sd.init");
  sdInit(); // SD history: first mount attempt shortly after boot
}

static bool networkReady = false; // usable Wi-Fi link established at least once
static bool timeStarted = false;  // SNTP kicked off once the link is up

// LVGL, the RCT poll and the (now task-based) SD logging share this task, so a
// blocking call anywhere here is a frozen panel. This logs every loop iteration
// that runs long enough to be felt, which is how the SD stall of 1457 ms and the
// blocking config portal were both found. Keep it: it costs one comparison.
#define STALL_REPORT_MS 300

// LVGL's clock has exactly one source. The yield hook below advances the same
// timestamp that loop() does; a second, independent "last" would let the two
// call sites disagree about how much time passed, and LVGL timers then run
// fast or stall depending on which one was called last.
static uint32_t s_lvLastTick = 0;

static void lvAdvance(uint32_t now) {
  const uint32_t d = now - s_lvLastTick;
  if (d > 0) {
    lv_tick_inc(d);
    s_lvLastTick = now;
  }
}

void loop() {
  static uint32_t loopStart = 0;
  uint32_t now = millis();
  {
    const uint32_t prevLoop = now - loopStart;
    if (prevLoop >= STALL_REPORT_MS) {
      Serial.printf("[stall] Iteration ab %lu ms, %lu ms lang\n",
                    (unsigned long)(now - prevLoop), (unsigned long)prevLoop);
    }
    loopStart = now;
  }
  lvAdvance(now); // monotonic-ish; provisioning is absorbed

  // Phase markers from here on. They cost one pointer comparison per call and
  // are what makes a hang reportable at all: without them the heartbeat can say
  // "something is stuck for 12 s" but not what. See Diag.cpp.
  diagPhase("lvgl");
  displayLooper(); // lv_timer_handler() -> flush -> esp_lcd

  // rctParse() has to wait for the device's answers, up to 2 s for an OID that
  // does not respond. Same task as LVGL, so it hands rendering back to us
  // through this hook - otherwise the whole panel froze for two seconds on
  // every such poll.
  static bool hookInstalled = false;
  if (!hookInstalled) {
    hookInstalled = true;
    rctSetYieldHook([]() {
      lvAdvance(millis());
      displayLooper();
    });
  }

  // Pump the Wi-Fi state machine on every loop. This runs the captive portal's
  // web/DNS servers via WiFiManager::process() whenever the panel is in the
  // provisioning state - including manual re-entry from the Service page
  // (restartProvisioning()), where networkReady is already true from an earlier
  // connect. Gating this on !networkReady starved the portal's HTTP server:
  // the softAP/DHCP (driver handled) kept working, but http://192.168.4.1
  // never answered. networkUpdate() returns immediately in WIFI_READY.
  diagPhase("net.update");
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
    rctParse(); // marks its own phases: rct.connect / rct.poll
  }

  // SD history: one CSV row per 5 minutes (see docs/sd-history.md). The card
  // work itself happens in the SD worker task; these calls only format and post.
  diagPhase("sd.tick");
  sdTick();
  static uint32_t lastSd = 0;
  if (now - lastSd >= SD_LOG_INTERVAL_MS) {
    lastSd = now;
    sdLogSample(rctState);
  }

}