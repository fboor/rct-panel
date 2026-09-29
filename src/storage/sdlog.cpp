// SD-card history logging (docs/sd-history.md).
//
// CSV, one file per calendar month on a FAT32 microSD in the TF slot:
//   sdInit()  -> starts the worker task that owns the card
//   sdTick()  -> kicks the worker (non-blocking)
//   sdLogSample() -> queues one row (caller drives the 5-minute cadence)
//
// Why a task: the SPI card talks at 400 kHz and SD.begin() alone measured
// 1457 ms, the history restore 1418 ms. LVGL runs on the same task as this
// file's callers, so a single long card operation froze the whole panel -
// GUI redraw and touch included - for over a second. The worker owns the card
// exclusively (the ESP32 SD/FS layer is not thread-safe) and the GUI thread
// only ever posts messages to it.
//
// SPI wiring on the 4848S040 (cross-checked with a working Tasmota setup):
//   SCK = 48, MOSI = 47 (shared with the boot-only bit-banged LCD config
//   SPI; idle after init), MISO = 41, CS = 42. Uses the FSPI peripheral,
//   blocking, no DMA. ~5 MHz - some UHS-class cards are picky in SPI mode.
//
// SPDX-License-Identifier: MIT
#include "storage/sdlog.h"

#include <Arduino.h>
#include <SD.h>

#include "../Diag.h"
#include <SPI.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

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

char s_path[32];    // "/hist/RCT-202609.csv" or "/hist/UPT-<days>.csv"
char s_pathKey[16]; // rotation key of the currently open file ("202609" ...)
bool s_pathValid = false;
bool s_warnLogged = false; // one-time write-error message per mount

char s_status[48] = "SD: --";

const char *const kCsvHeader =
    "ts,pv_a,pv_b,s0,temp_core,temp_bat,temp_hsink,"
    "load_l1,load_l2,load_l3,bat,soc,grid_l1,grid_l2,grid_l3,status";

// One formatted CSV row. Long enough for the 16 columns with worst-case
// negative values and a full 8-digit fault mask (about 102 characters).
constexpr size_t kLineCap = 176;
constexpr size_t kPathCap = 40;

// --------------------------------------------------------------------------
// Write-failure queue
//
// The card can be pulled out while the panel is running, and a write can fail
// for other reasons too (card full, marginal card). Dropping the row would
// punch a hole in the 24 h chart, so a row that cannot be written right now is
// parked in RAM and retried later.
//
// 12 slots = one hour at the 5-minute cadence. On overflow the oldest row goes,
// because recent data is what the chart needs; how many rows were lost stays
// visible in the status text. A row is stored already formatted rather than as
// a snapshot, so it keeps its original timestamp, and it remembers the path it
// belongs to - a month rollover during the outage then still splits correctly
// across two files.
// --------------------------------------------------------------------------
constexpr int kQueueCap = 12;
constexpr int kHistMaxRows = 288; // 288 * 5 min = 24 h (matches GuiApp)
constexpr uint32_t kQueueRetryMs = 5000; // retry parked rows from sdTick()
constexpr uint32_t kProbeMs = 5000;      // ask SD.cardSize() for card presence

struct QueuedRow {
  char line[kLineCap];
  char path[kPathCap];
};
QueuedRow s_queue[kQueueCap];
int s_queueCount = 0;     // rows parked right now
int s_queueHead = 0;      // ring cursor of the oldest parked row
uint32_t s_queueDropped = 0; // rows lost to overflow since boot
uint32_t s_nextQueueRetryMs = 0;
uint32_t s_nextProbeMs = 0; // next card-presence check

void buildStatus(); // defined below, called by queueFlush()

// --------------------------------------------------------------------------
// Worker task: the only context that ever touches the card.
//
// The GUI thread (Arduino loop + LVGL) posts the three things it needs - write
// a row, read the history, report status - and never waits for them.
// --------------------------------------------------------------------------
enum SdReqType : uint8_t { SDREQ_LOG, SDREQ_HISTORY, SDREQ_SHOT };

