// SD-card history logging (docs/sd-history.md).
//
// CSV, one file per calendar month on a FAT32 microSD in the TF slot:
//   sdInit()  -> starts the worker task that owns the card
//   sdTick()  -> kicks the worker (non-blocking)
//   sdLogSample() -> queues one row (caller drives the 5-minute cadence)
//
// Why a task: SD.begin() alone measured 1457 ms, the history restore 1418 ms,
// and a full card read is seconds. LVGL runs on the same task as this file's
// callers, so a single long card operation froze the whole panel - GUI redraw
// and touch included - for over a second. The worker owns the card exclusively
// (the ESP32 SD/FS layer is not thread-safe) and the GUI thread only ever posts
// messages to it.
//
// SPI wiring on the 4848S040 (cross-checked with a working Tasmota setup):
//   SCK = 48, MOSI = 47 (shared with the boot-only bit-banged LCD config
//   SPI; idle after init), MISO = 41, CS = 42. Uses the FSPI peripheral,
//   blocking, no DMA.
//
// Clock: the card is mounted at 4 MHz (the SD library's own default) and falls
// back to the 400 kHz that worked for every earlier release. Not a free choice
// - see kSdFastHz: the library runs *every* transaction, CMD0 included, at the
// given frequency, so a marginal card is detected only by the round-trip probe.
//
// SPDX-License-Identifier: MIT
#include "storage/sdlog.h"

#include <Arduino.h>
#include <SD.h>

#include "../Diag.h"
#include <SPI.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "storage/RowQueue.h"

