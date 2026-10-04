// OpenInverterGateway: an HTTP device, not a register bus.
//
// The second device family, and deliberately the one that behaves worst. An
// OpenInverterGateway is a stick in the inverter that answers HTTP instead of
// speaking a binary protocol, and it is driven by three properties that the RCT
// driver never had to think about:
//
//  1. **Which values exist depends on the model.** The stick publishes every
//     register of whichever protocol it speaks, and the protocols differ:
//     Growatt305 has twelve fields, Growatt307 has battery, grid and local-load
//     registers, GrowattSPF a different set again. The panel therefore learns
//     the shape from the answer (Json::has) and sets its caps from it. There is
//     no list of supported models here, because the answer is the list.
//
//  2. **A plain string inverter has no household meter and no grid meter.** It
//     knows its DC input and its AC output - its own numbers, not the house's.
//     The household value is then not zero, it is unknown, and the display says
//     so (DeviceCaps). Where the protocol does have local-load registers, they
//     are used, and caps.houseMeter is set accordingly.
//
//  3. **The device switches itself off at night.** Without generation and
//     without a battery there is nothing to run on, so it answers nothing from
//     dusk to dawn - hours, not seconds. That is not a fault and must not be
//     reported as one: the driver sets asleep when it has seen generation go
//     away and the link then stop answering, and DataStatus.h turns that into a
//     state of its own.
//
// Transport: plain HTTP over the same TCP transport the RCT uses, because the
// byte layer does not know what a request line is.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_OIG_DRIVER_H
#define RCT_OIG_DRIVER_H

#include "../device/DeviceDriver.h"

// The path the stick serves its values on. Kept as a constant because it is
// part of what makes this driver talk to an OpenInverterGateway at all.
static const char kOigStatusPath[] = "/status";

class OigDriver : public DeviceDriver {
public:
  void setTransport(DeviceTransport *link) override;
  void begin(const DeviceConfig &cfg) override;
  void poll(uint32_t budgetMs) override;
  bool connected() const override;

  // "OIG" - the family, and the prefix of the log file names.
  const char *typeName() const override { return "OIG"; }

  // Generation below this (W) for a full poll cycle counts as "the sun is not
  // up". Below the threshold the device is expected to shut down, so silence
  // afterwards is a state and not a fault. 20 W is the same figure the overview
  // uses to call generation inactive, so the panel does not change its mind
  // about what "nothing is being produced" means between the diagram and the
  // badge.
  static constexpr float kSleepPowerW = 20.0f;

  // How many poll cycles with no generation before the device is called asleep.
  // One is not enough: a cloud passing in front of the sun drops the production
  // for a cycle or two, and calling that "asleep" would report a night at noon.
  static const int kSleepCycles = 3;
};

#endif // RCT_OIG_DRIVER_H