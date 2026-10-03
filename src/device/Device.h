// The API every component uses to get at the device's data.
//
// Seven functions, and they are the whole contract the rest of the firmware is
// allowed to know:
//
//   deviceState()          the values, read-only
//   deviceSemantics()      how this device's numbers have to be read
//   deviceStateMutable()   the same struct, for the driver's use only
//   deviceBegin()          create the driver the settings ask for
//   devicePoll()           one collection run
//   deviceTypeName()       the family, e.g. "RCT"
//   deviceSetYieldHook()   installed once by main.cpp
//
// Nothing outside src/device/ and src/rct/ may include a driver header. That is
// the whole point: the GUI, the web interface, the switching output and the CSV
// logger know the plant, not the device.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_DEVICE_H
#define RCT_DEVICE_H

#include "DeviceDriver.h"
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

// Create the driver for cfg. Once, after the settings are loaded and before the
// first devicePoll(). A type that nobody implements is a settings mistake and
// says so in the log; it does not fall back to a device that is not the
// configured one.
void deviceBegin(const DeviceConfig &cfg);

// One collection run. Call it at a fixed rate (every 10 s); the driver decides
// how long it may block and has to keep the display drawing meanwhile.
void devicePoll();

// The family of the device in use, e.g. "RCT". It is the prefix of the log file
// names and appears in the settings page. "unbekannt" before deviceBegin().
const char *deviceTypeName();

// The yield hook, installed once by main.cpp before the first poll and called by
// the driver while it waits. A driver must call it; that is the whole reason
// a collection run may block at all.
void deviceSetYieldHook(void (*fn)());

// Run the installed hook, if there is one. Drivers call this, not the pointer.
void deviceYieldHook();

#endif // RCT_DEVICE_H