namespace {

// TF slot pins (see docs/sd-history.md section 1).
constexpr uint8_t kSpiSck = 48;
constexpr uint8_t kSpiMiso = 41;
constexpr uint8_t kSpiMosi = 47;
constexpr uint8_t kSpiSs = 42;

SPIClass s_spi(FSPI);
volatile bool s_mounted = false; // read by the GUI thread via sdMounted()
bool s_probePending = true;      // PROBE self-test marker exists until first flush
uint32_t s_nextMountMs = 0;
constexpr uint32_t kMountRetryMs = 10000; // retry every 10 s without a card

// SPI clock for the card. 4 MHz first, 400 kHz as the fallback that has
// proven itself on every release so far.
//
// The Arduino SD stack takes the frequency as a plain argument and keeps it in
// the card state (libraries/SD/src/sd_diskio.cpp: card->frequency = hz, and
// every transaction runs at SPISettings(card->frequency)). There is no
// automatic slow-down, so the *card detection itself* - the part that fails on
// a marginal card with "physical drive cannot work" - already runs at the fast
// clock. That is why the fast clock is only kept when the card proves it with
// the write+read-back probe (probeMount) and why the probe result decides for
// the rest of the session instead of being retried every 10 s.
constexpr uint32_t kSdFastHz = 4000000;
constexpr uint32_t kSdSlowHz = 400000;
int s_fastUsable = -1; // -1 untested, 1 fast clock works, 0 dropped to 400 kHz
uint32_t s_spiHz = kSdSlowHz;

char s_path[32];    // "/hist/RCT-202609.csv" or "/hist/UPT-<days>.csv"
char s_pathKey[16]; // rotation key of the currently open file ("202609" ...)
bool s_pathValid = false;
bool s_warnLogged = false; // one-time write-error message per mount

// 64, not 48: the status line grew a time span ("288 gepuffert (24 h)"), and
// the worst case - a full ring, a month of data and free space all at once - is
// 45 characters.
char s_status[64] = "SD: --";

// The row format (16 columns) and its reader live in storage/CsvRow.h, so the
// writer here, the history scan below and the file the web interface hands
// out cannot drift apart - tools/sd_queue_test walks a row through the round
// trip.
constexpr size_t kLineCap = csvrow::kLineCap;
constexpr size_t kPathCap = csvrow::kPathCap;

// --------------------------------------------------------------------------
// Write-failure queue
//
// The card can be pulled out while the panel is running, and a write can fail
// for other reasons too (card full, marginal card). Dropping the row would
// punch a hole in the 24 h chart, so a row that cannot be written right now is
// parked in RAM and retried later.
//
// 288 slots = 24 h at the 5-minute cadence, the same span the chart shows, so
// a card that is gone for a day loses nothing and the rows land in the month
// file in order when it comes back. The slots live in PSRAM (216 bytes each,
// ~62 kB of the 8 MB) and the fallback without PSRAM is the 12 slots in
// internal RAM that earlier releases used. On overflow the oldest row goes,
// because recent data is what the chart needs; how many rows were lost stays
// visible in the status text. A row is stored already formatted rather than as
// a snapshot, so it keeps its original timestamp, and it remembers the path it
// belongs to - a month rollover during the outage then still splits correctly
// across two files.
// --------------------------------------------------------------------------
constexpr int kQueueCap = 288;        // 24 h, PSRAM
constexpr int kQueueCapNoPsram = 12;  // 1 h, internal RAM
constexpr int kHistMaxRows = 288;     // 288 * 5 min = 24 h (matches GuiApp)
constexpr uint32_t kQueueRetryMs = 5000; // retry parked rows from sdTick()
constexpr uint32_t kProbeMs = 5000;      // ask SD.cardSize() for card presence

// The ring itself (storage/RowQueue.h) is unit-tested on the build machine.
rowq::Slot s_queueSlots[kQueueCapNoPsram]; // always there, the no-PSRAM fallback
rowq::Queue s_queue;
uint32_t s_nextQueueRetryMs = 0;
uint32_t s_nextProbeMs = 0; // next card-presence check

void buildStatus(); // defined below, called by queueFlush()

// --------------------------------------------------------------------------
// Worker task: the only context that ever touches the card.
//
// The GUI thread (Arduino loop + LVGL) posts the three things it needs - write
// a row, read the history, report status - and never waits for them.
// --------------------------------------------------------------------------
enum SdReqType : uint8_t { SDREQ_LOG, SDREQ_HISTORY, SDREQ_SHOT, SDREQ_STREAM,
                          SDREQ_LIST };

struct SdReq {
  uint8_t type;
  int maxRows;         // SDREQ_HISTORY
  bool waitForClock;   // SDREQ_HISTORY
  uint32_t tailBytes;  // SDREQ_STREAM: start that many bytes back from the end
  // SDREQ_SHOT: the captured screen. Owned by the caller until the worker sets
  // s_shotDone, which is the only thing that makes it safe to free again.
  const uint16_t *px;
  int w, h;
  char line[kLineCap];
  char path[kPathCap];
};

constexpr int kReqQueueLen = 4; // rows arrive every 5 min; depth 4 is ample
QueueHandle_t s_reqQ = nullptr;

// Guards s_status and the history result handoff between the two tasks.
SemaphoreHandle_t s_lock = nullptr;

// History restore result, written by the worker, collected by the GUI.
static SdHistSample s_histOut[kHistMaxRows];
static int s_histReady = 0; // 1 when a fresh result is waiting
static int s_histCount = 0;  // rows in s_histOut (see sdTakeHistory)
static bool s_histDeferred = false; // clock not ready yet, caller may retry
// Screenshot handoff: true once the worker is done with the caller's buffer.
static bool s_shotDone = true; // nothing in flight
// One converted BMP scanline, reused for every row. Static, not on the stack:
// the worker task has 4 kB and the card library already needs a good part of
// that. Sized for kShotMaxW pixels at 3 bytes each.
#define kShotMaxW 512
static uint8_t s_shotRow[kShotMaxW * 3];

// "202609" from the local calendar (SNTP; CET/CEST configured in main.cpp),
// or 0 when the wall clock is not valid yet.
const char *monthKey() {
  static char key[16];
  time_t nowT = time(nullptr);
  struct tm tmv;
  if (nowT < 1000000000 || !localtime_r(&nowT, &tmv)) {
    return nullptr;
  }
  snprintf(key, sizeof(key), "%04d%02d", tmv.tm_year + 1900, tmv.tm_mon + 1);
  return key;
}

// Uptime key (days since boot) until SNTP is valid. esp_timer is 64-bit, so
// no millis() wrap-around over the panel lifetime.
const char *uptimeKey() {
  static char key[16];
  uint64_t days = esp_timer_get_time() / UINT64_C(86400000000);
  snprintf(key, sizeof(key), "UPT-%llu", (unsigned long long)days);
  return key;
}

// Pick the file for this moment and remember its rotation key. Returns false
// when nothing changed or the card is gone.
bool updatePath() {
  const char *key = monthKey();
  const bool haveClock = key != nullptr;
  if (key == nullptr) {
    key = uptimeKey();
  }
  if (s_pathValid && strcmp(s_pathKey, key) == 0) {
    return true;
  }
  const bool hadPath = s_pathValid;
  // Paths must be absolute: the VFS layer rejects anything not starting with
  // "/" (the volume is mounted at /sd).
  snprintf(s_path, sizeof(s_path), "/hist/RCT-%s.csv", key);
  if (!s_pathValid && !haveClock) {
    // First sample before the clock synced: keep the plain name without the
    // misleading "RCT-" prefix.
    snprintf(s_path, sizeof(s_path), "/hist/%s.csv", key);
  }
  strlcpy(s_pathKey, key, sizeof(s_pathKey));
  s_pathValid = true;
  // A change of the file name is worth a line of its own. The usual reason is the
  // clock arriving a few seconds after the mount, which turns the provisional
  // "UPT-<days>.csv" into the month file - and a card holding both names looks
  // like a leftover from something else unless the log says where it came from.
  if (hadPath) {
    Serial.printf("SD: Datei %s (Uhr jetzt %s)\n", s_path,
                  haveClock ? "gueltig" : "noch nicht gueltig");
  }
  return true;
}

// --------------------------------------------------------------------------
// Mount self-test
//
// Writes /hist/PROBE and reads it back, comparing it with the pattern it just
// wrote. The read-back is the point: a write-only probe proves nothing about a
// raised SPI clock, because what breaks at high speed is the data coming back
// (bit errors, CRC mismatches) - exactly what a raw write never looks at. The
// round trip also yields the throughput, which is the number that says whether
// the fast clock is really worth keeping.
//
// 512 bytes: enough for a throughput figure that means something, small enough
// that a failing card fails in milliseconds instead of after a second of
// retries. Both buffers live here, not on the worker's 4 kB stack, and only
// this task ever touches them.
//
// The marker is not deleted here: it is removed after the first successful
// flush (see sdWorkerWriteRow), so "PROBE still exists" keeps meaning what it
// always meant - the write path is not proven yet.
// --------------------------------------------------------------------------
constexpr size_t kProbeBytes = 512;
uint8_t s_probeTx[kProbeBytes];
uint8_t s_probeRx[kProbeBytes];

static uint8_t probePattern(size_t i) { return (uint8_t)(i * 37 + 11); }

// Round-trip probe. kbsOut receives the measured round-trip throughput in
// kB/s (payload bytes both ways divided by the elapsed time).
bool probeMount(float *kbsOut) {
  for (size_t i = 0; i < kProbeBytes; i++) {
    s_probeTx[i] = probePattern(i);
  }
  const int64_t t0 = esp_timer_get_time();
  {
    File probe = SD.open("/hist/PROBE", FILE_WRITE);
    if (!probe) {
      Serial.println(F("SD: could not write /hist/PROBE (self-test failed)"));
      return false;
    }
    const size_t wrote = probe.write(s_probeTx, kProbeBytes);
    probe.flush();
    probe.close();
    if (wrote != kProbeBytes) {
      Serial.printf("SD: self-test short write %u/%u\n", (unsigned)wrote,
                    (unsigned)kProbeBytes);
      return false;
    }
  }
  {
    File in = SD.open("/hist/PROBE", FILE_READ);
    if (!in) {
      Serial.println(F("SD: could not read /hist/PROBE (self-test failed)"));
      return false;
    }
    const size_t got = in.read(s_probeRx, kProbeBytes);
    in.close();
    if (got != kProbeBytes) {
      Serial.printf("SD: self-test short read %u/%u\n", (unsigned)got,
                    (unsigned)kProbeBytes);
      return false;
    }
  }
  const int64_t dt = esp_timer_get_time() - t0;
  if (memcmp(s_probeTx, s_probeRx, kProbeBytes) != 0) {
    Serial.println(F("SD: self-test data mismatch - card cannot hold this clock"));
    return false;
  }
  if (kbsOut != nullptr) {
    // Both directions of the round trip count as payload here: the figure is
    // the card's throughput, not the file system's. esp_timer_get_time() counts
    // microseconds, so the conversion is bytes * 1000 / dt - without that the
    // result is a thousand times too small and every card reads as "0 kB/s".
    *kbsOut = dt > 0 ? (float)(2.0 * (double)kProbeBytes * 1000.0 / (double)dt)
                    : 0.0f;
  }
  return true;
}

// One mount attempt at a given clock: bus, mount, /hist, self-test. Leaves the
// card unmounted on any failure so the next attempt starts from a clean bus.
static bool tryMount(uint32_t hz, float *kbsOut, bool *probeOkOut) {
  s_spi.begin(kSpiSck, kSpiMiso, kSpiMosi, kSpiSs);
  if (!SD.begin(kSpiSs, s_spi, hz, "/sd", 4)) {
    SD.end(); // leave the bus clean for the next attempt
    return false;
  }
  // mkdir() reports false for an existing directory, so ask first - the old
  // "cannot create /hist" message was printed on every normal mount.
  if (!SD.exists("/hist") && !SD.mkdir("/hist")) {
    Serial.println(F("SD: cannot create /hist"));
  }
  const bool probeOk = probeMount(kbsOut);
  if (probeOkOut != nullptr) {
    *probeOkOut = probeOk;
  }
  if (!probeOk) {
    SD.end();
    return false;
  }
  return true;
}

// Append one formatted row. Returns 1 when it was written as the first row of
// a fresh file, 0 when it was appended, and -1 on any failure (card gone, card
// full, short write).
//
// The result is taken from the return value of println(), not from
// getWriteError(): the ESP32 core's FS write path never sets that flag, it just
// returns the byte count it managed to write. A short write would leave the row
// re-queued and possibly logged twice, but a short write on a 512-byte-sector
// card means the card is failing anyway.
int appendRow(const char *path, const char *line) {
  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    return -1;
  }
  const bool freshFile = (f.size() == 0);
  if (freshFile) {
    f.println(csvrow::kHeader); // fresh file (new month / first ever)
  }
  const size_t want = strlen(line) + 2; // trailing "\r\n"
  const size_t got = f.println(line);
  f.flush();
  f.close();
  return got >= want ? (freshFile ? 1 : 0) : -1;
}

