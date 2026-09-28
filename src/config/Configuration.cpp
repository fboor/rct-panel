// Configuration: settings storage + WiFiManager captive-portal provisioning.
//
// The provisioning state machine never blocks the GUI:
//   - networkSetup()     : load settings; with saved credentials start a
//                          background connect attempt, otherwise start the
//                          provisioning AP right away.
//   - networkUpdate()    : pumped every loop(); runs the captive portal web
//                          server / watches the background connect, returns
//                          true as soon as a usable Wi-Fi link is up.
//   - wifiReconnectLoop(): reconnects the saved network after a stable link
//                          was lost post-provisioning.
// If the saved network is unreachable, the "RCT-Panel" access point comes up
// after a short budget; it stays up (no portal timeout) until the user
// configures Wi-Fi and the RCT host/port, or the board is restarted.
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
static WiFiManager wm; // must outlive setup(): non-blocking portal is pumped from loop()
static bool shouldSaveConfig = false;
static bool ready = false; // provisioning finished, link usable

// Persisted Wi-Fi credentials, captured on every successful connect so boot
// reconnects and the reconnect loop never depend on WiFiManager's NVS state
// (its esp_wifi_get_config()-based getters return garbage before the Wi-Fi
// driver is initialized).
static char wifi_ssid[33] = "";
static char wifi_pass[65] = "";

enum WifiPhase { WIFI_CONNECTING, WIFI_PORTAL, WIFI_READY };
static WifiPhase phase = WIFI_CONNECTING;

static const uint32_t CONNECT_BUDGET_MS = 15000; // give up saved network after this
static uint32_t connectDeadline = 0;

static void saveConfigCallback() { shouldSaveConfig = true; }

static WiFiManagerParameter section_rct("<hr><h3>RCT Power options</h3>");
static WiFiManagerParameter p_rct_host("rct_host",
                                       "<b>RCT host</b><br>IP address or hostname "
                                       "of the RCT Power device",
                                       rct_host, 41);
static WiFiManagerParameter p_rct_port("rct_port",
                                       "<b>RCT port</b><br><code>8899</code> default",
                                       rct_port, 6);

void readConfig() {
  prefs.begin("config", false);
  strncpy(rct_host, prefs.getString("rct_host", rct_host).c_str(),
          sizeof(rct_host) - 1);
  rct_host[sizeof(rct_host) - 1] = '\0';
  strncpy(rct_port, prefs.getString("rct_port", rct_port).c_str(),
          sizeof(rct_port) - 1);
  rct_port[sizeof(rct_port) - 1] = '\0';
  strncpy(wifi_ssid, prefs.getString("wifi_ssid", "").c_str(),
          sizeof(wifi_ssid) - 1);
  wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
  strncpy(wifi_pass, prefs.getString("wifi_pass", "").c_str(),
          sizeof(wifi_pass) - 1);
  wifi_pass[sizeof(wifi_pass) - 1] = '\0';
  prefs.end();
}

void saveConfig() {
  prefs.begin("config", false);
  prefs.putString("rct_host", rct_host);
  prefs.putString("rct_port", rct_port);
  prefs.putString("wifi_ssid", wifi_ssid);
  prefs.putString("wifi_pass", wifi_pass);
  prefs.end();
}

// Serve the provisioning access point + captive portal (non-blocking; the
// WiFiManager portal web server is kept running by networkUpdate()).
static void startProvisioningAp() {
  Serial.println(F("WiFi: starting 'RCT-Panel' provisioning access point ..."));
  wm.startConfigPortal("RCT-Panel");
  phase = WIFI_PORTAL;
}

