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

// Provisioning AP: WiFiManager starts it open ("RCT-Panel", no password).
static const char kApSsid[] = "RCT-Panel";

enum WifiPhase { WIFI_CONNECTING, WIFI_PORTAL, WIFI_READY };
static WifiPhase phase = WIFI_CONNECTING;

// The saved-network attempt gets a full minute: with the measured RSSI of
// -74..-83 dBm, association plus DHCP regularly need longer than 15 s, and
// dropping into provisioning early only costs the user time.
static const uint32_t CONNECT_BUDGET_MS = 60000; // give up saved network after this
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
  // Keep the portal form's RCT host/port fields in sync with the values
  // actually in use. The WiFiManagerParameter defaults are captured at file
  // scope - before readConfig() and any dev override run - so without this a
  // save would re-submit the stale compile-time host and clobber NVS.
  p_rct_host.setValue(rct_host, sizeof(rct_host) - 1);
  p_rct_port.setValue(rct_port, sizeof(rct_port) - 1);
  // Modem sleep can make the ESP32-S3 softAP drop beacons/associations; keep
  // the radio fully awake while the panel is acting as the provisioning AP.
  WiFi.setSleep(false);
  // MUST be non-blocking. Otherwise startConfigPortal() parks the main loop in
  // its internal while(1), and with _configPortalTimeout == 0 (the default)
  // configPortalHasTimeout() never fires, so the only exits are a client
  // action or a WiFi status change: the loop can hang forever. That froze the
  // GUI and the touch input outright. networkUpdate() already pumps
  // wm.process() every loop, which serves the DNS/HTTP requests in that mode.
  wm.setConfigPortalBlocking(false);
  wm.startConfigPortal("RCT-Panel");
  char apIp[16];
  strncpy(apIp, WiFi.softAPIP().toString().c_str(), sizeof(apIp) - 1);
  apIp[sizeof(apIp) - 1] = '\0';
  Serial.printf("WiFi: softAP IP %s, mode %d, portal active %d\n", apIp,
                WiFi.getMode(), wm.getConfigPortalActive());
  // Do NOT touch the radio mode / station interface / AP after the portal has
  // started: WiFi.mode()/enableSTA()/enableAP() each stop and restart the
  // whole Wi-Fi stack, tearing down the softAP interface the web/DNS servers
  // were just bound to. That leaves the AP IP/PING/DHCP alive but the HTTP
  // server dead (browser: "waiting for 192.168.4.1"). Callers must present a
  // clean, station-idle radio state *before* startConfigPortal (see
  // restartProvisioning()); the library then runs the AP-only portal and
  // every server binds to a settled interface.
  WiFi.setAutoReconnect(false); // no stray station re-association while provisioning
  phase = WIFI_PORTAL;
}

// Called once Wi-Fi is usable (background connect or portal config done).
static void finishWifiUp() {
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
    // Portal save: the form edited the WiFiManager parameter buffers in
    // place, so adopt the submitted RCT settings and persist everything.
    strcpy(rct_host, p_rct_host.getValue());
    strcpy(rct_port, p_rct_port.getValue());
    saveConfig();
    shouldSaveConfig = false;
  } else {
    // Background reconnect: the parameter buffers still hold the compile-time
    // defaults; do NOT let them overwrite the values readConfig() loaded from
    // NVS. Persist newly captured credentials only.
    if (wifi_ssid[0]) {
      prefs.begin("config", false);
      prefs.putString("wifi_ssid", wifi_ssid);
      prefs.putString("wifi_pass", wifi_pass);
      prefs.end();
    }
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

  // TEMP (dev/test): point the panel at the local RCT simulator instead of
  // the stored host so live values can be verified. Remove for production; the
  // real host then comes from NVS / the provisioning portal again.
  // Real device, read-only. The panel sends exactly two kinds of frame:
  // READ requests (type 0x01, one per OID) and the 0x3c poll request, which
  // asks the device to volunteer its values. It never builds a WRITE frame
  // (type 0x02), so no setting on the device can be altered from here.
  strcpy(rct_host, "192.168.1.83");
  strcpy(rct_port, "8899");
  Serial.printf("RCT: using simulator host %s:%s (TEMP override)\n", rct_host,
                rct_port);

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
  // Pin the provisioning AP to the classic 192.168.4.1/24 (the QR overlay
  // caption promises this address; don't rely on the driver default).
  wm.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                         IPAddress(255, 255, 255, 0));
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
        String pass = wm.getWiFiPass();
        Serial.printf("WiFi: portal save: ssid='%s' (%d ch), pass %d ch\n",
                      ssid.c_str(), ssid.length(), pass.length());
        if (ssid.length() > 0) {
          strncpy(wifi_ssid, ssid.c_str(), sizeof(wifi_ssid) - 1);
          wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
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
        Serial.println(F("WiFi: connecting after save ..."));
        WiFi.setAutoReconnect(true); // normal operation
        WiFi.mode(WIFI_STA);
        // Connect with the station config the library just submitted (its
        // save flow ran WiFi.begin(ssid, pass) and esp_wifi_set_config, even
        // in save-only mode). The no-arg begin() uses exactly that config;
        // our NVS copy may still hold the previous network until
        // finishWifiUp() re-captures and persists the live credentials.
        WiFi.begin();
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

bool networkConnecting() { return phase == WIFI_CONNECTING; }

bool provisioningApActive() { return phase == WIFI_PORTAL; }

const char *provisioningApSsid() {
  // Prefer the live softAP SSID once the portal is up, so the QR reflects
  // what clients actually see; fall back to the configured name otherwise.
  if (phase == WIFI_PORTAL) {
    String ssid = WiFi.softAPSSID();
    if (ssid.length() > 0) {
      static char buf[33];
      strncpy(buf, ssid.c_str(), sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
      return buf;
    }
  }
  return kApSsid;
}

bool provisioningApOpen() {
  // The provisioning portal is started without a password (open network).
  return true;
}

void restartProvisioning() {
  if (phase == WIFI_PORTAL) {
    return; // already serving the provisioning AP
  }
  ready = false;
  Serial.println(F("WiFi: reopening 'RCT-Panel' provisioning AP (Service page)"));
  // The portal must start from the same clean radio state as a first boot
  // with no credentials (the only state known to work): WiFiManager skips its
  // own station teardown when the STA is still connected, and a portal
  // started alongside a live station link leaves the softAP and its web/DNS
  // servers unresponsive. Kill the station and the radio entirely first; the
  // library brings the radio back up in AP-only mode, and nothing touches
  // mode/STA/AP afterwards (see startProvisioningAp()).
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_OFF); // full teardown: station + radio off, like a fresh boot
  startProvisioningAp();
}