// The card as a sink for rowq::Queue::flushGrouped() - the only place in the
// project that writes a parked row, so the ordering and the month split live
// in the tested header and this stays a thin adapter.
//
// Consecutive rows of one file share a single open: after a 24 h outage that is
// 1 open instead of 288, which matters because each open is a directory lookup
// plus a sector read on a card that runs at 4 MHz.
struct CardSink {
  File f;
  bool open(const char *path) {
    f = SD.open(path, FILE_APPEND);
    if (!f) {
      return false;
    }
    if (f.size() == 0) {
      f.println(csvrow::kHeader); // fresh file (new month / first ever)
    }
    return true;
  }
  // The result is taken from the return value of println(), not from
  // getWriteError(): the ESP32 core's FS write path never sets that flag, it
  // just returns the byte count it managed to write. A short write leaves the
  // row parked and possibly logged twice, but a short write on a
  // 512-byte-sector card means the card is failing anyway.
  bool append(const char *line) { return f.println(line) >= strlen(line) + 2; }
  void close() {
    f.flush();
    f.close();
  }
};

// Retry parked rows. Stops at the first failure and keeps the rest, so the rows
// stay in chronological order inside the file.
void queueFlush() {
  CardSink sink;
  const int written = s_queue.flushGrouped(sink);
  if (written > 0) {
    Serial.printf("SD: %d gepufferte Zeile(n) nachgeschrieben\n", written);
    buildStatus();
  }
}

// "5 min", "25 min", "1 h", "24 h" - how far back the oldest parked row
// reaches. Better than the raw row count: 288 rows means nothing without the
// 5-minute cadence, and the question in the manual is "how long can the card be
// away".
void queueSpan(int rows, char *out, size_t cap) {
  const int minutes = rows * 5; // one row per log interval
  if (minutes < 60) {
    snprintf(out, cap, "%d min", minutes);
  } else {
    snprintf(out, cap, "%d h", minutes / 60);
  }
}

void buildStatus() {
  // Reads the card geometry (SD.totalBytes/usedBytes) whenever the status line
  // is refreshed, and that also happens on the GUI task - so this is card I/O
  // outside the worker, and it gets a name of its own.
  diagPhase("sd.status");
  char buf[64];
  char span[12];
  const int parked = s_queue.count();
  if (parked > 0) {
    queueSpan(parked, span, sizeof(span));
  }
  if (!s_mounted) {
    if (parked > 0) {
      snprintf(buf, sizeof(buf), "SD: -- | %d gepuffert (%s)", parked, span);
    } else {
      snprintf(buf, sizeof(buf), "SD: --");
    }
  } else if (s_queue.dropped() > 0) {
    // Lost rows outrank the free space: that is the number that matters.
    snprintf(buf, sizeof(buf), "SD: OK | %u %s verloren",
             (unsigned)s_queue.dropped(),
             s_queue.dropped() == 1 ? "Zeile" : "Zeilen");
  } else if (parked > 0) {
    snprintf(buf, sizeof(buf), "SD: OK | %d gepuffert (%s) | %.1f GB frei",
             parked, span,
             (float)(SD.totalBytes() - SD.usedBytes()) / 1.0e9f);
  } else {
    snprintf(buf, sizeof(buf), "SD: OK | %.1f GB frei",
             (float)(SD.totalBytes() - SD.usedBytes()) / 1.0e9f);
  }
  char *dot = strchr(buf, '.');
  if (dot != nullptr) {
    *dot = ','; // German decimal comma
  }
  strlcpy(s_status, buf, sizeof(s_status));
}

// --------------------------------------------------------------------------
// History restore (24 h "Verlauf" page): read the newest CSV rows back into
// the chart. Rows are appended in chronological order, so a forward scan plus
// a rolling window of maxRows keeps the newest rows without huge buffers.
// --------------------------------------------------------------------------

static bool parseLine(const char *line, SdHistSample *s) {
  csvrow::Row r;
  if (!csvrow::parse(line, r)) {
    return false;
  }
  csvrow::toSample(r, *s);
  return true;
}

// Rows are ~102 bytes (16 columns, see kLineCap), so this is how many bytes of
// history are needed to cover ringCap rows with room to spare. Used to skip
// forward to the interesting part of the file instead of parsing all of it.
constexpr size_t kRowBytesEst = 128;

// Scan rows of `path` into `ring` (raw ring layout, one slot per parsed row
// modulo ringCap). Return the number of parsed rows; the newest
// min(total, ringCap) rows survive in the ring.
//
// Reads only the newest ringCap rows, by seeking that far from the end of the
// file and parsing forward from there. Bounding the read is the whole point of
// this function: the log is appended to every 5 minutes, so a month file grows
// by ~8 600 rows, and parsing it from the front meant reading the entire file
// over the SPI bus. At the 400 kHz this started out with, that scan does not
// finish in any useful time - it held the card worker, and behind it the GUI
// waiting for the history result, for minutes. Measured on 2025-09-29: still
// running after 120 s. Bounding the read is what fixed it, and the higher clock
// makes the window cheap as well.
//
// The window is sized generously (kRowBytesEst per row rather than the exact
// row length), so it can begin slightly before the rows that matter. Extra rows
// at the front of the window lose the ring modulo to newer ones, which is the
// same behaviour as before and needs no special case.
static int scanFile(const char *path, SdHistSample *ring, int ringCap) {
  if (ringCap <= 0) {
    return 0;
  }
  File f = SD.open(path, FILE_READ);
  if (!f) {
    return 0;
  }

  const size_t want = (size_t)ringCap * kRowBytesEst;
  const size_t fileSize = f.size();
  const size_t from = fileSize > want ? fileSize - want : 0;
  f.seek(from);

  // One static block, no allocation. The old loop built a String for every row
  // of the whole file; this reuses 8 kB and parses in place.
  static char block[kRowBytesEst * 64];
  int total = 0;
  size_t carry = 0;   // bytes of a line split across the block boundary
  bool atBoundary = from == 0; // only line-skip when we started mid-file

  const uint32_t scanT0 = millis();
  while (true) {
    const size_t got = f.read((uint8_t *)block + carry, sizeof(block) - carry);
    if (got == 0) {
      break;
    }
    const size_t avail = carry + got;

    // Start on a line boundary. When we seeked into the middle of the file, the
    // first line in the block is a row's tail, not a row.
    size_t lineStart = 0;
    if (!atBoundary) {
      while (lineStart < avail && block[lineStart] != '\n') {
        lineStart++;
      }
      if (lineStart >= avail) {
        // Not one line end in a whole block: a row longer than the block, which
        // kLineCap rules out. Stop rather than shift the buffer forever.
        break;
      }
      lineStart++;
      atBoundary = true;
    }

    while (lineStart < avail) {
      const char *nl =
          (const char *)memchr(block + lineStart, '\n', avail - lineStart);
      if (nl == nullptr) {
        break; // unfinished line: carried over to the next block
      }
      const size_t lineEnd = (size_t)(nl - block);
      size_t len = lineEnd - lineStart;
      char *line = block + lineStart;
      if (len > 0 && line[len - 1] == '\r') {
        len--; // CRLF, as written by Print.println()
      }
      line[len] = '\0';

      if (len >= 12 && strncmp(line, "ts,", 3) != 0) {
        SdHistSample smp;
        if (parseLine(line, &smp)) {
          ring[total % ringCap] = smp;
          total++;
        }
      }
      lineStart = lineEnd + 1;
    }

    // Keep the unfinished tail for the next block.
    carry = avail - lineStart;
    if (carry > 0) {
      memmove(block, block + lineStart, carry);
    }
  }
  f.close();
  if (fileSize > want) {
    Serial.printf("SD: history scan %u B -> %u B, %d Zeilen, %lu ms\n",
                  (unsigned)fileSize, (unsigned)from, total,
                  (unsigned long)(millis() - scanT0));
  }
  return total;
}

