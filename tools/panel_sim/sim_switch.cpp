// Switching the device from the browser, in a simulator only.
//
// On a panel the settings page saves the device type and restarts, and the restart
// is what makes the new driver take effect. A simulator has no restart - ESP.restart()
// is a no-op here on purpose, so the window survives - which means without this
// function the settings page would save the choice, show it as selected, and go on
// talking to the device it was already talking to.
//
// So the switch is done where it can be: the driver is ended and a new one begins
// with the settings that were just saved. The same deviceBegin() the panel calls at
// boot, on the same DeviceConfig, built from the same globals - so what runs after
// the switch is what would have run after a restart.
//
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>

#include "../../src/config/Configuration.h"
#include "../../src/device/Device.h"
#include "../../src/web/WebServer.h"

#include "sim_config.h"
#include "sim_restart.h"

// Defined in src/web/WebServer.cpp under PANEL_SIM; the firmware has no such
// function and neither call site of it exists there.
// The settings as they stand, so that a save can write them out. Set once at
// startup from what the command line and the saved file said; the settings page
// reads the device fields out of the globals the firmware keeps them in, so there
// is one truth and not two.
static SimConfig g_aktuell;

// What is running now, so that a save of the same settings is not mistaken for a
// change. On a panel this comes from the NVS at boot; here it starts empty and is
// filled by the first deviceBegin().
static char g_laufendTyp[12] = "";
static char g_laufendHost[41] = "";
static char g_laufendPort[6] = "";

void simSetConfig(const SimConfig &cfg) { g_aktuell = cfg; }

void webDeviceSwitch() {
  DeviceConfig cfg;
  snprintf(cfg.type, sizeof(cfg.type), "%s", device_type);
  snprintf(cfg.host, sizeof(cfg.host), "%s", device_host);
  snprintf(cfg.port, sizeof(cfg.port), "%s", device_port);
  if (cfg.port[0] == '\0') {
    snprintf(cfg.port, sizeof(cfg.port), "%s", deviceDefaultPort(cfg.type));
  }

  printf("Geraet: wechsle auf Typ %s, %s:%s\n", cfg.type,
         cfg.host[0] ? cfg.host : "-", cfg.port);

  // deviceBegin() replaces the driver. The old one is left to the allocator, which
  // is what the firmware does at boot as well - one switch per settings save is not
  // a leak, it is one object.
  // Is this a CHANGE, or a save of what is already running? The settings page sends
  // both - the type, the address and the theme on every save - and a panel only
  // restarts when something changed, because a restart costs a minute.
  // The FIRST call establishes what is running; it is not a change. Without this the
  // simulator restarts itself in a loop and never comes up: g_laufendTyp starts
  // empty, so the boot call compares "OIG" against "" and finds a difference, execv
  // starts a new process, that one starts with an empty g_laufendTyp again, and the
  // loop has no end because the thing being compared is never the same object.
  //
  // It is the same mistake as the label positions in the LCARS prototype: reading a
  // value before anything has written it, and treating "nothing yet" as "something
  // else".
  static bool g_ersterAufruf = true;
  const bool gewechselt =
      !g_ersterAufruf &&
      (strcmp(cfg.type, g_laufendTyp) != 0 || strcmp(cfg.host, g_laufendHost) != 0 ||
       strcmp(cfg.port, g_laufendPort) != 0);
  g_ersterAufruf = false;

  deviceBegin(cfg);
  snprintf(g_laufendTyp, sizeof(g_laufendTyp), "%s", cfg.type);
  snprintf(g_laufendHost, sizeof(g_laufendHost), "%s", cfg.host);
  snprintf(g_laufendPort, sizeof(g_laufendPort), "%s", cfg.port);
  printf("Geraet: jetzt %s%s\n", deviceTypeName(),
         gewechselt ? " (gewechselt)" : " (unveraendert)");

  // Remember it, so the next start comes up the same way. The developer's own device
  // and board size are not retyped every morning - that is the whole reason this
  // exists, and the reason the file is in .gitignore.
  g_aktuell.deviceType = cfg.type;
  g_aktuell.deviceHost = cfg.host;
  g_aktuell.devicePort = cfg.port;
  simConfigSave(g_aktuell);

  // A change of device restarts the process, the way a panel restarts the chip. The
  // theme does NOT - that is applied immediately on both, which is why the two are
  // separated here rather than in the settings handler: one save can do both, and
  // only one of them costs a restart.
  //
  // simRestart() does not return when it succeeds.
  if (gewechselt) {
    simRestart(true);
  }
}
// The plain restart: the settings page's "neustart", and nothing else. Keeps the
// command line, because the user typed it a moment ago and a restart they asked for
// is a restart of what they asked for.
void webRestart() { simRestart(false); }
