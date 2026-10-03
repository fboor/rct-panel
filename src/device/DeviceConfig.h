// Where the device is and what family it belongs to.
//
// Its own header so that the settings layer can fill it without pulling in the
// driver interface: what the settings have to know is the shape of a device's
// address, not what a driver does with it.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_DEVICE_CONFIG_H
#define RCT_DEVICE_CONFIG_H

// Where a device is and what it is called. Filled from the settings by
// Configuration; type selects the driver, host/port are what that driver needs.
struct DeviceConfig {
  char type[12] = "RCT"; // the family, e.g. "RCT" - also the log file prefix
  char host[41] = "";    // address of the device
  char port[6] = "";     // port or bus
};

#endif // RCT_DEVICE_CONFIG_H