// Move the newest min(total, cap) rows from the ring to dst, chronological.
static void drainRing(const SdHistSample *ring, int total, int cap,
                      SdHistSample *dst) {
  int kept = total > cap ? cap : total;
  int start = total - kept;
  for (int i = 0; i < kept; i++) {
    dst[i] = ring[(start + i) % cap];
  }
}

// "202608" for "202609" (wraps the year), nullptr when not derivable.
static const char *prevMonthKey(char *buf, size_t len) {
  const char *key = monthKey();
  int y = 0, m = 0;
  if (key == nullptr || sscanf(key, "%4d%2d", &y, &m) != 2) {
    return nullptr;
  }
  if (m == 1) {
    y--;
    m = 12;
  } else {
    m--;
  }
  snprintf(buf, len, "%04d%02d", y, m);
  return buf;
}

} // namespace

// Periodic card duties, owned by the worker: mount retry, presence watch and
// flushing parked rows. Formerly sdTick()'s body.
static void sdWorkerPeriodic(uint32_t now) {
  if (s_mounted) {
    // A card pulled out in operation is invisible until something writes and
    // that write fails, which can be up to 5 minutes away. Reading the card
    // size instead detects removal within seconds and starts a fresh PROBE on
    // re-insert, without a remount in between.
    if ((int32_t)(now - s_nextProbeMs) >= 0) {
      s_nextProbeMs = now + kProbeMs;
      if (SD.cardSize() == 0) {
        if (s_warnLogged) { // only announce once per disappearance
          Serial.println("SD: card no longer present");
        }
        s_warnLogged = true;
        s_mounted = false;
        SD.end();
        s_pathValid = false;
        s_probePending = true;
        buildStatus();
      } else if (s_probePending && !s_warnLogged) {
        // A card is back. Do NOT re-write the PROBE marker here: delete+create
        // is FAT metadata traffic, and it measured 1971 ms on the 400 kHz bus
        // this panel started with (a few hundred ms at 4 MHz - still a long time
        // to hold the worker, for no information this block does not already
        // have).
        // Card presence is what this block is for, and it is answered by
        // cardSize(). If the inserted card is a different one, the next real
        // row write fails through appendRow() and parks the row in the RAM
        // queue - no marker needed.
        s_warnLogged = false; // back to normal, failures may warn again
      }
    }
    // Retry parked rows in between: a card that comes back or frees up should
    // not have to wait for the next 5-minute sample. Throttled, so a card that
    // is still missing is not hammered every pass.
    if (!s_queue.empty() && (int32_t)(now - s_nextQueueRetryMs) >= 0) {
      s_nextQueueRetryMs = now + kQueueRetryMs;
      queueFlush();
    }
    return;
  }
  if ((int32_t)(now - s_nextMountMs) < 0) {
    return;
  }
  s_nextMountMs = now + kMountRetryMs;

  // SD.begin() is the longest single blocking card operation in the project
  // (measured 1457 ms). It runs on this task, not on the GUI task, but the GUI
  // then blocks on s_lock in the status call, so it has to be visible.
  diagPhase("sd.mount");
  // Fast clock first (4 MHz, the library default), fall back to the 400 kHz
  // that worked before. The probe in tryMount() is what decides, and its
  // verdict sticks for the rest of the session: a card that cannot hold 4 MHz
  // must not be offered 4 MHz again every 10 s.
  uint32_t hz = s_fastUsable == 0 ? kSdSlowHz : kSdFastHz;
  float kbs = 0.0f;
  bool probeOk = false;
  if (!tryMount(hz, &kbs, &probeOk)) {
    if (hz == kSdSlowHz) {
      return; // already at the safe clock, nothing to fall back to
    }
    Serial.printf("SD: %lu Hz unbrauchbar, weiche auf %lu Hz zurueck\n",
                  (unsigned long)hz, (unsigned long)kSdSlowHz);
    s_fastUsable = 0;
    hz = kSdSlowHz;
    if (!tryMount(hz, &kbs, &probeOk)) {
      return;
    }
  } else if (s_fastUsable < 0) {
    s_fastUsable = 1;
  }
  s_spiHz = hz;
  s_mounted = true;
  s_warnLogged = false;
  s_pathValid = false;
  s_probePending = probeOk;
  updatePath(); // fill s_path, otherwise the log line below stays empty
  buildStatus();
  Serial.printf("SD: mounted at %lu Hz, self-test %.1f kB/s, %.1f GB free (%s)\n",
                (unsigned long)hz, (double)kbs,
                (double)(SD.totalBytes() - SD.usedBytes()) / 1.0e9, s_path);
  if (!s_queue.empty()) {
    s_nextQueueRetryMs = millis(); // flush parked rows right away
    queueFlush();
  }
  // Fill both listing caches now that there is something to list. Doing this in
  // sdInit() instead would queue the request before the mount and cache the
  // "no card" answer, and the first page view would come up empty until it was
  // asked a second time.
  sdRequestListing("/hist");
  sdRequestListing("/shot");
}

// Worker side of SDREQ_LOG: actually touch the card, or park the row.
static void sdWorkerWriteRow(const SdReq &req) {
  if (s_mounted) {
    diagPhase("sd.append");
    const int r = appendRow(req.path, req.line);
    if (r >= 0) {
      Serial.printf("SD: %s row -> %s\n", r > 0 ? "header + first" : "logged",
                    req.path);
      if (s_probePending) {
        // First successful flush: the self-test marker may go away now.
        if (SD.remove("/hist/PROBE")) {
          s_probePending = false;
          Serial.println("SD: PROBE self-test ok (marker removed)");
        } else {
          Serial.println(F("SD: PROBE marker could not be removed"));
        }
      }
      buildStatus();
      return;
    }
    // The write failed. Treat the card as gone so the worker goes looking for
    // it again - that is how a card pulled out during operation gets noticed.
    if (!s_warnLogged) {
      s_warnLogged = true;
      Serial.printf("SD: write to %s failed, card treated as gone\n", req.path);
    }
    s_mounted = false;
    SD.end();
    s_pathValid = false;
    s_probePending = true;
  }

  s_queue.push(req.line, req.path);
  buildStatus();
}

