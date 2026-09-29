// SD-card history logging (docs/sd-history.md).
//
// CSV, one file per calendar month on a FAT32 microSD in the TF slot:
//   sdInit()  -> first mount attempt shortly after boot
//   sdTick()  -> every loop: mount retry + status text refresh
//   sdLogSample() -> append one row (caller drives the 5-minute cadence)
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
#include <SPI.h>
#include <esp_timer.h>

namespace {

// TF slot pins (see docs/sd-history.md section 1).
constexpr uint8_t kSpiSck = 48;
constexpr uint8_t kSpiMiso = 41;
constexpr uint8_t kSpiMosi = 47;
constexpr uint8_t kSpiSs = 42;

SPIClass s_spi(FSPI);
bool s_mounted = false;
bool s_probePending = true; // PROBE self-test marker exists until first flush
uint32_t s_nextMountMs = 0;
constexpr uint32_t kMountRetryMs = 10000; // retry every 10 s without a card

char s_path[32];    // "/hist/RCT-202609.csv" or "/hist/UPT-<days>.csv"
char s_pathKey[16]; // rotation key of the currently open file ("202609" ...)
bool s_pathValid = false;
bool s_warnLogged = false; // one-time write-error message per mount

char s_status[40] = "SD: --";

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

void buildStatus() {
  if (!s_mounted) {
    strlcpy(s_status, "SD: --", sizeof(s_status));
    return;
  }
  const uint64_t freeB = SD.totalBytes() - SD.usedBytes();
  snprintf(s_status, sizeof(s_status), "SD: OK | %.1f GB frei",
           (float)freeB / 1.0e9f);
  char *dot = strchr(s_status, '.');
  if (dot != nullptr) {
    *dot = ','; // German decimal comma
  }
}

// --------------------------------------------------------------------------
// History restore (24 h "Verlauf" page): read the newest CSV rows back into
// the chart. Rows are appended in chronological order, so a forward scan plus
// a rolling window of maxRows keeps the newest rows without huge buffers.
// --------------------------------------------------------------------------

constexpr int kHistMaxRows = 288; // 288 * 5 min = 24 h (matches GuiApp)

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

// Scan all rows of `path` into `ring` (raw ring layout, one slot per parsed
// row modulo ringCap). Return the total number of parsed rows; the newest
// min(total, ringCap) rows survive in the ring.
static int scanFile(const char *path, SdHistSample *ring, int ringCap) {
  if (ringCap <= 0) {
    return 0;
  }
  File f = SD.open(path, FILE_READ);
  if (!f) {
    return 0;
  }
  int total = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() < 12 || line.startsWith("ts,")) {
      continue; // header or junk
    }
    SdHistSample s;
    if (!parseLine(line.c_str(), &s)) {
      continue;
    }
    ring[total % ringCap] = s;
    total++;
  }
  f.close();
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

void sdInit() {
  // First attempt shortly after boot; sdTick() takes over from there.
  s_nextMountMs = millis() + 500;
  buildStatus();
}

void sdTick() {
  if (s_mounted) {
    return;
  }
  const uint32_t now = millis();
  if ((int32_t)(now - s_nextMountMs) < 0) {
    return;
  }
  s_nextMountMs = now + kMountRetryMs;

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
  buildStatus();
  Serial.printf("SD: mounted, %.1f GB free (%s)\n",
                (double)(SD.totalBytes() - SD.usedBytes()) / 1.0e9, s_path);
}

void sdLogSample(const RctSnapshot &s) {
  if (!s_mounted || !s.haveData) {
    return;
  }
  if (!updatePath()) {
    return;
  }

  const char *header =
      "ts,pv_a,pv_b,s0,temp_core,temp_bat,temp_hsink,"
      "load_l1,load_l2,load_l3,bat,soc,grid_l1,grid_l2,grid_l3,status";
  char line[192];
  const unsigned long faults =
      (unsigned long)(s.faultBits[0] | s.faultBits[1] | s.faultBits[2] |
                      s.faultBits[3]);
  snprintf(line, sizeof(line),
           "%lu,%.0f,%.0f,%.0f,%.1f,%.1f,%.1f,"
           "%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%lX",
           (unsigned long)time(nullptr), s.pvPower[0], s.pvPower[1],
           s.s0Power, (double)s.coreTemp, (double)s.batteryTemp,
           (double)s.heatSinkTemp, (double)s.loadPower[0], (double)s.loadPower[1],
           (double)s.loadPower[2], (double)s.batteryPower, (double)s.batterySoc,
           (double)s.gridPower[0], (double)s.gridPower[1],
           (double)s.gridPower[2], faults);

  File f = SD.open(s_path, FILE_APPEND);
  if (!f) {
    if (!s_warnLogged) {
      s_warnLogged = true;
      Serial.printf("SD: cannot open %s for append\n", s_path);
    }
    return;
  }
  const bool freshFile = (f.size() == 0);
  if (freshFile) {
    f.println(header); // fresh file (new month / first ever)
  }
  f.println(line);
  f.flush();
  f.close();
  Serial.printf("SD: %s row -> %s\n", freshFile ? "header + first" : "logged",
                s_path);

  if (s_probePending) {
    // First successful flush: the self-test marker may go away now.
    if (SD.remove("/hist/PROBE")) {
      s_probePending = false;
      Serial.println("SD: PROBE self-test ok (marker removed)");
    } else {
      Serial.println(F("SD: PROBE marker could not be removed"));
    }
  }
}

const char *sdStatusText() { return s_status; }

bool sdMounted() { return s_mounted; }

int sdReadHistory(SdHistSample *out, int maxRows) {
  if (!s_mounted || out == nullptr || maxRows <= 0 || maxRows > kHistMaxRows) {
    return 0;
  }
  static SdHistSample ring[kHistMaxRows];
  static SdHistSample prevRing[kHistMaxRows];

  char path[40];
  const char *key = monthKey();
  const char *prevKey = nullptr;
  static char prevKeyBuf[16];
  if (key != nullptr) {
    snprintf(path, sizeof(path), "/hist/RCT-%s.csv", key);
    prevKey = prevMonthKey(prevKeyBuf, sizeof(prevKeyBuf));
  } else {
    snprintf(path, sizeof(path), "/hist/%s.csv", uptimeKey());
  }

  int cur = scanFile(path, ring, maxRows);
  if (cur == 0) {
    return 0;
  }
  if (cur >= maxRows) {
    drainRing(ring, cur, maxRows, out);
    return maxRows;
  }

  // Current file alone has fewer than maxRows rows - the 24 h window reaches
  // across a calendar boundary. Prepend the previous month's newest rows.
  int total = cur;
  if (prevKey != nullptr) {
    char prevPath[40];
    snprintf(prevPath, sizeof(prevPath), "/hist/RCT-%s.csv", prevKey);
    int prev = scanFile(prevPath, prevRing, maxRows - cur);
    if (prev > 0) {
      int need = maxRows - cur;
      int prevKept = prev > need ? need : prev;
      drainRing(prevRing, prev, need, out);
      drainRing(ring, cur, cur, out + prevKept);
      total = prevKept + cur;
    } else {
      drainRing(ring, cur, cur, out);
    }
  } else {
    drainRing(ring, cur, cur, out);
  }
  return total;
}