// The API every component uses to get at the device's data.
//
// Three functions today, and they are the whole contract the rest of the
// firmware is allowed to know:
//
//   deviceState()          the values, read-only
//   deviceSemantics()      how this device's numbers have to be read
//   deviceStateMutable()   the same struct, for the driver's use only
//
// Drivers and transports arrive with the next steps (DeviceDriver.h,
// DeviceTransport.h); until then src/rct/ is still reached directly by its
// own entry points, which is what the following steps replace.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_DEVICE_H
#define RCT_DEVICE_H

#include "DeviceState.h"

// The published state. Written only by the driver, from its own task.
const DeviceState &deviceState();

// The published state, writable. For a driver and nothing else: the whole
// design rests on one context touching the values at a time, because LVGL
// shares the task with the poll. A driver that finds it needs to fill the
// state somewhere else first and publish at the end may do so - the function
// returns a reference, not a promise about when.
DeviceState &deviceStateMutable();

// How this device's numbers have to be read. Set once by the driver and never
// changed, so every caller may hold on to the reference.
const DeviceSemantics &deviceSemantics();

// Driver only, once, before the first poll: state the three facts about how
// this device's meters read, and every rule in Rules.h follows from them.
void deviceSetSemantics(const DeviceSemantics &sem);

#endif // RCT_DEVICE_H