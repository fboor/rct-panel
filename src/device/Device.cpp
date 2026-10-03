// Storage behind the API in Device.h.
//
// One instance, no locking, no copy on read. The panel polls every 10 s from
// the task that also runs LVGL, so the values are read and written from that
// one context and a lock would only be a second way to get it wrong.
//
// SPDX-License-Identifier: MIT
#include "Device.h"

static DeviceState s_device;

const DeviceState &deviceState() { return s_device; }

DeviceState &deviceStateMutable() { return s_device; }

const DeviceSemantics &deviceSemantics() { return s_device.semantics; }

void deviceSetSemantics(const DeviceSemantics &sem) {
  s_device.semantics = sem;
}