void sdLogSample(const RctSnapshot &s) {
  if (!s.haveData) {
    return; // no zero rows for a disconnected inverter
  }
  if (!updatePath()) {
    return;
  }

  // Formatting stays here: it is pure computation (microseconds) and needs the
  // snapshot, which the worker never sees. Only the card access is handed over,
  // so a slow card can no longer stall the GUI. The row itself is built by
  // storage/CsvRow.h - the same code the history scan below reads it back with.
  SdReq req;
  req.type = SDREQ_LOG;
  req.waitForClock = false;
  strlcpy(req.path, s_path, sizeof(req.path));
  csvrow::Row row;
  row.ts = (uint32_t)time(nullptr);
  row.faults = (uint32_t)(s.faultBits[0] | s.faultBits[1] | s.faultBits[2] |
                         s.faultBits[3]);
  row.pvA = s.pvPower[0];
  row.pvB = s.pvPower[1];
  row.s0 = s.s0Power;
  row.tc = s.coreTemp;
  row.tb = s.batteryTemp;
  row.th = s.heatSinkTemp;
  row.l1 = s.loadPower[0];
  row.l2 = s.loadPower[1];
  row.l3 = s.loadPower[2];
  row.bat = s.batteryPower;
  row.soc = s.batterySoc;
  row.g1 = s.gridPower[0];
  row.g2 = s.gridPower[1];
  row.g3 = s.gridPower[2];
  if (csvrow::format(req.line, sizeof(req.line), row) == 0) {
    // Not reachable with kLineCap as sized - the host test formats the worst
    // case it can build and checks the margin - but a truncated line would be
    // unparsable, so the row is dropped loudly instead of written.
    Serial.println(F("SD: Zeile zu lang, verworfen"));
    return;
  }

  if (s_reqQ == nullptr || xQueueSend(s_reqQ, &req, 0) != pdTRUE) {
    // Only reachable if the worker is wedged or not started yet; count the loss
    // rather than dropping it silently.
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_queue.noteDropped();
    buildStatus();
    xSemaphoreGive(s_lock);
    Serial.println(F("SD: worker unreachable, row dropped"));
  }
}

const char *sdStatusText() {
  // Copied out under the lock: the worker rewrites s_status from its own task.
  // Both readers live on the GUI task, so one scratch buffer is enough.
  static char buf[64];
  if (s_lock != nullptr) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(buf, s_status, sizeof(buf));
    xSemaphoreGive(s_lock);
  } else {
    strlcpy(buf, s_status, sizeof(buf));
  }
  return buf;
}


uint32_t sdSpiHz() { return s_mounted ? s_spiHz : 0; }

bool sdMounted() { return s_mounted; }

static void sdWorkerWriteShot(const SdReq &req) {
  // 24-bit BMP: no compression, no palette, bottom-up. Chosen over PNG because
  // it needs no encoder on the device - the panel has no room for one, and this
  // is a debug image that any viewer opens.
  //
  // Rows must start on a 4-byte boundary. At 480 px wide a row is 1440 bytes
  // and already aligned, but the padding is computed rather than assumed: the
  // next screenshot would be silently skewed at a width that is not.
  const int w = req.w;
  const int h = req.h;
  const size_t rowBytes = ((size_t)w * 3u + 3u) & ~(size_t)3u;
  const size_t imgBytes = rowBytes * (size_t)h;

  diagPhase("sd.shot");
  if (!s_mounted) {
    Serial.println(F("SD: Screenshot verworfen, keine Karte"));
    s_shotDone = true;
    return;
  }
  if (w <= 0 || h <= 0 || w > kShotMaxW || req.px == nullptr) {
    Serial.printf("SD: Screenshot verworfen, unbrauchbare Groesse %dx%d\n", w, h);
    s_shotDone = true;
    return;
  }
  SD.mkdir("/shot");

  // Next free name, so several shots in a row are all still there afterwards.
  char path[kPathCap];
  unsigned n = 1;
  do {
    snprintf(path, sizeof(path), "/shot/shot%03u.bmp", n);
    n++;
  } while (n <= 999 && SD.exists(path));

  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("SD: Screenshot nicht schreibbar (%s)\n", path);
    s_shotDone = true;
    return;
  }

  uint8_t hdr[54] = {0};
  hdr[0] = 'B';
  hdr[1] = 'M';
  const uint32_t fileBytes = (uint32_t)(sizeof(hdr) + imgBytes);
  memcpy(hdr + 2, &fileBytes, 4);
  const uint32_t dataOff = (uint32_t)sizeof(hdr);
  memcpy(hdr + 10, &dataOff, 4);
  const uint32_t hdrSize = 40;
  memcpy(hdr + 14, &hdrSize, 4);
  memcpy(hdr + 18, &w, 4);
  memcpy(hdr + 22, &h, 4); // positive height = rows stored bottom-up
  const uint16_t planes = 1, bpp = 24;
  memcpy(hdr + 26, &planes, 2);
  memcpy(hdr + 28, &bpp, 2);
  memcpy(hdr + 34, &imgBytes, 4);
  const uint32_t ppm = 2835; // 72 dpi, what Windows writes for a screen grab
  memcpy(hdr + 38, &ppm, 4);
  memcpy(hdr + 42, &ppm, 4);
  f.write(hdr, sizeof(hdr));

  // Bottom-up: BMP starts at the last scanline. A 480x480 BMP is 691 254 bytes:
  // ~1.4 s at the 4 MHz clock the card normally runs at, >14 s at the 400 kHz
  // fallback - and the 5 s task-watchdog window is shorter than the second case.
  // Feed the WDT from this task and let IDLE0 run every row, so a card that fell
  // back to 400 kHz still writes its picture completely. A transfer that is
  // truly stuck still trips the watchdog: this only legalises writes that are
  // slow, not ones that hang.
  for (int y = h - 1; y >= 0; y--) {
    const uint16_t *src = req.px + (size_t)y * (size_t)w;
    size_t n = 0;
    for (int x = 0; x < w; x++) {
      const uint16_t p = src[x];
      s_shotRow[n++] = (uint8_t)((p & 0x1F) * 255u / 31u);       // B
      s_shotRow[n++] = (uint8_t)(((p >> 5) & 0x3F) * 255u / 63u); // G
      s_shotRow[n++] = (uint8_t)(((p >> 11) & 0x1F) * 255u / 31u); // R
    }
    while (n < rowBytes) {
      s_shotRow[n++] = 0; // padding, only needed if w*3 is not 4-aligned
    }
    f.write(s_shotRow, rowBytes);
    esp_task_wdt_reset();
    vTaskDelay(1); // give IDLE0 a slice so its own WDT stays fed
  }
  f.close();

  Serial.printf("SD: Screenshot -> %s (%dx%d, %lu Bytes)\n", path, w, h,
                (unsigned long)fileBytes);
  s_shotDone = true;
}

