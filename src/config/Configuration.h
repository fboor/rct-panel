// Configuration: settings storage + WiFiManager captive-portal provisioning.
//
// Wi-Fi is brought up non-blocking so the LVGL/GUI keeps running while the
// panel either (a) reconnects to the saved network in the background or
// (b) serves the "RCT-Panel" provisioning access point + captive portal
// web page at 192.168.4.1. Follows the WiFiManager NonBlocking example
// pattern (config portal non-blocking + process() pumped from loop()).
//
// Ported/adapted from the Energy2Shelly_ESP project
// (https://github.com/) - Apache License 2.0. See NOTICE.
#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <Arduino.h>

// RCT Power device settings (entered in the WiFiManager portal).
extern char rct_host[41]; // IP address or hostname of the RCT Power device
extern char rct_port[6];  // TCP port (8899 is the RCT Power standard port)

// Load persisted settings from NVS.
void readConfig();

// Persist current settings to NVS.
void saveConfig();

// One-time boot setup: loads settings and starts the Wi-Fi state machine.
// Never blocks: with saved credentials it starts a background connect
// attempt; without them (or when the saved network stays unreachable) it
// serves the "RCT-Panel" provisioning access point instead.
void networkSetup();

// Pump the Wi-Fi state machine (background connection or captive portal
// web server). Call on every loop(). Returns true once a usable Wi-Fi
// link is up and provisioning housekeeping is done; afterwards the portal
// is stopped and wifiReconnectLoop() takes over.
bool networkUpdate();

// Lightweight periodic reconnect attempts after the link dropped
// post-provisioning. No-op while provisioning (the portal owns the radio).
void wifiReconnectLoop();

#endif // CONFIGURATION_H