struct SdReq {
  uint8_t type;
  int maxRows;       // SDREQ_HISTORY
  bool waitForClock; // SDREQ_HISTORY
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
  if (key == nullptr) {
    key = uptimeKey();
  }
  if (s_pathValid && strcmp(s_pathKey, key) == 0) {
    return true;
  }
  // Paths must be absolute: the VFS layer rejects anything not starting with
  // "/" (the volume is mounted at /sd).
  snprintf(s_path, sizeof(s_path), "/hist/RCT-%s.csv", key);
  if (!s_pathValid && strncmp(key, "UPT-", 4) == 0) {
    // First sample before the clock synced: keep the plain name without the
    // misleading "RCT-" prefix.
    snprintf(s_path, sizeof(s_path), "/hist/%s.csv", key);
  }
  strlcpy(s_pathKey, key, sizeof(s_pathKey));
  s_pathValid = true;
  return true;
}

// Self-test: a marker that only disappears after the first successful flush.
// If it still exists after a power loss, the write path is suspect.
bool probeMount() {
  File probe = SD.open("/hist/PROBE", FILE_WRITE);
  if (!probe) {
    Serial.println(F("SD: could not write /hist/PROBE (self-test failed)"));
    return false;
  }
  probe.println(F("probe"));
  probe.flush();
  probe.close();
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
    f.println(kCsvHeader); // fresh file (new month / first ever)
  }
  const size_t want = strlen(line) + 2; // trailing "\r\n"
  const size_t got = f.println(line);
  f.flush();
  f.close();
  return got >= want ? (freshFile ? 1 : 0) : -1;
}

void queuePush(const char *line, const char *path) {
  if (s_queueCount == kQueueCap) {
    s_queueHead = (s_queueHead + 1) % kQueueCap; // drop the oldest row
    s_queueCount--;
    s_queueDropped++;
  }
  // Next free slot is one past the newest row. When the ring was just full,
  // head has already moved on by one, so this is exactly the freed slot.
  const int tail = (s_queueHead + s_queueCount) % kQueueCap;
  strlcpy(s_queue[tail].line, line, sizeof(s_queue[tail].line));
  strlcpy(s_queue[tail].path, path, sizeof(s_queue[tail].path));
  s_queueCount++;
}

// Retry parked rows, oldest first. Stops at the first failure and keeps the
// rest, so the rows stay in chronological order inside the file.
void queueFlush() {
  int written = 0;
  while (s_queueCount > 0) {
    const QueuedRow &q = s_queue[s_queueHead];
    if (appendRow(q.path, q.line) < 0) {
      break;
    }
    s_queueHead = (s_queueHead + 1) % kQueueCap;
    s_queueCount--;
    written++;
  }
  if (written > 0) {
    Serial.printf("SD: %d gepufferte Zeile(n) nachgeschrieben\n", written);
    buildStatus();
  }
}

