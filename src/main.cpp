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
#include "output/Relay.h"
#include "rct/RctClient.h"
#include "storage/sdlog.h"
#include "web/WebServer.h"

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

  // Before anything else touches hardware: the switched output is driven to its
  // off level while the rest of the board is still coming up. Everything that
  // follows can only ever open it, never leave it undefined (see Relay.h).
  relayInit();

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

// Hand the display back while something else holds this task: advance LVGL's
// clock and flush. Installed as the yield hook for the inverter poll and for the
// web interface's file downloads, so both keep the panel alive without knowing
// anything about the display.
static void panelYield() {
  lvAdvance(millis());
  displayLooper();
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

  // rctParse() has to wait for the device's answers, and a file download from the
  // web interface is pumped out of its handler. Both block this task, which is
  // also LVGL's, so they hand rendering back to us through a hook - otherwise the
  // panel stands still for as long as the wait lasts.
  static bool hookInstalled = false;
  if (!hookInstalled) {
    hookInstalled = true;
    rctSetYieldHook(panelYield);
    webSetYieldHook(panelYield);
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

  // Web interface: exists only in normal operation, next to the portal - never
  // with it, since both want port 80. Started once the link is up, stopped
  // again by startProvisioningAp()/restartProvisioning() inside Configuration.
  // Pumping is like the portal's: one handleClient() plus at most one stream
  // chunk per iteration, so a download never blocks the panel.
  diagPhase("web.update");
  if (normalOperation()) {
    if (!webRunning()) {
      webStart();
    }
    webUpdate();
  }

  static uint32_t lastRct = 0;
  if (now - lastRct >= RCT_POLL_MS) {
    lastRct = now;
    rctParse(); // marks its own phases: rct.connect / rct.poll
  }

  // Switched output: evaluates its rule at 1 Hz on the values rctParse() just
  // refreshed. Costs a few comparisons; the timings it waits for (20 s on-delay,
  // 60 s minimum hold) are far longer than a poll interval.
  diagPhase("relay.update");
  if (relayUpdate()) {
    // It switched: show that now. The 1 Hz refresh would get here up to a second
    // late, and a relay that has already changed state while the display still
    // shows the old one is the worst way to watch the 5 s test.
    guiRelayStateChanged();
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