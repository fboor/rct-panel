// SD-card history logging (see docs/sd-history.md for the approved design).
//
// SPI mode on the 4848S040 TF slot. One blocking SPI write every 5 minutes
// has no impact on the RGB/LVGL path.
//
// The card may be pulled out while the panel runs, and a write can fail for
// other reasons too (full, marginal card). Rows that cannot be written are
// parked in a RAM ring and retried; on overflow the oldest row goes and the
// loss shows up in sdStatusText(). The ring holds 24 h (288 rows at the
// 5-minute cadence) in PSRAM when the panel has PSRAM, and falls back to one
// hour in internal RAM without it.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_SDLOG_H
#define RCT_SDLOG_H

#include "rct/RctTypes.h"
#include "storage/CsvRow.h"

// The chart row format lives in storage/CsvRow.h, together with the reader for
// it, and is used here unchanged: sdWriteRow() writes it, the history scan
// reads it back.
typedef csvrow::Sample SdHistSample;

// sdInit(): start the worker task that owns the card. Non-blocking.
// sdTick(): call every main loop iteration. Kept as the caller's hook, but all
//           the work lives in the worker task now - mounting the card measured
//           1457 ms and would freeze the GUI, which shares this thread.
void sdInit();
void sdTick();

// Append one 5-minute CSV row. If the card is missing or the write fails, the
// row is parked in the RAM ring (up to 24 h) and retried; on overflow the
// oldest row goes and the loss shows up in sdStatusText(). Returns immediately
// - the card work happens in the worker.
void sdLogSample(const RctSnapshot &s);

// Service page status, e.g. "SD: OK | 8,4 GB frei", "SD: -- | 5 gepuffert
// (25 min)", "SD: OK | 2 Zeilen verloren". Thread-safe (copied out under a
// lock).
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
// the GUI task: 691 kB, and even at 4 MHz that is over a second.
// --------------------------------------------------------------------------
void sdScreenshot(const uint16_t *rgb565, int w, int h);
// True once the worker finished with the buffer passed to sdScreenshot().
bool sdTakeShotDone();

// --------------------------------------------------------------------------
// File streaming and directory listing (web interface, see
// docs/web-interface.md)
//
// Same rule as the rest of this header: the card belongs to the worker task,
// so the caller never touches it. The web server streams a file out of a card
// by asking for one chunk per GUI tick and writing that chunk to the socket.
// A single stream runs at a time; a second request replaces the first.
//
//   sdRequestStream(path, tailBytes) - ask. tailBytes 0 = whole file, otherwise
//                                    start that many bytes back from the end
//                                    (clamped to the file size). Returns
//                                    immediately.
//   sdStreamTotal()                 - byte count the caller will receive, 0 if
//                                    no stream is open. Read this right after
//                                    requesting, so the web server can send a
//                                    real Content-Length and the browser shows
//                                    a true progress bar.
//   sdTakeStreamChunk(out, max)     - copy out the next piece: bytes copied,
//                                    0 if the worker has not filled a chunk yet
//                                    (ask again next tick), -1 when finished or
//                                    failed. Terminal: ask until -1.
//   sdStreamFailed()                - true if the worker could not open the
//                                    file (distinguishes an empty reply from a
//                                    finished download).
//   sdStopStream()                  - abort (browser closed the connection,
//                                    another request came in, provisioning
//                                    started). Safe to call at any time.
//
// A browser that stops reading stops the card: the single chunk buffer stays
// full and the worker waits. At most one chunk of delay for a queued 5-minute
// CSV row, so logging and streaming do not starve each other.
// --------------------------------------------------------------------------
void sdRequestStream(const char *path, uint32_t tailBytes);
uint32_t sdStreamTotal();
int sdTakeStreamChunk(uint8_t *out, uint32_t max);
bool sdStreamFailed();
void sdStopStream();

// Directory listing for the web interface ("/hist" or "/shot"), one line per
// entry as "name|size|epoch" in directory order.
//
// The card belongs to the worker task, so it fills a cache and the caller reads
// that: sdRequestListing() asks for a refresh (a no-op while the cache is younger
// than a few seconds), sdListingText() hands out the cached text in the same pass.
// Its return is the number of bytes written into out, 0 for an empty directory,
// or kListingUnavailable if the card cannot be read or the cache is still cold.
void sdRequestListing(const char *path);
int sdListingText(const char *path, char *out, size_t cap);

// Return value of sdListingText() that means "no answer available".
#define kListingUnavailable (-1)

// SPI clock the card is currently mounted at, in Hz (0 when no card). Shown on
// the web interface's overview: 4000000 is the normal case, 400000 means the
// card failed the self-test at 4 MHz and the bus was dropped back.
uint32_t sdSpiHz();

#endif // RCT_SDLOG_H