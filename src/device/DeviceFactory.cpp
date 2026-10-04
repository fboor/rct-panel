// Which driver serves which family.
//
// One place that knows all the families, kept out of the drivers themselves on
// purpose: a driver that knew about the others would depend on them, and adding
// a third family would mean editing the first two. It is the same shape as
// makeDriver() in every project that has more than one, and the same one line
// per family.
//
// A type nobody implements is nullptr, and Device.cpp says so in the log rather
// than falling back: a panel showing another family's data is worse than a panel
// showing none.
//
// SPDX-License-Identifier: MIT
#include "DeviceDriver.h"

#include <string.h>

#include "../oig/OigDriver.h"
#include "RctDriver.h"

DeviceDriver *makeDriver(const DeviceConfig &cfg) {
  if (strcmp(cfg.type, "RCT") == 0) {
    return new RctDriver();
  }
  if (strcmp(cfg.type, "OIG") == 0) {
    return new OigDriver();
  }
  return nullptr;
}

// The port each family answers on by default. The settings page offers it when
// the type changes, because the two are not the same number - a RCT Power speaks
// its own protocol on 8899, and a stick answers HTTP on 80 - and a number that is
// wrong is a panel that never connects.
//
// Next to makeDriver() and not inside the drivers: a driver that carried its own
// default would have to be asked before it exists, and the list of families is
// already written down once, here.
const char *deviceDefaultPort(const char *type) {
  if (strcmp(type, "RCT") == 0) {
    return "8899";
  }
  if (strcmp(type, "OIG") == 0) {
    return "80";
  }
  return "";
}
