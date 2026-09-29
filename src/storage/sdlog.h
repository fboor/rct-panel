// SD-card history logging (see docs/sd-history.md for the approved design).
//
// SPI mode on the 4848S040 TF slot. One blocking SPI write every 5 minutes
// has no impact on the RGB/LVGL path.
//
// The card may be pulled out while the panel runs, and a write can fail for
// other reasons too (full, marginal card). Rows that cannot be written are
// parked in a 12-slot RAM ring (one hour at the 5-minute cadence) and retried;
// on overflow the oldest row goes and the loss shows up in sdStatusText().
//
// SPDX-License-Identifier: MIT
#ifndef RCT_SDLOG_H
#define RCT_SDLOG_H

#include "rct/RctTypes.h"

// sdInit(): kick off the first mount attempt (non-blocking).
// sdTick(): call every main loop iteration - retries the mount while the
//           card is absent, watches for a card pulled out in operation, and
//           retries rows that could not be written.
void sdInit();
void sdTick();

// Append one 5-minute CSV row. If the card is missing or the write fails, the
// row is parked in RAM and written out later. Silently skipped before the
// first RCT frame arrived (no zero rows for a disconnected inverter).
void sdLogSample(const RctSnapshot &s);

// Service page status, e.g. "SD: OK | 8,4 GB frei", "SD: -- | 5 gepuffert",
// "SD: OK | 2 Zeilen verloren".
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