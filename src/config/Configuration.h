// Configuration: settings storage + WiFiManager captive-portal provisioning.
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

// Bring up Wi-Fi. On first boot (no saved credentials) this starts the
// "RCT-Panel" access point + captive portal web page at 192.168.4.1 where the
// RTC host/port are entered. Blocks until connected or the portal timed out.
void setupConfigPortal();

// Lightweight periodic reconnect attempt when the link dropped.
void wifiReconnectLoop();

#endif // CONFIGURATION_H