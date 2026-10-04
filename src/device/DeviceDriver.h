// What a driver has to do, and what it must not.
//
// A driver turns one device family into DeviceState. It is the only place in
// the firmware that knows how that device is addressed and what its registers
// mean; the GUI, the web interface, the relay and the CSV logger know none of
// it. The one implementation today is src/rct/RctDriver.cpp; a second family is
// a second class behind this interface and nothing else.
//
// The timing contract is the part that is easy to get wrong and expensive to
// debug, so it is stated here rather than left to a comment: poll() runs in the
// task that also runs LVGL. It blocks - it waits for frames on a device that may
// not answer - and while it does, it must call the yield hook that main.cpp
// installed, or the panel stands still for as long as the wait lasts. It must
// stay inside budgetMs for the same reason, hold its last good value when a
// value does not arrive rather than dropping it to zero, and never drop the
// link because a frame did not match.
//
// No allocation, no task, no timer of its own. The yield hook and the
// collection budget live in Device.h, because they belong to the panel rather
// than to any one driver.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_DEVICE_DRIVER_H
#define RCT_DEVICE_DRIVER_H

#include <stdint.h>

#include "DeviceConfig.h"
#include "DeviceState.h"
#include "DeviceTransport.h"

class DeviceDriver {
public:
  virtual ~DeviceDriver() = default;

  // The panel hands the driver its link; a driver never opens a connection
  // itself. That is what keeps the transport swappable, and what lets a host
  // test hand the driver a link of its own without a network.
  virtual void setTransport(DeviceTransport *link) = 0;

  // Once, after the settings are loaded and before the first poll. Set the
  // meter semantics here (deviceSetSemantics) - that is the driver's statement
  // of how this family's meters read, and every rule follows from it.
  virtual void begin(const DeviceConfig &cfg) = 0;

  // One collection run. See the header for the timing contract.
  virtual void poll(uint32_t budgetMs) = 0;

  // Link up right now, for the status line and the reconnect decision.
  virtual bool connected() const = 0;

  // The family as a short name. It ends up in the log file names and in the
  // settings page, so it stays short, upper case and free of spaces.
  virtual const char *typeName() const = 0;
};

// The driver for cfg.type, or nullptr for a type nobody implements. A nullptr
// is a settings mistake, and the caller reports it rather than guessing - a
// panel without data is bad enough without a silent fallback to a device that
// is not the one configured.
DeviceDriver *makeDriver(const DeviceConfig &cfg);

// The port a family answers on by default, "" for a type nobody implements. The
// settings page puts it into the port field when the type changes, so that
// switching a device does not also mean remembering a number.
const char *deviceDefaultPort(const char *type);

#endif // RCT_DEVICE_DRIVER_H