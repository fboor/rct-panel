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
static bool portalSaved = false; // set when the user submits the portal form
// After a save the provisioning AP stays up briefly (portalClosePending) so
// the browser reliably receives the "Saved!" reply before the radio switches
// over to the configured network.
static bool portalClosePending = false;
static uint32_t portalCloseDeadline = 0;
static const uint32_t PORTAL_CONFIRM_MS = 3000;
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

static void saveConfigCallback() {
  shouldSaveConfig = true; // finishWifiUp() persists host/port + captured credentials
  portalSaved = true;      // networkUpdate() hands off to the background connect
}

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
  // Modem sleep can make the ESP32-S3 softAP drop beacons/associations; keep
  // the radio fully awake while the panel is acting as the provisioning AP.
  WiFi.setSleep(false);
  wm.startConfigPortal("RCT-Panel");
  // WiFiManager disables the station interface when the portal starts while
  // not connected (_disableSTAConn), leaving the radio in AP-only mode. A
  // portal save then re-enables STA inside the library's save processing - a
  // full mode flap that has dropped the browser's connection before it could
  // finish reading the "Saved!" reply. Keep the radio in AP_STA with the
  // station interface enabled but idle: the save then leaves the radio alone
  // (no mode change, no flap) and the reply is delivered reliably. The
  // station is reconfigured for the target network during the save hand-off.
  WiFi.setAutoReconnect(false); // no stray station re-association while provisioning
  WiFi.mode(WIFI_AP_STA);
  WiFi.enableSTA(true);
  WiFi.disconnect(false, false); // drop any stale link, keep the AP up
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
  // Portals must stay cooperative with the GUI loop instead of running
  // WiFiManager's internal blocking loop (the library default): we pump the
  // captive portal web server from networkUpdate()/loop() instead.
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(0); // AP stays up until configured; we close it ourselves
  // Bound the portal's connect-on-save: with the library default (_connectTimeout
  // = 0) a save runs Arduino's 60 s waitForConnectResult() while the loop is
  // blocked inside process() - the portal and LCD freeze and the client times
  // out. 8 s covers a normal association + DHCP on a reachable network.
  wm.setConnectTimeout(8);
  // Save-only provisioning: the portal must not churn the radio by connecting
  // to the submitted network itself - that kicks the client off the AP and has
  // left the S3 softAP in a degraded (beaconing but unjoinable) state after a
  // failed save. Saving here only persists the submitted credentials into the
  // esp-wifi profile; saveConfigCallback() then hands control to our
  // non-blocking background connect in networkUpdate().
  wm.setSaveConnect(false);
  // Even without connect-on-save, one process() call after a save still polls
  // for a connect result; bound it tightly (0 would fall back to a 60 s wait).
  wm.setSaveConnectTimeout(1);
  // Explain the save hand-off on every portal page so the disappearing AP is
  // not mistaken for a failure.
  wm.setCustomHeadElement(
      "<script>document.addEventListener('DOMContentLoaded',function(){"
      "var n=document.createElement('p');"
      "n.style.cssText='background:#fff3cd;border:1px solid #ffe08a;"
      "border-radius:6px;padding:8px;margin:10px 0;font-size:13px;"
      "line-height:1.4';"
      "n.textContent='Save: the panel switches to the entered network and this "
      "RCT-Panel access point then disappears. To provision again, boot with "
      "the Wi-Fi unreachable (or erase NVS).';"
      "document.body.prepend(n);});</script>");

  if (wifi_ssid[0]) {
    // Background connect to the credentials from a previous session; the GUI
    // keeps running while this is in progress (polled from networkUpdate()).
    Serial.printf("WiFi: trying saved network '%s' ...\n", wifi_ssid);
    WiFi.setSleep(false); // keep the radio responsive for both STA and the AP
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_pass);
    phase = WIFI_CONNECTING;
  } else {
    // No credentials of our own yet (fresh board, NVS wiped, or credentials
    // never captured): serve the provisioning AP right away instead of poking
    // the chip's own NVS profile, whose contents are unreliable before the
    // Wi-Fi driver has been initialized (and have been observed as garbage).
    startProvisioningAp();
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
      // Pumps DNS/HTTP + the post-save flow. Returns true once the station is
      // up, i.e. the user configured the Wi-Fi (only reachable in connect-on-
      // save mode; we use save-only, so this stays false).
      if (wm.process()) {
        wm.stopConfigPortal();
        finishWifiUp();
        return true;
      }
      // Save-only portal: a save returns WL_IDLE from process() and fires
      // saveConfigCallback(). Take over with our own background connect so the
      // GUI keeps running, and a wrong password just re-opens the portal after
      // the connect budget expires.
      if (portalSaved) {
        portalSaved = false;
        // Capture the submitted credentials now (the Wi-Fi driver is up and
        // the library already configured the station with them), so the
        // hand-off never depends on the chip's NVS profile.
        String ssid = wm.getWiFiSSID();
        if (ssid.length() > 0) {
          strncpy(wifi_ssid, ssid.c_str(), sizeof(wifi_ssid) - 1);
          wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
          String pass = wm.getWiFiPass();
          strncpy(wifi_pass, pass.c_str(), sizeof(wifi_pass) - 1);
          wifi_pass[sizeof(wifi_pass) - 1] = '\0';
        }
        // Keep the provisioning AP up a moment longer so the browser reliably
        // receives and renders the "Saved!" reply before the radio switches
        // over to the configured network.
        portalClosePending = true;
        portalCloseDeadline = millis() + PORTAL_CONFIRM_MS;
        Serial.println(F("WiFi: portal save received, switching networks ..."));
      }
      if (portalClosePending &&
          (int32_t)(millis() - portalCloseDeadline) >= 0) {
        portalClosePending = false;
        wm.stopConfigPortal();
        Serial.println(F("WiFi: connecting to saved network ..."));
        WiFi.setAutoReconnect(true); // normal operation
        WiFi.mode(WIFI_STA);
        if (wifi_ssid[0]) {
          WiFi.begin(wifi_ssid, wifi_pass);
        } else {
          WiFi.begin(); // profile-based fallback
        }
        phase = WIFI_CONNECTING;
        connectDeadline = millis() + CONNECT_BUDGET_MS;
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