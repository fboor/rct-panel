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

// Defined in src/web/WebServer.cpp under PANEL_SIM; the firmware has no such
// function and neither call site of it exists there.
// The settings as they stand, so that a save can write them out. Set once at
// startup from what the command line and the saved file said; the settings page
// reads the device fields out of the globals the firmware keeps them in, so there
// is one truth and not two.
static SimConfig g_aktuell;

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
  deviceBegin(cfg);
  printf("Geraet: jetzt %s\n", deviceTypeName());

  // Remember it, so the next start comes up the same way. The developer's own device
  // and board size are not retyped every morning - that is the whole reason this
  // exists, and the reason the file is in .gitignore.
  g_aktuell.deviceType = cfg.type;
  g_aktuell.deviceHost = cfg.host;
  g_aktuell.devicePort = cfg.port;
  simConfigSave(g_aktuell);
}