// Worker side of SDREQ_HISTORY. Formerly sdReadHistory(), which filled a
// caller-owned buffer on the GUI task and therefore blocked it for the whole
// scan (measured 1418 ms). Now the scan happens here and hands the rows over
// through s_histOut; the GUI collects them whenever it is ready.
void sdWorkerReadHistory(int maxRows, bool waitForClock) {
  // scanFile() walks CSV files backwards over the SPI bus: still the second-longest
  // card operation, and the one the chart on the panel waits on.
  diagPhase("sd.scan");
  int count = 0;
  bool deferred = false;

  if (s_mounted) {
    const char *key = monthKey();
    if (key == nullptr && waitForClock) {
      // Card is there, but SNTP has not delivered a time yet: the monthly log
      // file cannot be named, and the pre-SNTP uptime file is the wrong one
      // (the writer switches to the month file as soon as the clock is up).
      // The caller retries; waitForClock=false takes what is there.
      deferred = true;
    } else {
      static SdHistSample ring[kHistMaxRows];
      static SdHistSample prevRing[kHistMaxRows];

      char path[40];
      const char *prevKey = nullptr;
      static char prevKeyBuf[16];
      if (key != nullptr) {
        snprintf(path, sizeof(path), "/hist/RCT-%s.csv", key);
        prevKey = prevMonthKey(prevKeyBuf, sizeof(prevKeyBuf));
      } else {
        snprintf(path, sizeof(path), "/hist/%s.csv", uptimeKey());
      }

      char prevPath[40];
      prevPath[0] = '\0';
      if (prevKey != nullptr) {
        snprintf(prevPath, sizeof(prevPath), "/hist/RCT-%s.csv", prevKey);
      }

      const int cur = scanFile(path, ring, maxRows);
      if (cur > 0) {
        if (cur >= maxRows || prevPath[0] == '\0') {
          drainRing(ring, cur, cur > maxRows ? maxRows : cur, s_histOut);
          count = cur > maxRows ? maxRows : cur;
        } else {
          // Current file alone has fewer than maxRows rows - the 24 h window
          // reaches across a calendar boundary. Prepend the previous month's
          // newest rows.
          const int need = maxRows - cur;
          const int prev = scanFile(prevPath, prevRing, need);
          if (prev <= 0) {
            drainRing(ring, cur, cur, s_histOut);
            count = cur;
          } else {
            const int prevKept = prev > need ? need : prev;
            drainRing(prevRing, prev, need, s_histOut);
            drainRing(ring, cur, cur, s_histOut + prevKept);
            count = prevKept + cur;
          }
        }
      } else if (prevPath[0] != '\0') {
        // Early in a new month nothing is logged into its file yet, but the
        // last 24 h are all in the previous month's file.
        const int prev = scanFile(prevPath, ring, maxRows);
        if (prev > 0) {
          drainRing(ring, prev, prev > maxRows ? maxRows : prev, s_histOut);
          count = prev > maxRows ? maxRows : prev;
        }
      }
    }
  }

  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_histCount = count;
  s_histDeferred = deferred;
  s_histReady = 1;
  xSemaphoreGive(s_lock);
}

void sdRequestHistory(int maxRows, bool waitForClock) {
  if (s_reqQ == nullptr || maxRows <= 0 || maxRows > kHistMaxRows) {
    return;
  }
  SdReq req;
  memset(&req, 0, sizeof(req));
  req.type = SDREQ_HISTORY;
  req.maxRows = maxRows;
  req.waitForClock = waitForClock;
  xQueueSend(s_reqQ, &req, 0);
}

// Collect a finished history scan. Returns the row count (>= 0) once the
// worker is done, -2 while the scan is still running or was never requested,
// and -1 when the worker deferred because the clock was not up yet.
int sdTakeHistory(SdHistSample *out, int maxRows) {
  if (out == nullptr || s_lock == nullptr) {
    return -2;
  }
  int rc = -2;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  if (s_histReady) {
    if (s_histDeferred) {
      rc = -1;
    } else {
      const int n = s_histCount < maxRows ? s_histCount : maxRows;
      memcpy(out, s_histOut, (size_t)n * sizeof(SdHistSample));
      rc = n;
    }
    s_histReady = 0;
  }
  xSemaphoreGive(s_lock);
  return rc;
}

// --------------------------------------------------------------------------
// File streaming and directory listing (web interface, see docs/web-interface.md)
//
// The card stays in this worker's hands, same rule as everywhere else in this
// file: the GUI asks for a chunk and copies bytes out of a buffer below, it
// never touches a File itself. The web server cannot read the card in the same
// breath as it writes to a socket - that is what the worker is for.
//
// One chunk per worker pass, and only when the previous chunk has been picked
// up. So a queued 5-minute row or a card-presence check waits at most one
// chunk instead of a whole file, and a browser that stops reading stops the
// card (the buffer simply stays full) - no unbounded buffering.
//
// A stream announces its byte count up front (whole file, or the last N kB of
// it), which is what lets the web server send a real Content-Length and the
// browser show a true progress bar.
// --------------------------------------------------------------------------

// 16 kB: at 4 MHz a chunk takes ~40 ms, so the worker's 10 ms idle pass is
// noise; at the 400 kHz fallback it is ~380 ms and still harmless. 8 kB was
// measurably more per-pass overhead for no gain in responsiveness.
constexpr size_t kStreamChunk = 16384;
// PSRAM first: this buffer is touched once per GUI loop while a stream runs,
// and internal heap is the scarce resource on this panel (~150 kB).
uint8_t *s_streamBuf = nullptr;
// 2 kB of listing: a line is ~34 bytes ("shot042.bmp|691024|1758901200"), so this
// holds ~60 entries. A full card of screenshots (999 by the naming scheme)
// exceeds it - the page says so rather than pretending the list is complete.
constexpr size_t kListingCap = 2048;
// One slot per directory the web interface lists (/hist and /shot).
constexpr int kListCacheCount = 2;
// How long a listing is served as it is before the worker reads the card again.
constexpr uint32_t kListingMaxAgeMs = 5000;

struct StreamState {
  bool active = false;  // worker holds an open file
  bool ready = false;   // a filled chunk is waiting to be picked up
  bool done = false;    // everything announced has been handed over (or failed)
  bool failed = false;
  bool stop = false;    // abort asked for by the caller
  size_t total = 0;     // bytes the caller will receive in total
  size_t sent = 0;      // bytes handed over so far
  size_t have = 0;      // bytes in the buffer not yet taken
};
StreamState s_stream;
File s_streamFile; // worker-only: opened by SDREQ_STREAM, closed on done/abort

// One cached directory listing per directory the web interface shows. The worker
// fills it, the web task only reads it: the card is the worker's alone, and a
// request is answered in the same pass it arrives in. Guarded by s_lock.
struct ListCache {
  char path[16];
  char *text;
  int len;       // >= 0 filled, -1 not filled yet, -2 read failed
  uint32_t atMs; // when it was filled
};
ListCache s_cache[kListCacheCount];
char *s_listPool = nullptr;

ListCache *cacheFor(const char *path) {
  for (int i = 0; i < kListCacheCount; i++) {
    if (s_cache[i].path[0] != '\0' && strcmp(s_cache[i].path, path) == 0) {
      return &s_cache[i];
    }
  }
  for (int i = 0; i < kListCacheCount; i++) {
    if (s_cache[i].path[0] == '\0') {
      strlcpy(s_cache[i].path, path, sizeof(s_cache[i].path));
      return &s_cache[i];
    }
  }
  return nullptr; // both slots taken by other directories
}

