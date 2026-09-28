// Configuration: settings storage + WiFiManager captive-portal provisioning.
//
// Ported/adapted from the Energy2Shelly_ESP project - Apache License 2.0.
// See NOTICE for details and copyright.
//
// SPDX-License-Identifier: Apache-2.0
#include "Configuration.h"

#include <Preferences.h>
#include <WiFiManager.h>

char rct_host[41] = "192.168.0.1"; // defaults match the Energy2Shelly_ESP project
char rct_port[6] = "8899";

static Preferences prefs;
static bool shouldSaveConfig = false;

static void saveConfigCallback() { shouldSaveConfig = true; }

void readConfig() {
  prefs.begin("config", false);
  strncpy(rct_host, prefs.getString("rct_host", rct_host).c_str(), sizeof(rct_host) - 1);
  rct_host[sizeof(rct_host) - 1] = '\0';
  strncpy(rct_port, prefs.getString("rct_port", rct_port).c_str(), sizeof(rct_port) - 1);
  rct_port[sizeof(rct_port) - 1] = '\0';
  prefs.end();
}

void saveConfig() {
  prefs.begin("config", false);
  prefs.putString("rct_host", rct_host);
  prefs.putString("rct_port", rct_port);
  prefs.end();
}

void setupConfigPortal() {
  readConfig();

  WiFiManager wifiManager;
  wifiManager.setDebugOutput(false);
  wifiManager.setTitle("RCT Panel");
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  static WiFiManagerParameter section_rct("<hr><h3>RCT Power options</h3>");
  static WiFiManagerParameter p_rct_host("rct_host",
                                         "<b>RCT host</b><br>IP address or hostname "
                                         "of the RCT Power device",
                                         rct_host, 41);
  static WiFiManagerParameter p_rct_port("rct_port",
                                         "<b>RCT port</b><br><code>8899</code> default",
                                         rct_port, 6);
  wifiManager.addParameter(&section_rct);
  wifiManager.addParameter(&p_rct_host);
  wifiManager.addParameter(&p_rct_port);
  wifiManager.setConfigPortalTimeout(180);

  Serial.println(F("WiFi: connecting via WiFiManager portal ..."));
  if (!wifiManager.autoConnect("RCT-Panel")) {
    Serial.println(F("WiFi: portal timed out, restarting"));
    delay(3000);
    ESP.restart();
  }

  // Pull the values back out of the portal form (they were edited in-place).
  strcpy(rct_host, p_rct_host.getValue());
  strcpy(rct_port, p_rct_port.getValue());

  if (shouldSaveConfig) {
    saveConfig();
  }
  Serial.printf("WiFi: connected, RSSI %d dBm, RCT host '%s' port '%s'\n",
                WiFi.RSSI(), rct_host, rct_port);
}

void wifiReconnectLoop() {
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastAttempt = 0;
    uint32_t now = millis();
    if (now - lastAttempt > 10000) {
      lastAttempt = now;
      Serial.println(F("WiFi: link lost, reconnecting ..."));
      WiFi.reconnect();
    }
  }
}