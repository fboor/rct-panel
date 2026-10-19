// What the panel knows about the plant, and how those numbers came to be.
//
// This is the shape every component reads: the GUI, the web interface, the
// switching output, the CSV logger and the history. None of them may reach
// past it into a device driver, and no field name may name a register, an OID
// or a device family - the point of the file is that "household consumption"
// is a property of the plant, not of the RCT.
//
// Header-only and free of Arduino on purpose: the rules that read this state
// live in Rules.h and are checked on the host (tools/device_test), so nothing
// in here may pull in the framework.
//
// Units are in the field names, because they differ within one struct - the
// powers are W, the counters Wh, the state of charge percent - and because a
// second device family will fill the same fields from different registers.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_DEVICE_STATE_H
#define RCT_DEVICE_STATE_H

#include <stdint.h>

#include "DeviceCaps.h"

// How this device's numbers have to be read.
//
// The panel wants four derived quantities - household consumption, total
// generation, feed-in and own use - and there is no way to get them from the
// raw meters alone: every device family has its own idea of what its household
// meter sees and where its external generator ends up. So instead of writing
// the RCT's correction into the display code (where it stood five times), the
// driver states the three facts and Rules.h applies them. A second device
// fills in a different struct; no new branch appears in the panel.
//
// The three flags are deliberately few. Anything that is not one of these -
// whether a battery exists at all - is still answered by the have* flags in
// DeviceState, which say "has not answered yet" rather than "cannot".
struct DeviceSemantics {
  // The household meter already counts the external generator:
  //   household = sum(houseW) + (sees it ? 0 : extW)
  // False on the RCT: its load meter reads the demand already minus the S0
  // generator, so the external power has to be added back. Without it the
  // household reads the generator instead of the house, and goes negative
  // whenever the external input produces more than the meter sees.
  bool loadMeterSeesExternal;

  // The generation counters (day/month/year/lifetime) already include the
  // external generator:
  //   generation = genW + (counts it ? 0 : extW)
  // False on the RCT: the e_dc_* family only counts the two DC inputs, so
  // without the external counters the generator is missing from generation,
  // and - because the load meter does not see it either - from consumption
  // and own use too.
  bool genCounterSeesExternal;

  // The feed-in counters arrive as a negative value, so the magnitude is the
  // energy that went in. Measured on the RCT: -20,1 kWh on a day with 32,5 kWh
  // production. Shown as reported, the bar grew leftwards and the value read
  // -549,7 kWh, which is not an amount of energy that was fed in.
  bool feedCounterNegative;
};

// Everything the panel displays, from one device, with the units in the names.
struct DeviceState {
  // --- grid exchange, + = drawn from the grid --------------------------------
  float gridW[3];    // per phase [W]; phases the device does not have stay 0
  float gridExchangeW; // total [W], + = Bezug (import)

  // --- grid quality ----------------------------------------------------------
  float gridV[3];    // per phase [V]
  float gridHz[3];   // per phase [Hz]

  // --- generation ------------------------------------------------------------
  float genW[2];     // generator A / B [W], >= 0 production
  float extW;        // external generator (the RCT reads it at S0) [W]
  // Energy integrated from extW, in Wh, accumulated since boot. Not displayed:
  // the device's own counters are authoritative, because those survive a
  // restart of the panel while this integration does not. Kept as an
  // independent cross-check - it must track the rise of the device's counter,
  // and a growing divergence means that counter is not counting what its name
  // says.
  float extEnergyWh;

  // --- household -------------------------------------------------------------
  float houseW[3];   // per phase [W], as the device's meter sees it

  // --- battery ---------------------------------------------------------------
  float socPct;      // [%]
  // Sign convention, measured on the real device: PV 0 W | Haus 832 W |
  // Netz +4 W | Batterie +810 W. With no production the battery cannot be
  // charging, and 810 + 4 balances the 832 W the house draws, so positive is
  // discharging.
  float batW;        // [W], positive = discharging
  float batV;        // [V]
  float batA;        // [A], positive = discharging
  uint32_t batteryStatus; // status bitfield
  float batteryTemp; // battery temperature [°C]
  float batteryCycles; // charge/discharge cycles
  float batterySoh;  // state of health [%]