// Worker side of SDREQ_STREAM: open, position, announce.
static void sdWorkerOpenStream(const SdReq &req) {
  if (s_stream.active) {
    s_streamFile.close();
  }
  s_stream.active = false;
  s_stream.failed = false;
  s_stream.stop = false;
  s_stream.sent = 0;
  s_stream.have = 0;
  s_stream.total = 0;
  if (!s_mounted) {
    s_stream.failed = true;
    s_stream.done = true;
    return;
  }
  diagPhase("sd.open");
  File f = SD.open(req.path, FILE_READ);
  if (!f) {
    Serial.printf("SD: cannot open %s for streaming\n", req.path);
    s_stream.failed = true;
    s_stream.done = true;
    return;
  }
  const size_t size = (size_t)f.size();
  // Tail request: start that many bytes back from the end, clamped to the file.
  size_t start = 0;
  if (req.tailBytes > 0 && req.tailBytes < size) {
    start = size - req.tailBytes;
  }
  if (start > 0 && !f.seek(start)) {
    Serial.printf("SD: seek %u in %s failed, streaming from the start\n",
                  (unsigned)start, req.path);
    start = 0;
  }
  s_streamFile = f;
  s_stream.total = size - start;
  s_stream.active = true;
  s_stream.done = false;
  // A zero-length file is finished before it starts: otherwise the first
  // take would return 0 (nothing yet) and the caller would wait forever.
  s_stream.done = s_stream.total == 0;
  Serial.printf("SD: stream %s, %u bytes from %u\n", req.path,
                (unsigned)s_stream.total, (unsigned)start);
}

// Worker side of SDREQ_LIST: one directory into its cache, formatted in place.
static void sdWorkerListDir(const SdReq &req) {
  ListCache *c = cacheFor(req.path);
  if (c == nullptr) {
    return;
  }
  if (!s_mounted) {
    c->len = -2;
    c->atMs = millis();
    return;
  }
  diagPhase("sd.listdir");
  xSemaphoreTake(s_lock, portMAX_DELAY);
  size_t used = 0;
  int n = 0;
  File dir = SD.open(req.path);
  if (dir && !dir.isDirectory()) {
    dir.close();
  } else if (!dir) {
    // A missing directory is normal on a fresh card (no /shot yet).
    used = 0;
  }
  if (dir) {
    for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
      if (e.isDirectory()) {
        e.close();
        continue;
      }
      // PROBE is the panel's own self-test marker, not a data file. It has to
      // stay on the card until the first row was written (that is what it
      // proves), so it is filtered out here rather than deleted earlier - a user
      // should not find a download called PROBE next to the month files.
      if (strcmp(e.name(), "PROBE") == 0) {
        e.close();
        continue;
      }
      // name|size|epoch, one per line. The pipe is safe: FAT names cannot
      // contain it, and the web server splits on it without parsing.
      const int w = snprintf(c->text + used, kListingCap - used, "%s|%u|%u\n",
                             e.name(), (unsigned)e.size(),
                             (unsigned long)e.getLastWrite());
      e.close();
      if (w <= 0 || (size_t)w >= kListingCap - used) {
        break; // full or would not fit
      }
      used += (size_t)w;
      n++;
    }
    dir.close();
  }
  // An empty directory is an answer, not a failure: the page says so, and it can
  // do that without the card. -2 stays reserved for "no card or read error".
  c->len = n > 0 ? (int)used : 0;
  c->atMs = millis();
  xSemaphoreGive(s_lock);
}

void sdRequestStream(const char *path, uint32_t tailBytes) {
  if (s_reqQ == nullptr || path == nullptr || s_streamBuf == nullptr) {
    return;
  }
  SdReq req;
  memset(&req, 0, sizeof(req));
  req.type = SDREQ_STREAM;
  req.tailBytes = tailBytes;
  strlcpy(req.path, path, sizeof(req.path));
  if (xQueueSend(s_reqQ, &req, 0) != pdTRUE) {
    // No worker to hand it to: report the stream as finished, so a waiting web
    // client gets an answer instead of hanging.
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_stream.active = false;
    s_stream.done = true;
    s_stream.failed = true;
    xSemaphoreGive(s_lock);
  }
}

// Ask for a fresh listing, but only if the cached one is missing or older than
// kListingMaxAgeMs. A page reload therefore shows a new file within seconds
// without every visit re-reading the directory.
void sdRequestListing(const char *path) {
  if (s_reqQ == nullptr || path == nullptr) {
    return;
  }
  ListCache *c = cacheFor(path);
  if (c != nullptr) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool fresh = c->len >= 0 &&
                       (int32_t)(millis() - c->atMs) < kListingMaxAgeMs;
    xSemaphoreGive(s_lock);
    if (fresh) {
      return;
    }
  }
  SdReq req;
  memset(&req, 0, sizeof(req));
  req.type = SDREQ_LIST;
  strlcpy(req.path, path, sizeof(req.path));
  if (xQueueSend(s_reqQ, &req, 0) != pdTRUE && c != nullptr) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    c->len = -2;
    c->atMs = millis();
    xSemaphoreGive(s_lock);
  }
}

uint32_t sdStreamTotal() {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  const uint32_t t = s_stream.done ? 0 : (uint32_t)s_stream.total;
  xSemaphoreGive(s_lock);
  return t;
}

// Hand out the next piece of the stream. Returns the number of bytes copied,
// 0 when the worker has not filled a chunk yet (ask again next GUI tick), and
// -1 when the stream is finished or failed. Once -1 the caller is done; the
// worker's file is closed by then.
int sdTakeStreamChunk(uint8_t *out, uint32_t max) {
  if (out == nullptr || s_lock == nullptr) {
    return -1;
  }
  int rc = 0;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  if (s_stream.ready && s_stream.have > 0) {
    const uint32_t n = (uint32_t)(s_stream.have < max ? s_stream.have : max);
    memcpy(out, s_streamBuf, n);
    if (n < s_stream.have) {
      // Caller wanted less than the chunk holds: keep the rest for next time.
      memmove(s_streamBuf, s_streamBuf + n, s_stream.have - n);
    }
    s_stream.have -= n;
    s_stream.sent += n;
    if (s_stream.have == 0) {
      s_stream.ready = false;
    }
    rc = (int)n;
  } else if (s_stream.done) {
    rc = -1;
  }
  xSemaphoreGive(s_lock);
  return rc;
}

bool sdStreamFailed() {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  const bool f = s_stream.failed;
  xSemaphoreGive(s_lock);
  return f;
}

