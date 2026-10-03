// Storage behind the API in Device.h, plus the one entry point that drives a
// device.
//
// One state instance, no locking, no copy on read. The panel polls every 10 s
// from the task that also runs LVGL, so the values are read and written from
// that one context and a lock would only be a second way to get it wrong.
//
// SPDX-License-Identifier: MIT
#include "Device.h"

#include "DeviceDriver.h"
#include "TcpTransport.h"

static DeviceState s_device;

// The panel's link. A driver never opens a socket itself, so this is where the
// transport is chosen - one line, when a second transport arrives.
static TcpTransport s_tcpLink;

// The driver, created once by deviceBegin(). nullptr before that and after a
// type nobody implements - both are states the settings can be in, and both
// mean "no device", which is what the status line then says.
static DeviceDriver *s_driver = nullptr;

const DeviceState &deviceState() { return s_device; }

DeviceState &deviceStateMutable() { return s_device; }

const DeviceSemantics &deviceSemantics() { return s_device.semantics; }

void deviceSetSemantics(const DeviceSemantics &sem) {
  s_device.semantics = sem;
}

// ---------------------------------------------------------------------------
// The poll
// ---------------------------------------------------------------------------

// The LVGL looper, installed by main.cpp before the first poll. The driver
// calls deviceYieldHook() while it waits for frames, which is the only reason
// the panel keeps drawing during a collection run; it lives here rather than in
// the driver so that no driver has to know about LVGL.
static void (*s_yieldHook)() = nullptr;

void deviceSetYieldHook(void (*fn)()) { s_yieldHook = fn; }

void deviceYieldHook() {
  if (s_yieldHook) {
    s_yieldHook();
  }
}

// How long one collection run may block. The per-frame windows belong to the
// driver; this is the ceiling for the whole run, and it is what a device that
// answers nothing at all may cost.
static const uint32_t kDevicePollBudgetMs = 4000;

void deviceBegin(const DeviceConfig &cfg) {
  DeviceDriver *drv = makeDriver(cfg);
  if (drv == nullptr) {
    Serial.printf("Geraet: Typ '%s' ist nicht implemented - keine Daten\\n",
                  cfg.type);
    s_device.connected = false;
    return;
  }
  drv->setTransport(&s_tcpLink);
  drv->begin(cfg);
  s_driver = drv;
}

void devicePoll() {
  if (s_driver == nullptr) {
    return;
  }
  s_driver->poll(kDevicePollBudgetMs);
}

const char *deviceTypeName() {
  return s_driver != nullptr ? s_driver->typeName() : "unbekannt";
}