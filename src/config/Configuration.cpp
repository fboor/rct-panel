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

#include "../output/Relay.h"
#include "../web/WebServer.h"
#include <Preferences.h>
#include <WiFiManager.h>

char device_type[12] = "RCT"; // the driver to use; also the log file prefix
char device_host[41] = "192.168.0.1"; // defaults match the Energy2Shelly_ESP project
char device_port[6] = "8899";

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

static WiFiManagerParameter section_rct("<hr><h3>Inverter options</h3>");
// The type is not a field yet: there is one implemented driver, and a list of
// one is noise. It becomes a list the day a second family exists, and the saved
// value is the prefix of the log files.
static WiFiManagerParameter p_device_host("device_host",
                                       "<b>Address</b><br>IP address or hostname "
                                       "of the inverter",
                                       device_host, 41);
static WiFiManagerParameter p_device_port("device_port",
                                       "<b>Port</b><br><code>8899</code> for an "
                                       "RCT Power",
                                       device_port, 6);

// The switched output, as two number fields. A <select> is not reachable here:
// WiFiManagerParameter renders an <input> from the ID it is given (see
// WiFiManager.cpp, HTTP_FORM_PARAM), and a parameter with no ID is emitted as
// raw HTML but never receives its value back on save. So the portal gets
// numbers with the list spelled out, and the places where this is actually
// pleasant to set - tapping the function on the Service page, and the form on
// the panel's own web page - get a select or a tap instead.
static WiFiManagerParameter p_relay_mode(
    "relay_mode",
    "<b>Output function</b><br>0=off (default) &middot; 1=grid draw &middot; "
    "2=PV surplus &middot; 3=fault &middot; 4=island. Easier to set on the "
    "panel (Service page) or in its web interface.",
    "0", 2, "type=\"number\" min=\"0\" max=\"4\"");
static WiFiManagerParameter p_relay_w(
    "relay_w",
    "<b>Output threshold</b><br>in watts, 0-5000; used by functions 1 and 2. "
    "Default 500.",
    "500", 5, "type=\"number\" min=\"0\" max=\"5000\" step=\"50\"");

void readConfig() {
  prefs.begin("config", false);
  // device_* are the settings of the abstraction; rct_* is what the firmware
  // wrote before there was one. They are read once more as a fallback, so an
  // update does not leave a panel without its inverter - the failure would only
  // show up as "no data" on the wall.
  strncpy(device_host, prefs.getString("device_host", "").c_str(),
          sizeof(device_host) - 1);
  device_host[sizeof(device_host) - 1] = '\0';
  if (device_host[0] == '\0') {
    strncpy(device_host, prefs.getString("rct_host", device_host).c_str(),
            sizeof(device_host) - 1);
    device_host[sizeof(device_host) - 1] = '\0';
  }
  strncpy(device_port, prefs.getString("device_port", "").c_str(),
          sizeof(device_port) - 1);
  device_port[sizeof(device_port) - 1] = '\0';
  if (device_port[0] == '\0') {
    strncpy(device_port, prefs.getString("rct_port", device_port).c_str(),
            sizeof(device_port) - 1);
    device_port[sizeof(device_port) - 1] = '\0';
  }
  strncpy(device_type, prefs.getString("device_type", device_type).c_str(),
          sizeof(device_type) - 1);
  device_type[sizeof(device_type) - 1] = '\0';
  strncpy(device_host, device_host, sizeof(device_host) - 1);
  device_host[sizeof(device_host) - 1] = '\0';
  strncpy(device_port, device_port, sizeof(device_port) - 1);
  device_port[sizeof(device_port) - 1] = '\0';
  strncpy(wifi_ssid, prefs.getString("wifi_ssid", "").c_str(),
          sizeof(wifi_ssid) - 1);
  wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
  strncpy(wifi_pass, prefs.getString("wifi_pass", "").c_str(),
          sizeof(wifi_pass) - 1);
  wifi_pass[sizeof(wifi_pass) - 1] = '\0';
  prefs.end();
}

void deviceConfig(DeviceConfig &out) {
  strncpy(out.type, device_type, sizeof(out.type) - 1);
  out.type[sizeof(out.type) - 1] = '\0';
  strncpy(out.host, device_host, sizeof(out.host) - 1);
  out.host[sizeof(out.host) - 1] = '\0';
  strncpy(out.port, device_port, sizeof(out.port) - 1);
  out.port[sizeof(out.port) - 1] = '\0';
}

void saveConfig() {
  prefs.begin("config", false);
  prefs.putString("device_type", device_type);
  prefs.putString("device_host", device_host);
  prefs.putString("device_port", device_port);
  prefs.putString("wifi_ssid", wifi_ssid);
  prefs.putString("wifi_pass", wifi_pass);
  prefs.end();
}

