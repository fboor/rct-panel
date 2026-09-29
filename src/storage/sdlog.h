// SD-card history logging (see docs/sd-history.md for the approved design).
//
// SPI mode on the 4848S040 TF slot. One blocking SPI write every 5 minutes
// has no impact on the RGB/LVGL path.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_SDLOG_H
#define RCT_SDLOG_H

#include "rct/RctTypes.h"

// sdInit(): kick off the first mount attempt (non-blocking).
// sdTick(): call every main loop iteration - retries the mount while the
//           card is absent and rebuilds the Service page status text.
void sdInit();
void sdTick();

// Append one 5-minute CSV row. Skips silently while the card is missing or
// before the first RCT frame arrived (no zero rows for a disconnected
// inverter).
void sdLogSample(const RctSnapshot &s);

// Service page status: "SD: OK · 8,4 GB frei" / "SD: --".
const char *sdStatusText();
bool sdMounted();

// History restore for the 24 h "Verlauf" page. One CSV row per log sample;
// the five power values mirror the chart series order (Netz, Haus, PV, S0,
// Bat) as sums over the logged phases/inputs, in W.
struct SdHistSample {
  uint32_t ts; // unix seconds of the row
  float v[5];  // {grid, house, pv, s0, battery} in W
};

// Read up to maxRows newest rows from the log (header skipped), taking over a
// calendar boundary when the current month file alone has fewer rows than
// maxRows. Rows come back in chronological order, oldest first. Returns the
// number stored, or 0 when the card / files are not available.
// maxRows must be <= 288.
//
// waitForClock: with waitForClock=true and the wall clock not yet valid (SNTP
// still pending at boot), the read is deferred and -1 is returned so the
// caller can retry once the time is there. Pass false to accept the pre-SNTP
// uptime-named file as a last resort.
int sdReadHistory(SdHistSample *out, int maxRows, bool waitForClock);

#endif // RCT_SDLOG_H