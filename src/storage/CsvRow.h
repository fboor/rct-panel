// The CSV row: one line of a month file, both ends of the format.
//
// Header-only and free of Arduino, SD and LVGL on purpose. The row format is
// the contract between three places - the writer (sdlog worker), the reader
// that fills the 24 h chart, and the CSV file the web interface hands out as a
// download - so the format and its round trip belong in one testable place
// instead of two static functions inside a 1300-line card driver
// (tools/sd_queue_test checks that everything written comes back unchanged).
//
// 23 columns. Deliberately plain: no separators in the numbers, fixed decimal
// places (no decimal for the powers, one for the three temperatures), a unix
// timestamp in seconds, and the four fault words as one hex mask in the column
// `status`. A value therefore comes back exactly as far as it went in: whole
// watts, tenths of a degree. What is written is what the chart shows, and the
// file stays readable in a spreadsheet.
//
// Format 2 appended seven columns: the island flag and the device's lifetime
// counters (PV A/B, external generator, household, feed-in, grid draw). They
// are the same numbers the display reads, so a row now carries the sums as well
// as the moments - and the sum history stays readable when the month file is
// cut. The rule that made this possible: **the old 16 columns keep their names
// and their order**, the new ones are appended after them. So every row that
// older firmware wrote is a prefix of what is written now, and parse() can take
// it as an old row (see kLegacyColumns).
//
// SPDX-License-Identifier: MIT
#ifndef RCT_STORAGE_CSVROW_H
#define RCT_STORAGE_CSVROW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace csvrow {

// One row, as numbers. The temperatures and the powers are what the panel
// samples; faults is the 128 fault bits of the inverter in one mask; the totals
// are the device's own lifetime counters in Wh.
//
// ts and faults are fixed-width on purpose: the host test and the ESP32 build
// have to produce the same line for the same input, and unsigned long is 32
// bit on the panel but 64 bit on the build machine.
struct Row {
  uint32_t ts; // unix seconds
  float pvA, pvB;   // W, the two PV strings
  float s0;         // W, external generator (S0 meter)
  float tc, tb, th; // degC core / battery / heat sink (one decimal)
  float l1, l2, l3; // W house load per phase
  float bat;        // W battery
  float soc;        // %
  float g1, g2, g3; // W grid per phase
  uint32_t faults;  // 128 fault bits, hex in the file
  // --- format 2, appended ---
  float island;                  // island operation at the moment of sampling
  float pvATotal, pvBTotal;      // Wh, lifetime solar generator A / B
  float extTotal;                // Wh, lifetime external generator (S0)
  float loadTotal;               // Wh, lifetime household
  float feedTotal;               // Wh, lifetime feed-in
  float gridTotal;               // Wh, lifetime draw from the grid
};

// One point of the 24 h chart. Order is the chart series order.
struct Sample {
  uint32_t ts;
  float v[6]; // {net, house, pv, s0, battery, soc} - soc in %, rest in W
};

// The file's first line. Written by sdlog when a file is created, and not on
// append - a month file gets the header exactly once.
static const char kHeader[] =
    "ts,pv_a,pv_b,s0,temp_core,temp_bat,temp_hsink,"
    "load_l1,load_l2,load_l3,bat,soc,grid_l1,grid_l2,grid_l3,status,"
    "island,pv_a_total_wh,pv_b_total_wh,ext_total_wh,load_total_wh,"
    "feed_total_wh,grid_total_wh";

static const int kColumns = 23;

// The shape a row had before the sums were logged. Such a row is read as a full
// row with the appended columns set to 0 - which is the honest answer for a
// counter that was never written, and the reason the old columns may not be
// reordered or renamed.
static const int kLegacyColumns = 16;

// Long enough for the columns with worst-case negative values, three-digit
// temperatures, seven lifetime counters with up to eight digits and a full
// 8-digit fault mask. The test formats the worst case it can construct and
// asserts the room that is left, so this number is checked rather than believed.
static const size_t kLineCap = 256;

// Path buffer, used for the queued rows' file name.
static const size_t kPathCap = 40;