void sdStopStream() {
  if (s_lock == nullptr) {
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_stream.stop = true;
  // Give up on the remaining bytes. The worker sees the flag on its next pass
  // and closes the file; a caller that is waiting for a chunk gets -1 and can
  // stop writing immediately.
  s_stream.ready = false;
  s_stream.have = 0;
  s_stream.done = true;
  xSemaphoreGive(s_lock);
}

// Collect a directory listing. Returns the number of bytes written into out
// (lines "name|size|epoch"), -1 while the worker is still busy, and -2 when the
// card is not mounted or the directory is empty.
// The cached listing, copied out. Returns the number of bytes, 0 for an empty
// directory, or kListingUnavailable if the card cannot be read. A cache that is
// still cold answers kListingUnavailable too: it only happens between boot and
// the worker's first pass, and the caller shows the same "SD-Karte liess sich
// nicht lesen" page it would show for a missing card.
int sdListingText(const char *path, char *out, size_t cap) {
  if (out == nullptr || cap == 0 || s_lock == nullptr) {
    return kListingUnavailable;
  }
  ListCache *c = cacheFor(path);
  if (c == nullptr) {
    return kListingUnavailable;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  int rc = kListingUnavailable;
  if (c->len >= 0) {
    const size_t n = (size_t)c->len < cap ? (size_t)c->len : cap;
    if (n > 0) {
      memcpy(out, c->text, n);
    }
    rc = (int)n;
  }
  xSemaphoreGive(s_lock);
  return rc;
}

// Worker side, called once per pass: fill at most one chunk, and only when the
// last one has been taken. Nothing here waits, so the pass stays short.
static void sdWorkerStreamStep() {
  bool openNext = false;
  bool closeNow = false;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  if (s_stream.active && !s_stream.stop && !s_stream.ready &&
      s_stream.have == 0 && !s_stream.done) {
    openNext = true;
  }
  if (s_stream.active && (s_stream.stop || s_stream.done) && s_stream.have == 0) {
    closeNow = true;
  }
  xSemaphoreGive(s_lock);
  if (closeNow) {
    diagPhase("sd.close");
    s_streamFile.close();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_stream.active = false;
    s_stream.ready = false;
    s_stream.have = 0;
    s_stream.done = true;
    xSemaphoreGive(s_lock);
    return;
  }
  if (!openNext) {
    return;
  }
  diagPhase("sd.readchunk");
  const size_t n = s_streamFile.read(s_streamBuf, kStreamChunk);
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_stream.have = n;
  if (n == 0) {
    s_stream.done = true; // clean end of file
  } else {
    s_stream.ready = true;
    if (n < kStreamChunk) {
      s_stream.done = true; // last chunk handed over together with it
    }
  }
  esp_task_wdt_reset();
  xSemaphoreGive(s_lock);
}

// --------------------------------------------------------------------------
// Worker task
// --------------------------------------------------------------------------

static void sdTask(void *) {
  s_nextMountMs = millis() + 500; // first attempt shortly after boot
  for (;;) {
    SdReq req;
    while (s_reqQ != nullptr &&
           xQueueReceive(s_reqQ, &req, 0) == pdTRUE) {
      switch (req.type) {
      case SDREQ_LOG:
        sdWorkerWriteRow(req);
        break;
      case SDREQ_HISTORY:
        sdWorkerReadHistory(req.maxRows, req.waitForClock);
        break;
      case SDREQ_SHOT:
        sdWorkerWriteShot(req);
        break;
      case SDREQ_STREAM:
        sdWorkerOpenStream(req);
        break;
      case SDREQ_LIST:
        sdWorkerListDir(req);
        break;
      default:
        break;
      }
    }
    sdWorkerPeriodic(millis());
    // One stream chunk per pass, so a queued row or a presence check never
    // waits for a whole file. A no-op unless a stream is running.
    sdWorkerStreamStep();
    // Say "alive" for the idle pass. Without this the card worker keeps the
    // phase of its last real job and the heartbeat reports a hang for as long
    // as the worker does nothing - which is most of the time, by design.
    diagBeat();
    // Nothing else to do between requests; the card duties above are already
    // throttled. 10 ms is far finer than any of those intervals.
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void sdScreenshot(const uint16_t *rgb565, int w, int h) {
  if (s_reqQ == nullptr || rgb565 == nullptr || w <= 0 || h <= 0) {
    return;
  }
  // One at a time. A second request while the first is still being converted
  // would be serviced from the same caller's buffer, and the caller only frees
  // that buffer once sdTakeShotDone() says the worker is finished - so the
  // second shot would be written from memory the first one is done with.
  if (!s_shotDone) {
    Serial.println(F("SD: Screenshot verworfen, der vorige laeuft noch"));
    return;
  }
  SdReq req = {};
  req.type = SDREQ_SHOT;
  req.px = rgb565;
  req.w = w;
  req.h = h;
  // Cleared before posting: the worker can pick the request up before this
  // function returns, and the caller must never see "done" for a shot it has
  // not handed over yet.
  s_shotDone = false;
  if (xQueueSend(s_reqQ, &req, 0) != pdTRUE) {
    s_shotDone = true;
    Serial.println(F("SD: Screenshot verworfen, Warteschlange voll"));
  }
}

bool sdTakeShotDone() { return s_shotDone; }

void sdInit() {
  if (s_reqQ != nullptr) {
    return; // already running
  }
  s_reqQ = xQueueCreate(kReqQueueLen, sizeof(SdReq));
  s_lock = xSemaphoreCreateMutex();
  // Parked rows: 24 h in PSRAM, otherwise the 12 slots that are always in
  // internal RAM (one hour). 288 slots * 216 bytes = 62 kB, which would be a
  // third of the free internal heap - the ring is touched once per 5-minute
  // sample, so it has no business living there. Nothing in the ring is a DMA
  // buffer, so plain PSRAM is fine.
  s_queue.attach(s_queueSlots, kQueueCapNoPsram);
  rowq::Slot *bigSlots = (rowq::Slot *)heap_caps_malloc(
      sizeof(rowq::Slot) * kQueueCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (bigSlots != nullptr) {
    s_queue.attach(bigSlots, kQueueCap);
    Serial.printf("SD: Puffer %d Zeilen / 24 h (%u bytes, PSRAM)\n", kQueueCap,
                  (unsigned)(sizeof(rowq::Slot) * kQueueCap));
  } else {
    Serial.printf("SD: Puffer nur %d Zeilen / 1 h (kein PSRAM)\n",
                  kQueueCapNoPsram);
  }
  // Stream buffer: PSRAM if it is there, internal otherwise. Never fail - a
  // missing buffer only costs the web interface its file downloads, and the
  // card logging below must not be conditional on it.
  const size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  s_streamBuf = (uint8_t *)heap_caps_malloc(kStreamChunk,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_streamBuf == nullptr) {
    s_streamBuf = (uint8_t *)malloc(kStreamChunk);
  }
  if (s_streamBuf != nullptr) {
    // Ask the allocator instead of guessing from an address range: only the
    // successful PSRAM attempt reports how much of it was free, which is also
    // the number worth watching if the web interface ever needs trimming.
    Serial.printf("SD: stream buffer %u bytes (PSRAM frei %u bytes)\n",
                  (unsigned)kStreamChunk, (unsigned)psramFree);
  } else {
    Serial.println(F("SD: no stream buffer, web file download disabled"));
  }
  // Listing cache pool: one chunk of kListingCap per cache entry, all in internal
  // RAM (a few kB). The directory text is small (max ~2 kB total) and read by
  // the web task without card access.
  s_listPool = (char *)malloc(kListingCap * kListCacheCount);
  if (s_listPool != nullptr) {
    for (int i = 0; i < kListCacheCount; i++) {
      s_cache[i].text = s_listPool + i * kListingCap;
      s_cache[i].path[0] = '\0';
      s_cache[i].len = -1;
      s_cache[i].atMs = 0;
    }
  } else {
    for (int i = 0; i < kListCacheCount; i++) {
      s_cache[i].text = nullptr;
      s_cache[i].path[0] = '\0';
      s_cache[i].len = -2;
      s_cache[i].atMs = 0;
    }
    Serial.println(F("SD: no listing cache, /daten and /bilder disabled"));
  }
  buildStatus();
  // Priority 1, the same as the Arduino loop task: the card work then shares
  // the CPU with LVGL instead of pre-empting it, and the worker sleeps 10 ms
  // between passes so a long card operation cannot monopolise the core.
  xTaskCreate(sdTask, "sd", 4096, nullptr, 1, nullptr);
}

void sdTick() {
  // All the work moved into the worker task; this stays as the caller's hook
  // so the main loop keeps its shape, but it deliberately does nothing.
}
