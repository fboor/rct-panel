// Which field of the answer is which quantity.
//
// An OpenInverterGateway publishes every register of whichever protocol it
// speaks, and the protocols do not agree on names. The frequency alone is
// AcFrequency on Growatt305, GridFrequency on Growatt120/124/307/BP/TLXH,
// LineFrequency and OutFrequency on GrowattSPF, and AcFreq in the simulator.
// Reading one of them and hoping is how the frequency went missing on a real
// stick: the driver asked for GridFrequency, the device said AcFrequency, and
// the value stayed zero - a number the display then showed as if it had been
// measured.
//
// So the names are data here, one list per quantity in the order that asks the
// most specific question first, and the driver asks which of them the answer
// contains. Nothing in the driver knows a protocol; a device family that is not
// in these tables shows nothing for that quantity, which is the honest result.
//
// Header-only and free of Arduino, like Rules.h, so tools/oig_field_test runs
// the tables against the real field names of all seven protocols - extracted
// from the OpenInverterGateway source, not written from memory. A name that
// does not exist in any protocol has no place in a list.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_OIG_FIELDS_H
#define RCT_OIG_FIELDS_H

#include <stddef.h>

#include "../device/Json.h"

// One list per quantity. The order matters and is not alphabetical: the first
// name that the answer contains is the one used.
//
// Grid frequency. AcFrequency (305) before GridFrequency (the rest), because a
// device that has both is a hybrid inverter whose grid frequency is the more
// specific value. OutFrequency (SPF) is the frequency at the inverter output,
// LineFrequency the one at its input - the grid is the latter, so it comes last.
static const char *const kOigFrequency[] = {"AcFrequency", "GridFrequency",
                                            "LineFrequency", "OutFrequency",
                                            "AcFreq", nullptr};

// Grid voltage. The AC output voltage is what a grid-connected string inverter
// measures; SPF has no AC output measurement and reports the grid input
// instead, which for it is the same net.
static const char *const kOigVoltage[] = {"AcVoltage", "L1ThreePhaseGridVoltage",
                                          "GridL1Voltage", "GridInVoltage",
                                          "OutVoltage", nullptr};

// Generation, the DC side. DcPower (305, simulator) is the DC input of the
// inverter; InputPower is the same measurement on the protocols that call it
// that; PV1Power/PV1ChargePwr are the per-string values, and PV2 is added to
// them because a two-string device reports two of them.
static const char *const kOigGeneration[] = {"DcPower",     "InputPower",
                                             "PV1Power",     "PV1ChargePwr",
                                             "PV2Power",     "PV2ChargePwr",
                                             nullptr};

// The AC output, which on a string inverter is what leaves it.
static const char *const kOigAcPower[] = {"AcPower", "ACPower", nullptr};

// Household load, where the protocol has local-load registers. A plain string
// inverter has none, and then there is no household meter.
static const char *const kOigHouse[] = {"ACPowerToUser", "INVPowerToLocalLoad",
                                        nullptr};

// Feed-in, where the protocol has export registers.
static const char *const kOigExport[] = {"ACPowerToGrid", nullptr};

// State of charge, in percent. The three names are three protocols' spellings
// of the same number; SOC first, because on a hybrid inverter it is the one the
// device itself offers.
static const char *const kOigSoc[] = {"SOC", "BattSOC", "BatteryPercentage",
                                      nullptr};

// Battery power, positive is charging. Every protocol spells its sign differently
// or not at all; the driver reconciles it against the physical one.
static const char *const kOigCharge[] = {"ChargePower", "BattPwr", "BattCharge",
                                        "BatteryCharge", nullptr};

static const char *const kOigBatteryVoltage[] = {"BatteryVoltage", "BattVoltage",
                                                 nullptr};

// Inverter status, the stick's own summary. 3 is a fault (GrowattTypes.h:
// GwStatusWaiting / Normal / Fault).
static const char *const kOigStatus[] = {"InverterStatus", "Status", nullptr};

// Device temperature, in degrees.
static const char *const kOigTemperature[] = {"Temperature", "InverterTemperature",
                                              "InverterTemp", nullptr};

// Energy counters, in kWh - the panel counts in Wh.
static const char *const kOigEnergyToday[] = {"EnergyToday", nullptr};
static const char *const kOigEnergyTotal[] = {"EnergyTotal", nullptr};

// Feed-in and household energy, where the protocol counts them.
static const char *const kOigEnergyToGrid[] = {"EnergyToGridToday",
                                               "TodayEnergyToGrid", nullptr};
static const char *const kOigEnergyToUser[] = {"EnergyToUserToday",
                                               "LocalLoadEnergyToday",
                                               "TodayEnergyToUser", nullptr};

// The first of the names the answer actually contains, or nullptr. The order of
// the list is the priority; see above.
inline const char *oigFieldPresent(const char *json, size_t len,
                                  const char *const *names) {
  for (size_t i = 0; names[i] != nullptr; i++) {
    if (json::has(json, len, names[i])) {
      return names[i];
    }
  }
  return nullptr;
}

// The first of the names the answer contains that also holds a number, written
// to out. `used` gets the name that was taken, or nullptr. A key that exists
// but holds something else is skipped rather than reported as a missing
// measurement - the device does publish it, we just cannot read it as a number.
inline bool oigNumber(const char *json, size_t len, const char *const *names,
                      float *out, const char **used = nullptr) {
  for (size_t i = 0; names[i] != nullptr; i++) {
    double v = 0;
    if (json::getNumber(json, len, names[i], &v)) {
      *out = (float)v;
      if (used != nullptr) {
        *used = names[i];
      }
      return true;
    }
  }
  if (used != nullptr) {
    *used = nullptr;
  }
  return false;
}

#endif // RCT_OIG_FIELDS_H