// Format one row into out (always NUL-terminated, truncated rather than
// overflowing). Returns the number of characters written, or 0 if out was too
// small for even a truncated row - which would mean kLineCap is too small.
inline size_t format(char *out, size_t cap, const Row &r) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  const int n = snprintf(out, cap,
                         "%lu,%.0f,%.0f,%.0f,%.1f,%.1f,%.1f,"
                         "%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%lX,"
                         "%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f",
                         (unsigned long)r.ts, (double)r.pvA, (double)r.pvB,
                         (double)r.s0, (double)r.tc, (double)r.tb,
                         (double)r.th, (double)r.l1, (double)r.l2,
                         (double)r.l3, (double)r.bat, (double)r.soc,
                         (double)r.g1, (double)r.g2, (double)r.g3,
                         (unsigned long)r.faults, (double)r.island,
                         (double)r.pvATotal, (double)r.pvBTotal,
                         (double)r.extTotal, (double)r.loadTotal,
                         (double)r.feedTotal, (double)r.gridTotal);
  if (n < 0) {
    out[0] = '\0';
    return 0;
  }
  if ((size_t)n >= cap) {
    // Truncated. The row would be unparsable, so the caller has to know.
    return 0;
  }
  return (size_t)n;
}

// Read one row back. Accepts a full row of kColumns columns and a row of
// kLegacyColumns columns, which is the shape every row written before format 2
// has; there the appended fields are set to 0 rather than left alone, so no
// caller can read a stale value.
//
// Everything else is refused: the header line, a partial line at the end of a
// file, a row with an intermediate column count (a half-written row of the
// current format would land there), and a row that still carries the CR of a
// CRLF line ending (the caller strips it; see scanFile).
//
// Strict on purpose: a row that is not exactly understood is skipped rather
// than half-understood. The visible result is a gap in the 24 h chart, which
// gets looked at; the silent result would be plausible numbers that are
// quietly the wrong ones.
inline bool parse(const char *line, Row &r) {
  if (line == nullptr) {
    return false;
  }
  unsigned long ts = 0, status = 0;
  int consumed = 0;
  // %n is not part of the return value, so n still counts only the columns.
  const int n = sscanf(line,
                       "%lu,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%lX%n",
                       &ts, &r.pvA, &r.pvB, &r.s0, &r.tc, &r.tb, &r.th, &r.l1,
                       &r.l2, &r.l3, &r.bat, &r.soc, &r.g1, &r.g2, &r.g3,
                       &status, &consumed);
  if (n != kLegacyColumns) {
    return false;
  }
  r.ts = (uint32_t)ts;
  r.faults = (uint32_t)status;

  const char *tail = line + consumed;
  if (*tail == '\0') {
    // Old row: the appended columns were never written. Zero them here and not
    // in the caller - a counter that was not logged reads as 0, and only the
    // one place that knows that may say so.
    r.island = 0.0f;
    r.pvATotal = 0.0f;
    r.pvBTotal = 0.0f;
    r.extTotal = 0.0f;
    r.loadTotal = 0.0f;
    r.feedTotal = 0.0f;
    r.gridTotal = 0.0f;
    return true;
  }
  if (*tail != ',') {
    return false;
  }
  int used = 0;
  const int m = sscanf(tail + 1, "%f,%f,%f,%f,%f,%f,%f%n", &r.island,
                       &r.pvATotal, &r.pvBTotal, &r.extTotal, &r.loadTotal,
                       &r.feedTotal, &r.gridTotal, &used);
  if (m != kColumns - kLegacyColumns) {
    return false;
  }
  return tail + 1 + used == line + strlen(line);
}

// Row -> chart point. The sums are the same ones the live display uses, and the
// S0 asymmetry is deliberate: the inverter's load meter does not see external
// generation, so the house is meter + external while the PV node is the two
// strings only. Same rule as the relay's surplus mode.
inline void toSample(const Row &r, Sample &s) {
  s.ts = r.ts;
  s.v[0] = r.g1 + r.g2 + r.g3;        // Netz
  s.v[1] = r.l1 + r.l2 + r.l3 + r.s0;  // Verbrauch (meter + external)
  s.v[2] = r.pvA + r.pvB;              // PV A+B
  s.v[3] = r.s0;                       // S0
  s.v[4] = r.bat;                      // Bat
  s.v[5] = r.soc;                      // SOC %
}

} // namespace csvrow

#endif // RCT_STORAGE_CSVROW_H