// Serve the provisioning access point + captive portal (non-blocking; the
// WiFiManager portal web server is kept running by networkUpdate()).
static void startProvisioningAp() {
  Serial.println(F("WiFi: starting 'RCT-Panel' provisioning access point ..."));
  // Port 80 and the radio go to the portal from here on. The normal-operation
  // web server must be gone first: WiFiManager only checks configPortalActive,
  // not a foreign server, and two WebServers on port 80 is not a state either of
  // them can serve. See startConfigPortal() in the library and src/web/.
  webStop();
  // Keep the portal form's RCT host/port fields in sync with the values
  // actually in use. The WiFiManagerParameter defaults are captured at file
  // scope - before readConfig() and any dev override run - so without this a
  // save would re-submit the stale compile-time host and clobber NVS.
  p_device_host.setValue(device_host, sizeof(device_host) - 1);
  p_device_port.setValue(device_port, sizeof(device_port) - 1);
  // Same reason for the output fields: their defaults are also compile-time
  // constants, and a portal save submits whatever the form holds.
  char relayModeStr[4];
  char relayWStr[8];
  snprintf(relayModeStr, sizeof(relayModeStr), "%d", (int)relayMode());
  snprintf(relayWStr, sizeof(relayWStr), "%d", relayThreshold());
  p_relay_mode.setValue(relayModeStr, strlen(relayModeStr));
  p_relay_w.setValue(relayWStr, strlen(relayWStr));
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
    strcpy(device_host, p_device_host.getValue());
    strcpy(device_port, p_device_port.getValue());
    saveConfig();
    // The switched output goes with it - this is the only way to reach these
    // settings when the panel is not in the home network and its own web
    // interface is therefore not reachable. Out-of-range input is ignored
    // rather than clamped, so a typo cannot silently pick a different
    // function; relaySetThreshold() clamps on its own because a number with a
    // stray character is still a number.
    const int m = atoi(p_relay_mode.getValue());
    if (m >= 0 && m < kRelayModeCount) {
      relaySetMode((RelayMode)m);
    } else {
      Serial.printf("Portal: relay_mode '%s' ignoriert\n", p_relay_mode.getValue());
    }
    relaySetThreshold(atoi(p_relay_w.getValue()));
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
  Serial.printf("WiFi: connected, RSSI %d dBm, Geraet '%s' port '%s'\n",
                WiFi.RSSI(), device_host, device_port);
  Serial.printf("WiFi: ip %s, gw %s, dns %s\n",
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(),
                WiFi.dnsIP().toString().c_str());
}

void networkSetup() {
  readConfig();

  // Dev/test only: point the panel at a local RCT simulator instead of the
  // stored host, so live values can be verified without the real device. The
  // real host comes from NVS / the provisioning portal.
  //
  // This is a build flag and not a source edit, because forgetting to remove it
  // would ship a panel that talks to an address that does not exist. The value
  // has to arrive as a string, and the inner quotes have to survive the shell
  // inside PlatformIO:
  //   PLATFORMIO_BUILD_FLAGS='-DDLV_CONF_INCLUDE_SIMPLE -I include \
  //     -DRCT_SIM_HOST=\"192.168.1.83\"' pio run -e esp32-s3 -t upload \
  //     --upload-port /dev/ttyACM0
#ifdef RCT_SIM_HOST
  strncpy(device_host, RCT_SIM_HOST, sizeof(device_host) - 1);
  device_host[sizeof(device_host) - 1] = '\0';
  strcpy(device_port, "8899");
  Serial.printf("RCT: Simulator-Host %s:%s (Build-Flag RCT_SIM_HOST)\n", device_host,
                device_port);
#endif
  // Real device, read-only. The panel sends exactly two kinds of frame:
  // READ requests (type 0x01, one per OID) and the 0x3c poll request, which
  // asks the device to volunteer its values. It never builds a WRITE frame
  // (type 0x02), so no setting on the device can be altered from here.

  wm.setDebugOutput(false);
  wm.setTitle("RCT Panel");
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.addParameter(&section_rct);
  wm.addParameter(&p_device_host);
  wm.addParameter(&p_device_port);
  wm.addParameter(&p_relay_mode);
  wm.addParameter(&p_relay_w);
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

// True when the Wi-Fi link is usable and normal operation has begun: the
// web interface starts here and stops again as soon as provisioning takes the
// radio back.
bool normalOperation() { return phase == WIFI_READY && ready; }

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
  // Release port 80 and any open download before the portal takes the radio.
  webStop();
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