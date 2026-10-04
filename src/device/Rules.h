// The four derived quantities the panel displays, for every device family.
//
// Before this existed the household was computed in five places in C++ and
// once in the browser, always as "meter plus external generation", because
// that is true of the RCT and of nothing else: its load meter reads the demand
// already minus the S0 generator. A second device whose meter sees everything
// would have needed the same five places corrected, and the one that was
// forgotten would have been a silent wrong number rather than a gap.
//
// So the correction is not written here either. It lives in DeviceSemantics,
// the driver states it once, and the functions below apply it. A device that
// measures everything cleanly sets the flags and this file adds nothing.
//
// Header-only, free of Arduino, like DataStatus.h and Charts.h: shared because
// the rule is shared, not because there is code to put behind an interface.
// tools/device_test runs it on the host against both a clean device and the
// RCT's semantics.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_DEVICE_RULES_H
#define RCT_DEVICE_RULES_H

#include "DeviceState.h"

// --- momentary values --------------------------------------------------------

// Grid exchange, + = draw from the grid. The one sign convention of the whole
// project: negative while energy is fed in, so the switching output's surplus
// mode is the negated value and a threshold of 500 W means "500 W and more go
// into the grid".
inline float ruleGridExchangeW(const DeviceState &s) {
  return s.gridExchangeW;
}

// Feed-in as an amount, i.e. never negative.
inline float ruleFeedInW(const DeviceState &s) {
  const float w = -s.gridExchangeW; // + = export
  return w > 0.0f ? w : 0.0f;
}

// Household consumption over all phases, plus the external generation if this
// device's meter does not see it. The sum every consumer used to write itself.
inline float ruleHouseW(const DeviceState &s) {
  float w = s.houseW[0] + s.houseW[1] + s.houseW[2];
  if (!s.semantics.loadMeterSeesExternal) {
    w += s.extW;
  }
  return w;
}

// Generation over both generators, plus the external one if this device's
// counters do not see it. The overview's PV node.
inline float ruleGenerationW(const DeviceState &s) {
  float w = s.genW[0] + s.genW[1];
  if (!s.semantics.genCounterSeesExternal) {
    w += s.extW;
  }
  return w;
}

// Generation as the two generators alone - what the history chart shows as its
// own series, with the external generator as a second series beside it.
inline float ruleGeneratorW(const DeviceState &s) {
  return s.genW[0] + s.genW[1];
}

// Battery power, + = discharging, the convention the sign reconciliation in the
// driver produces.
inline float ruleBatteryW(const DeviceState &s) { return s.batW; }

// --- energy of one period ---------------------------------------------------

// One period's counters, straight from the device, before the rules are
// applied. period is 0 day, 1 month, 2 year, 3 lifetime - the order the pages
// and the JSON endpoint use.
struct RawPeriod {
  float genWh;
  float houseWh;
  float feedWh;
  float gridDrawWh;
  float extWh;
};

inline RawPeriod ruleRawPeriod(const DeviceState &s, int period) {
  switch (period) {
  case 1:
    return {s.monthGenWh, s.monthHouseWh, s.monthFeedInWh, s.monthGridDrawWh,
            s.monthExtWh};
  case 2:
    return {s.yearGenWh, s.yearHouseWh, s.yearFeedInWh, s.yearGridDrawWh,
            s.yearExtWh};
  case 3:
    return {s.totalGenWh, s.totalHouseWh, s.feedInTotalWh, s.gridDrawTotalWh,
            s.totalExtWh};
  default:
    return {s.dayGenWh, s.dayHouseWh, s.dayFeedInWh, s.dayGridDrawWh,
            s.dayExtWh};
  }
}

// Whether own use can be computed at all. It is a difference of two meters, and
// a difference needs both of them: a device without a house meter does not know
// what the house took, and one without a grid meter does not know what came from
// the grid. Both missing means there is no own consumption to show - not a zero
// one, and not a hundred per cent of self-sufficiency.
inline bool ruleOwnKnown(const DeviceState &s) {
  return s.caps.houseMeter && s.caps.gridMeter;
}

// What one period is worth, in the panel's terms. Own use is consumption minus
// grid draw - the energy the house took that did not come from the grid, whether
// it arrived straight from the array or out of the battery.
//
// Counting it on the way out and not on the way in is a decision, and the
// battery is the reason: what is charged today is used tomorrow, and the two
// days would otherwise tell different stories about the same kilowatt hours.
// The price is that the three bars no longer add up - generation, own use and
// feed-in differ by what is in the battery and what the conversion lost, which
// is a number the counters simply do not carry. It is not hidden: the bars are
// five counters and four of them are measured.
//
// The external energy goes to both sides when the device's counters exclude
// it. On the RCT that is what makes an external generator visible at all: it is
// in neither e_dc_* (generation) nor in the load meter (consumption), so
// without this it would appear nowhere. A device that already counts it in
// both gets it added twice over, which is why the flags decide rather than the
// display code.
//
// Own use is clamped at 0: the counters run apart for a moment after a device
// restart, and a negative bar would be meaningless. Where the two meters are
// missing, the value is 0 and ruleOwnKnown() says so - the callers leave the bar
// out rather than showing a zero that was never measured.
struct PeriodValues {
  float genWh;
  float ownWh;
  float feedWh;
  float gridDrawWh;
  float houseWh;
};

inline PeriodValues rulePeriod(const DeviceState &s, int period) {
  const RawPeriod raw = ruleRawPeriod(s, period);
  const bool extSeparate = !s.semantics.genCounterSeesExternal;

  PeriodValues v;
  v.genWh = raw.genWh + (extSeparate ? raw.extWh : 0.0f);
  v.houseWh = raw.houseWh + (extSeparate ? raw.extWh : 0.0f);
  v.feedWh = s.semantics.feedCounterNegative && raw.feedWh < 0.0f
                  ? -raw.feedWh
                  : raw.feedWh;
  v.gridDrawWh = raw.gridDrawWh;
  const float diff = v.houseWh - v.gridDrawWh;
  v.ownWh = diff > 0.0f ? diff : 0.0f;
  return v;
}

// --- the six series of the history ------------------------------------------
//
// Order is the chart series order: grid, household, generation, external
// generator, battery, state of charge. The last one is a percentage, the rest
// are watts.
//
// Generation is the two generators alone, not the total: the external
// generator has its own series beside it, so adding it here would count it
// twice in the chart. The rule lives here rather than in each consumer for the
// same reason as above, and the browser's own copy reads it out of the CSV in
// the same form.
inline void ruleChartSample(const DeviceState &s, uint32_t ts, float out[6]) {
  out[0] = ruleGridExchangeW(s);
  out[1] = ruleHouseW(s);
  out[2] = ruleGeneratorW(s);
  out[3] = s.extW;
  out[4] = ruleBatteryW(s);
  out[5] = s.socPct;
  (void)ts;
}

#endif // RCT_DEVICE_RULES_H