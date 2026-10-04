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

// The AC output, which on a string inverter is what leaves it. OutputPower is the
// same measurement on the protocol of the MIC 1000, where InputPower is the DC
// side (and already the generation above) - the pair reads like the two ends of
// the inverter, which is what it is.
static const char *const kOigAcPower[] = {"AcPower", "ACPower", "OutputPower",
                                          nullptr};

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

// Battery power, positive is discharging - the other end of the same pair, and a
// separate register on the protocols that publish both. That it is a separate
// register and not the negated charge value is the point: a device that reports
// only ChargePower reports nothing at all while it discharges, so a battery that
// spends the afternoon discharging looks to the panel like one doing nothing.
static const char *const kOigDischarge[] = {"DischargePower", "BattDischarge",
                                            "BattDischargePwr", nullptr};

// Is a battery attached at all. A hybrid inverter publishes the battery registers
// either way - a MIC 1000 without a battery reports BatteryState 0, SOC 0,
// ChargePower 0, DischargePower 0 and BatteryVoltage 0 - so the existence of a
// register proves nothing, and this one register is the device's own answer.
//
// The values are not documented in the OpenInverterGateway (register 1041, unit
// NONE, in Growatt124.cpp and Growatt307.cpp), so the rule is deliberately the
// weak one: zero means none attached, anything else means one is. The driver logs
// the raw value, so if a real battery reports something unexpected that is in the
// serial log rather than in a guess in the source.
static const char *const kOigBatteryState[] = {"BatteryState", "BattState",
                                               "BatteryConnected", nullptr};

static const char *const kOigBatteryVoltage[] = {"BatteryVoltage", "BattVoltage",
                                                 nullptr};

// Battery temperature, in degrees - its own register, and not the inverter's.
// TemperatureInsideIPM and BoostTemperature are components of the inverter, not
// the battery, and mixing them would put a component temperature on the battery
// page.
static const char *const kOigBatteryTemperature[] = {"BatteryTemperature",
                                                     "BattTemperature", nullptr};

// Inverter status, the stick's own summary. 3 is a fault (GrowattTypes.h:
// GwStatusWaiting / Normal / Fault).
static const char *const kOigStatus[] = {"InverterStatus", "Status", nullptr};

// Device temperature, in degrees.
static const char *const kOigTemperature[] = {"Temperature", "InverterTemperature",
                                              "InverterTemp", nullptr};

// Energy counters, in kWh - the panel counts in Wh.
//
// TodayGenerateEnergy and TotalGenerateEnergy are the MIC 1000's spelling of the
// same two counters. PVEnergyTotal comes after them and never before: on some
// models it is a lifetime figure and on others only one string, and a total that
// is really half of the total is worse than no total.
static const char *const kOigEnergyToday[] = {"EnergyToday", "TodayGenerateEnergy",
                                              nullptr};
static const char *const kOigEnergyTotal[] = {"EnergyTotal", "TotalGenerateEnergy",
                                              "PVEnergyTotal", nullptr};

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

// Is a published meter register a meter at all? A hybrid inverter publishes
// ACPowerToUser and ACPowerToGrid whether or not a meter is fitted: a MIC 1000
// without one reports 0 for both, every poll, and nothing in the answer says
// otherwise - no flag, no state register, nothing. The panel cannot tell that from
// a house that happens to draw nothing at that moment, and it did: it drew a house
// node and a grid node full of zeros, which is the one thing the capability layer
// was built to stop.
//
// The difference that does show up in the answer is time. A household draws
// something within a day - the fridge alone is well past this - and a plant exports
// something whenever the sun is on it, so a register that has only ever read zero
// is a register with no meter behind it. hoechsterWertW is the largest absolute
// value that register has held since boot, and the driver keeps it: a meter that
// has been seen stays a meter when it reads zero this second.
//
// Half a watt is the bar, because a register with a resolution of 1 W that has read
// anything has read at least one watt, and half a watt leaves room for a register
// that reports a scaled fraction.
inline bool oigMeterVorhanden(float hoechsterWertW) { return hoechsterWertW >= 0.5f; }

// The household's consumption where there is no load meter of its own: what the
// inverter delivers on its AC side, minus what it feeds into the grid. With both
// of those measured it is the household by physics, and the device usually
// computes the same sum internally.
//
// It is only usable where that is true - both halves measured. A device that
// publishes a feed-in register which never leaves zero has no measurement of its
// grid connection either, and subtracting a permanent zero from the AC output
// hands back the AC output under the name "household". See the driver, which
// checks oigMeterVorhanden() for the grid register before it uses this.
//
// Positive is consumption, which is the panel's convention. Clamped at zero,
// because a device that reports more feed-in than it delivers - a rounding
// difference, or a load that changed between the two registers - would otherwise
// produce a household that consumes negative watts, and a negative household is a
// thing that does not exist.
inline float oigHausAusAc(float acW, float einspeisungW) {
  const float rest = acW - einspeisungW;
  return rest > 0.0f ? rest : 0.0f;
}

// Is a battery attached to the device at all - as opposed to the device
// publishing battery registers, which a hybrid does whether or not one is there.
//
// The state register decides when it exists: zero is "none attached", anything
// else is "one is" (see kOigBatteryState for what is and is not documented about
// the values). A protocol without that register falls back to the old rule - an
// SOC field exists - because there the registers themselves are the only answer
// the device gives. rawState gets the value that was read, for the log.
//
// This is a rule and not a flag, so it lives here where the host test can reach
// it: a MIC 1000 without a battery publishes SOC, ChargePower, DischargePower,
// BatteryVoltage and BatteryTemperature, all of them zero, and every one of those
// registers would otherwise be read as "there is a battery, doing nothing".
inline bool oigBatteryPresent(const char *json, size_t len, float *rawState) {
  float state = 0.0f;
  if (oigNumber(json, len, kOigBatteryState, &state)) {
    if (rawState != nullptr) {
      *rawState = state;
    }
    return state != 0.0f;
  }
  if (rawState != nullptr) {
    *rawState = 0.0f;
  }
  float soc = 0.0f;
  return oigNumber(json, len, kOigSoc, &soc);
}

#endif // RCT_OIG_FIELDS_H