void buildStatus() {
  // Reads the card geometry (SD.totalBytes/usedBytes) whenever the status line
  // is refreshed, and that also happens on the GUI task - so this is card I/O
  // outside the worker, and it gets a name of its own.
  diagPhase("sd.status");
  char buf[48];
  if (!s_mounted) {
    if (s_queueCount > 0) {
      snprintf(buf, sizeof(buf), "SD: -- | %d gepuffert", s_queueCount);
    } else {
      snprintf(buf, sizeof(buf), "SD: --");
    }
  } else if (s_queueDropped > 0) {
    // Lost rows outrank the free space: that is the number that matters.
    snprintf(buf, sizeof(buf), "SD: OK | %u %s verloren",
             (unsigned)s_queueDropped,
             s_queueDropped == 1 ? "Zeile" : "Zeilen");
  } else if (s_queueCount > 0) {
    snprintf(buf, sizeof(buf), "SD: OK | %d gepuffert | %.1f GB frei",
             s_queueCount,
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
  unsigned long ts, status;
  float pvA, pvB, s0, tc, tb, th, l1, l2, l3, bat, soc, g1, g2, g3;
  int n = sscanf(line,
                 "%lu,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%lX", &ts,
                 &pvA, &pvB, &s0, &tc, &tb, &th, &l1, &l2, &l3, &bat, &soc,
                 &g1, &g2, &g3, &status);
  if (n != 16) {
    return false;
  }
  s->ts = (uint32_t)ts;
  s->v[0] = g1 + g2 + g3; // Netz
  s->v[1] = l1 + l2 + l3; // Haus
  s->v[2] = pvA + pvB;    // PV A+B
  s->v[3] = s0;           // S0
  s->v[4] = bat;          // Bat
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
// over a 400 kHz SPI bus. That scan does not finish in any useful time - it held
// the card worker, and behind it the GUI waiting for the history result, for
// minutes. Measured on 2025-09-29: still running after 120 s.
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
        // is FAT metadata traffic that measured 1971 ms on this 400 kHz bus.
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
    if (s_queueCount > 0 && (int32_t)(now - s_nextQueueRetryMs) >= 0) {
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
  s_spi.begin(kSpiSck, kSpiMiso, kSpiMosi, kSpiSs);
  // Spec-compliant 400 kHz init: the Arduino SD library clocks the card at the
  // given frequency from CMD0 onwards, and fast init is the classic cause of
  // "physical drive cannot work" with marginal wiring/cards. Plenty fast for
  // one 16-column row per 5 minutes.
  if (!SD.begin(kSpiSs, s_spi, 400000, "/sd", 4)) {
    SD.end(); // leave the bus clean for the next attempt
    return;
  }
  if (!SD.mkdir("/hist")) {
    Serial.println(F("SD: cannot create /hist"));
  }
  s_mounted = true;
  s_warnLogged = false;
  s_pathValid = false;
  s_probePending = probeMount();
  updatePath(); // fill s_path, otherwise the log line below stays empty
  buildStatus();
  Serial.printf("SD: mounted, %.1f GB free (%s)\n",
                (double)(SD.totalBytes() - SD.usedBytes()) / 1.0e9, s_path);
  if (s_queueCount > 0) {
    s_nextQueueRetryMs = millis(); // flush parked rows right away
    queueFlush();
  }
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

  queuePush(req.line, req.path);
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
  // so a slow card can no longer stall the GUI.
  SdReq req;
  req.type = SDREQ_LOG;
  req.waitForClock = false;
  strlcpy(req.path, s_path, sizeof(req.path));
  const unsigned long faults =
      (unsigned long)(s.faultBits[0] | s.faultBits[1] | s.faultBits[2] |
                      s.faultBits[3]);
  snprintf(req.line, sizeof(req.line),
           "%lu,%.0f,%.0f,%.0f,%.1f,%.1f,%.1f,"
           "%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%lX",
           (unsigned long)time(nullptr), s.pvPower[0], s.pvPower[1],
           s.s0Power, (double)s.coreTemp, (double)s.batteryTemp,
           (double)s.heatSinkTemp, (double)s.loadPower[0], (double)s.loadPower[1],
           (double)s.loadPower[2], s.batteryPower, s.batterySoc,
           (double)s.gridPower[0], (double)s.gridPower[1],
           (double)s.gridPower[2], faults);

  if (s_reqQ == nullptr || xQueueSend(s_reqQ, &req, 0) != pdTRUE) {
    // Only reachable if the worker is wedged or not started yet; count the loss
    // rather than dropping it silently.
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_queueDropped++;
    buildStatus();
    xSemaphoreGive(s_lock);
    Serial.println(F("SD: worker unreachable, row dropped"));
  }
}

const char *sdStatusText() {
  // Copied out under the lock: the worker rewrites s_status from its own task.
  // Both readers live on the GUI task, so one scratch buffer is enough.
  static char buf[48];
  if (s_lock != nullptr) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(buf, s_status, sizeof(buf));
    xSemaphoreGive(s_lock);
  } else {
    strlcpy(buf, s_status, sizeof(buf));
  }
  return buf;
}


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

  // Bottom-up: BMP starts at the last scanline. The card runs at 400 kHz
  // (spec-compliant init chosen for this marginal wiring), so a 480x480 BMP
  // needs >14 s of transfer time - longer than the 5 s task-watchdog window.
  // Feed the WDT from this task and let IDLE0 run every row, or the first
  // screenshot after every boot would reboot the panel mid-write and leave
  // a 0-byte file behind. A transfer that is truly stuck still trips the
  // watchdog: this only legalises writes that are slow, not ones that hang.
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
  // scanFile() walks whole CSV files backwards over a 400 kHz bus: the
  // second-longest card operation, and the one the chart on the panel waits on.
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
      default:
        break;
      }
    }
    sdWorkerPeriodic(millis());
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
