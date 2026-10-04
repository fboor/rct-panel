// What this device family can report at all.
//
// The first driver hid this in three flags that each answered a different
// question: haveBattery (has the state of charge ever arrived), islandKnown (has
// the island flag ever answered) and a grid sum that was zero because there is
// no grid meter. A second device family makes the difference unavoidable - a
// plain PV inverter behind an OpenInverterGateway has no battery and no
// household meter at all, and a panel that keeps drawing the battery node and
// the household value is drawing a zero and calling it a measurement.
//
// So the driver states its shape once, in caps, and the display asks before it
// shows. Four rules keep this honest:
//
//   - A flag is about the device family, not about this moment. "No household
//     meter" is a property of the hardware; "the value is old" is a property of
//     the network, and that one stays in DataStatus.h where it belongs.
//   - The driver sets the flags in begin() from what it knows about the family,
//     not from what happened to arrive. A driver that sets them from the answer
//     would flip them off on one bad poll and the display would flicker.
//   - Nothing here decides a number. Whether the household is meter plus the
//     external generator is DeviceSemantics; whether there is a household meter
//     at all is this file.
//   - A derived number needs measured inputs, both of them. "What the inverter
//     delivers minus what it feeds in" is a household only where both halves are
//     measurements - and a device whose feed-in register reads zero for ever has
//     just told us that it has no meter there. Subtracting that zero hands back
//     the AC output wearing the name of a household, which is a guess with a
//     number on it. Where we can tell that something is missing, the answer is
//     that we do not have the value; not a plausible one.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_DEVICE_CAPS_H
#define RCT_DEVICE_CAPS_H

struct DeviceCaps {
  // A meter of the household's own consumption. Without one the panel cannot
  // show a household value at all - not zero, but nothing: the difference
  // between "the house drew nothing" and "nobody measured the house" is the
  // whole point of the flag.
  //
  // An OpenInverterGateway reports what its inverter measured, and a plain
  // string inverter measures neither the house nor the grid: it knows its DC
  // input and its AC output. Those are the inverter's own numbers, not the
  // house's.
  bool houseMeter;

  // A meter at the grid connection: the value that answers "is energy leaving
  // or entering the house". Its absence is not an error, it is what a string
  // inverter without an export meter looks like.
  //
  // It also decides whether the own consumption exists at all: that figure is
  // consumption minus grid draw, a difference of two meters, so it needs this
  // one and houseMeter together - see ruleOwnKnown() in Rules.h. A device
  // without them has no own consumption, no self-sufficiency and no share, and
  // the pages write a dash instead of a figure.
  bool gridMeter;

  // A battery behind the inverter, with a state of charge. Drives the battery
  // node, the sixth chart series and the energy page's own use.
  bool battery;

  // An island / off-grid flag. The switching output's island rule and the
  // warning triangle on the connector.
  bool islandFlag;

  // Inverter fault bits. The service page's fault list and the switching
  // output's fault rule.
  bool faultBits;

  // The device can be powered down on purpose and does so: no generation, no
  // supply, nothing answering for hours. An inverter without battery does this
  // every night. It changes what "no answer" means - see DeviceState::asleep -
  // and nothing else: the values are still the values, they are just not being
  // refreshed because the device is off.
  bool sleepsWithoutGeneration;

  // Filled by the driver family. A driver that does not know, does not set it.
  bool isKnown() const {
    return houseMeter || gridMeter || battery || islandFlag || faultBits ||
           sleepsWithoutGeneration;
  }
};

#endif // RCT_DEVICE_CAPS_H