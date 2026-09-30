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

// sdInit(): start the worker task that owns the card. Non-blocking.
// sdTick(): call every main loop iteration. Kept as the caller's hook, but all
//           the work lives in the worker task now - mounting the card measured
//           1457 ms and would freeze the GUI, which shares this thread.
void sdInit();
void sdTick();

// Append one 5-minute CSV row. If the card is missing or the write fails, the
// row is parked in a 12-slot RAM ring (one hour at the 5-minute cadence) and
// retried; on overflow the oldest row goes and the loss shows up in
// sdStatusText(). Returns immediately - the card work happens in the worker.
void sdLogSample(const RctSnapshot &s);

// Service page status, e.g. "SD: OK | 8,4 GB frei", "SD: -- | 5 gepuffert",
// "SD: OK | 2 Zeilen verloren". Thread-safe (copied out under a lock).
const char *sdStatusText();
bool sdMounted();

// --------------------------------------------------------------------------
// History restore for the 24 h "Verlauf" page. One CSV row per log sample;
// the six values mirror the chart series order (Netz, Verbrauch, PV, S0,
// Bat, SOC); the first five are power sums over the logged phases/inputs in W,
// the sixth is the battery state of charge in percent.
//
// Asynchronous, because the scan reads whole CSV files (measured 1418 ms) and
// must not run on the GUI thread:
//   1. sdRequestHistory(maxRows, waitForClock)   - ask, returns immediately
//   2. sdTakeHistory(out, maxRows) each GUI tick - collect when it is ready
// maxRows must be <= 288 (288 * 5 min = 24 h).
//
// waitForClock: with waitForClock=true and the wall clock not yet valid (SNTP
// still pending at boot), the worker defers and sdTakeHistory() returns -1 so
// the caller can retry. Pass false to accept the pre-SNTP uptime-named file.
//
// sdTakeHistory returns:
//   >= 0  number of rows copied into out (chronological, oldest first)
//    -1   deferred, clock not ready yet - ask again
//    -2   nothing to collect yet, still running or not requested
// --------------------------------------------------------------------------
struct SdHistSample {
  uint32_t ts; // unix seconds of the row
  float v[6];  // {grid, house, pv, s0, battery, soc} - soc in %, rest in W
};

void sdRequestHistory(int maxRows, bool waitForClock);
int sdTakeHistory(SdHistSample *out, int maxRows);

// --------------------------------------------------------------------------
// Screenshot to /shot/*.bmp
//
// Hands a captured screen (rgb565, w*h pixels, row-major, top row first) to
// the card worker and returns immediately. The buffer must stay valid and
// untouched until the worker is done with it - the worker reports that by
// logging "SD: shot ... geschrieben", and sdTakeShotDone() tells the caller
// when it may free the buffer.
//
// The worker owns it because the conversion plus the write is far too slow for
// the GUI task: 691 kB at a 400 kHz SPI clock is seconds, not milliseconds.
// --------------------------------------------------------------------------
void sdScreenshot(const uint16_t *rgb565, int w, int h);
// True once the worker finished with the buffer passed to sdScreenshot().
bool sdTakeShotDone();

#endif // RCT_SDLOG_H