// Called once Wi-Fi is usable (background connect or portal config done).
static void finishWifiUp() {
  // The portal form edits the parameter buffers in place; on a background
  // reconnect they still hold the persisted values, so only persist when the
  // user actually changed something in the portal.
  strcpy(rct_host, p_rct_host.getValue());
  strcpy(rct_port, p_rct_port.getValue());

  // Capture the connected network's credentials (Wi-Fi driver is up now, so
  // the getters are valid) so future boots and reconnects are self-contained.
  String ssid = wm.getWiFiSSID();
  if (ssid.length() > 0) {
    strncpy(wifi_ssid, ssid.c_str(), sizeof(wifi_ssid) - 1);
    wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
    String pass = wm.getWiFiPass();
    strncpy(wifi_pass, pass.c_str(), sizeof(wifi_pass) - 1);
    wifi_pass[sizeof(wifi_pass) - 1] = '\0';
  }

  if (shouldSaveConfig) {
    saveConfig();
    shouldSaveConfig = false;
  } else if (wifi_ssid[0]) {
    // Newly captured credentials should survive a reboot even when the portal
    // was not involved (can happen on the very first connect).
    prefs.begin("config", false);
    prefs.putString("wifi_ssid", wifi_ssid);
    prefs.putString("wifi_pass", wifi_pass);
    prefs.end();
  }
  ready = true;
  phase = WIFI_READY;
  Serial.printf("WiFi: connected, RSSI %d dBm, RCT host '%s' port '%s'\n",
                WiFi.RSSI(), rct_host, rct_port);
  Serial.printf("WiFi: ip %s, gw %s, dns %s\n",
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(),
                WiFi.dnsIP().toString().c_str());
}

void networkSetup() {
  readConfig();

  wm.setDebugOutput(false);
  wm.setTitle("RCT Panel");
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.addParameter(&section_rct);
  wm.addParameter(&p_rct_host);
  wm.addParameter(&p_rct_port);
  wm.setConfigPortalTimeout(0); // AP stays up until configured; we close it ourselves

  String savedSsid = String(wifi_ssid);
  if (savedSsid.length() > 0) {
    // Background connect to the credentials from a previous session.
    Serial.printf("WiFi: trying saved network '%s' ...\n", savedSsid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_pass);
    phase = WIFI_CONNECTING;
  } else {
    // No stored credentials of our own: fall back to the esp_wifi NVS profile
    // (boards provisioned by older builds / the reference project still have
    // one). WiFi.begin() without arguments connects with that profile; the
    // successful network is captured into our own storage by finishWifiUp().
    Serial.println(F("WiFi: trying saved profile ..."));
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    phase = WIFI_CONNECTING;
  }
  connectDeadline = millis() + CONNECT_BUDGET_MS;
}

bool networkUpdate() {
  switch (phase) {
    case WIFI_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        finishWifiUp();
        return true;
      }
      if ((int32_t)(millis() - connectDeadline) >= 0) {
        Serial.println(F("WiFi: saved network not reachable"));
        startProvisioningAp();
      }
      return false;

    case WIFI_PORTAL:
      // The portal can drop out of its own accord (user pressed "Exit",
      // abort, ...): re-open it so the provisioning AP is always available
      // until the panel is actually configured.
      if (!wm.getConfigPortalActive()) {
        static uint32_t lastOpen = 0;
        if ((int32_t)(millis() - lastOpen) >= 2000) {
          lastOpen = millis();
          startProvisioningAp();
        }
        return false;
      }
      // Pumps DNS/HTTP + the connect-after-save flow. Returns true once the
      // station is up, i.e. the user configured the Wi-Fi.
      if (wm.process()) {
        wm.stopConfigPortal();
        finishWifiUp();
        return true;
      }
      return false;

    case WIFI_READY:
    default:
      return true;
  }
}

void wifiReconnectLoop() {
  if (!ready || WiFi.status() == WL_CONNECTED) {
    return;
  }
  static uint32_t lastAttempt = 0;
  uint32_t now = millis();
  if (now - lastAttempt > 10000) {
    lastAttempt = now;
    Serial.println(F("WiFi: link lost, reconnecting ..."));
    if (wifi_ssid[0]) {
      WiFi.begin(wifi_ssid, wifi_pass);
    } else {
      WiFi.reconnect(); // profile-based
    }
  }
}