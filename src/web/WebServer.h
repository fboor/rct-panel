// Web interface for normal operation (see docs/web-interface.md).
//
// A small HTTP server on port 80, running only while the panel is connected to
// the home network. It serves four things:
//
//   /          overview: live values, card status, firmware, IP address
//   /daten     the logged CSV files, downloadable, whole or as a tail
//   /bilder    the screenshots taken on the panel
//   /update    firmware update by upload (OTA)
//
// Why its own server and not WiFiManager's web-portal mode:
//
//   WiFiManager keeps its server in a private std::unique_ptr (WiFiManager.h),
//   so the root handler cannot be replaced. In web-portal mode "/" serves the
//   *WLAN form*, and submitting it calls connectWifi() - which would switch the
//   panel off the home network from a normal-operation web page. The library is
//   therefore kept for what it is good at (the provisioning AP) and the pages
//   here are built from PROGMEM strings instead.
//
// Everything is pumped from loop() like the portal is: webUpdate() serves at
// most what fits in one iteration and returns. Card access goes through the SD
// worker (sdlog.h) - the server never touches the card itself.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_WEB_WEBSERVER_H
#define RCT_WEB_WEBSERVER_H

#include <Arduino.h>

// Start the web server. Call once the Wi-Fi link is usable. A no-op while the
// provisioning AP owns the radio (never both on port 80 - see webStop).
void webStart();

// Stop the server and release the port. Called before the provisioning AP
// starts and before a reboot triggered by the firmware update.
void webStop();

// Close an open file download without touching the server. Used on the way
// into provisioning, where the portal takes over the radio but the server object
// may still exist.
void webAbortStreams();

// True while the server is running and serving.
bool webRunning();

// Pump the server: serves the requests that arrive. A file download is pumped
// inside its own handler and keeps the panel alive through the yield hook, so
// this is a plain non-blocking handleClient() - call it every loop().
void webUpdate();

// Keep the display alive while something holds the loop task, used between the
// chunks of a download. Installed once from main.cpp, the same way as
// deviceSetYieldHook; the web module itself knows nothing about the display.
void webSetYieldHook(void (*fn)());

// The 4-digit code that gates everything that changes the panel (firmware
// update, restart, provisioning). Read routes need no code. Returns the current
// code; passing nullptr just reads it. Returns nullptr if the server is not
// running (no code exists then).
const char *webCode(const char *newCode);

// Draw a fresh code and return it. Called from the panel display (tap on the
// code) - the panel is the only place the code is ever visible, so it is also
// the only place it can be replaced.
const char *webNewCode();
#ifdef PANEL_SIM
// Restart the DEVICE with the settings as they stand. A panel restarts the whole
// machine instead and never calls this; the simulator has no restart, so this is what
// makes a switch from the settings page take effect. Defined in
// tools/panel_sim/sim_switch.cpp.
void webDeviceSwitch();
#endif

#endif // RCT_WEB_WEBSERVER_H