// The emulated device: a declaration without an implementation, on purpose.
//
// The driver lives in tools/panel_sim/, not here, and that is what keeps it off
// the panel: an ESP32 build links DeviceFactory.cpp, which registers "SIM" only
// under PANEL_SIM, and the implementation is not in that build's file list. If
// somebody enables the flag on a firmware build the link fails with one undefined
// symbol, which is the right kind of failure - louder than a driver that silently
// answers with made-up numbers on a device that is supposed to show real ones.
//
// The interface is the shipped one. A SIM driver is a DeviceDriver like the RCT and
// the OIG, so the simulator exercises the real device layer, the real Rules.h and
// the real factories rather than a path of its own.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_DEVICE_SIMDRIVER_H
#define RCT_DEVICE_SIMDRIVER_H

#include "DeviceDriver.h"

// Defined in tools/panel_sim/sim_device.cpp, which a firmware build does not have.
DeviceDriver *makeSimDriver();

// The file the driver takes its values from. Set by the simulator before
// deviceBegin(); nullptr means the built-in curve, which needs no file at all.
void simDriverSetDataFile(const char *pfad);

// The clock the driver shapes its values with. The simulator sets it before each
// poll, so the numbers are a function of the panel's clock and not of the wall
// clock - which is what makes two runs of the same build produce the same picture.
void simDriverSetNowMs(uint32_t ms);

#endif // RCT_DEVICE_SIMDRIVER_H
