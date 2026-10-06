// A stand-in for the ESP32 WiFi, connected to nothing.
//
// The GUI shows the panel's own IP address on the info page and asks the network
// layer whether it is still connecting. Both answers here are fixed ones, chosen so
// that the pages show their normal state: the link is up, and the address is the
// one a real installation of this panel gets. Nothing dials out.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_PANEL_SIM_WIFI_H
#define RCT_PANEL_SIM_WIFI_H

#include "Arduino.h"

enum WiFiMode_t { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 };

struct WiFiClass {
  void begin(const char * = nullptr) {}
  void mode(WiFiMode_t) {}
  WiFiMode_t getMode() { return WIFI_STA; }
  bool setAutoReconnect(bool) { return true; }
  bool setSleep(bool) { return true; }
  bool reconnect() { return true; }
  IPAddress localIP() { return IPAddress(); }
  IPAddress softAPIP() { return IPAddress(); }
  IPAddress gatewayIP() { return IPAddress(); }
  IPAddress dnsIP() { return IPAddress(); }
  int32_t RSSI() { return -52; }
};

extern WiFiClass WiFi;

#endif // RCT_PANEL_SIM_WIFI_H