  // --- day counters ----------------------------------------------------------
  float dayGenWh;     // generated today [Wh]
  float dayHouseWh;   // household today [Wh]
  float dayFeedInWh;  // fed into the grid today [Wh]
  float dayGridDrawWh; // drawn from the grid today [Wh]
  // External energy from the device's own counters. The RCT carries both a
  // summed and an unsummed variant of each period; the summed one is
  // displayed, the unsummed day and month pair is kept for comparison in the
  // log, because which is meant is documented nowhere.
  float dayExtWh;     // summed external generator today [Wh]
  float dayExtPlainWh; // unsummed variant [Wh]

  // --- month / year / lifetime ----------------------------------------------
  float monthGenWh, yearGenWh, totalGenWh;
  float totalGenAWh, totalGenBWh; // lifetime per generator [Wh]
  float monthHouseWh, yearHouseWh, totalHouseWh;
  float monthFeedInWh, yearFeedInWh, feedInTotalWh;
  float monthGridDrawWh, yearGridDrawWh, gridDrawTotalWh;
  float monthExtWh, yearExtWh, totalExtWh;
  float monthExtPlainWh;

  // --- device info (page "Gerät") -------------------------------------------
  char deviceName[40];      // device name
  char firmwareVersion[24]; // control software version
  float coreTemp;           // core temperature [°C]
  float heatSinkTemp;       // heat sink temperature [°C]
  uint32_t nextCalibTs;     // next battery calibration [unix s]
  uint32_t faultBits[4];    // fault words, 128 fault bits

  // --- the device's own event log --------------------------------------------
  // What the device wrote down itself. The panel never asks for this table: it
  // arrives when another client on the bus asks (the app, on a phone) and is
  // read along - see src/rct/RctEvents.h for why. So it is empty until somebody
  // opens the log in the app, and it stays as it is afterwards: the panel does
  // not page back through it.
  //
  // The type is the number of a fault bit - except for the three types above
  // the bits (0x80..0x82) - so an entry is named with the texts the panel
  // already has (T_FAULT_0 + type).
  struct DeviceEvent {
    uint8_t type;
    uint32_t startUnix; // the device's clock
    uint32_t endUnix;   // end of a ranged event, or something else - per type
    uint32_t value1;    // raw; what it means depends on the type
    uint32_t value2;
  };
  // How many entries are kept. The list is shown newest first; anything older
  // would need another request, which the panel does not make.
  enum { kEventMax = 40 };
  DeviceEvent events[kEventMax]; // newest first, by start time
  int eventCount;
  uint32_t eventQueryUnix; // the window the last answer was for, 0 = none yet

  // --- state -----------------------------------------------------------------
  // prim_sm.island_flag is a bitfield; only bit 0 is the island flag. Measured
  // on the real device: 0x00000002 while running normally on the grid.
  // islandKnown distinguishes "the device said 0" from "the device has not
  // answered yet" - both read as 0 in the raw register.
  bool islandMode;
  bool islandKnown;

  // The device is off on purpose and that is normal: a battery-less inverter
  // shuts down when there is no generation and answers nothing until morning.
  // Set by the driver, not by the panel, because only the driver knows that
  // its family does this - and it is the difference between "wartet" (data is
  // late) and "schläft" (nobody is going to answer). Without it the panel
  // would report a fault every night for a device that is doing exactly what it
  // is built to do.
  bool asleep;

  bool haveData;    // any value ever received from the device
  bool haveBattery; // state of charge ever answered (device has a battery)
  bool connected;   // link up right now
  uint32_t lastUpdateMs; // timestamp of the last received frame

  // How this device's numbers have to be read. Set once by the driver at
  // begin() and never changed afterwards.
  DeviceSemantics semantics;

  // What this family can report at all. Set once by the driver at begin().
  DeviceCaps caps;
};

#endif // RCT_DEVICE_STATE_H