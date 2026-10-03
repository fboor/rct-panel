// Host test for the two pure-logic pieces of the SD logger: the CSV row
// (src/storage/CsvRow.h) and the parked-rows ring (src/storage/RowQueue.h).
//
// Both are header-only and free of Arduino, so this compiles the *shipped*
// headers - not a copy - with nothing but a host compiler. The card itself
// cannot be tested here, which is exactly the point: the parts that decide
// whether a day of samples survives a card outage are pure bookkeeping, and
// this is where they get checked.
//
//   ./run.sh   (or: g++ -std=c++17 -Isrc -o /tmp/sdqt test_rowqueue.cpp)
//
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "storage/CsvRow.h"
#include "storage/RowQueue.h"

// The RCT's meter semantics, for the row -> chart conversion: its load meter
// does not see the external generator, its generation counters do not either,
// and its feed-in counter arrives negative. A device whose meters see
// everything gets the all-true struct, which is what the second half of
// testSampleMapping() checks.
static const DeviceSemantics kRctSemantics = {false, false, true};
static const DeviceSemantics kSeesEverything = {true, true, false};

static int g_checks = 0;
static int g_failed = 0;

static void check(bool ok, const char *what) {
  g_checks++;
  if (!ok) {
    g_failed++;
    printf("FAIL  %s\n", what);
  }
}

static void checkEq(long got, long want, const char *what) {
  g_checks++;
  if (got != want) {
    g_failed++;
    printf("FAIL  %s: got %ld, want %ld\n", what, got, want);
  }
}

// ---------------------------------------------------------------------------
// A row that is easy to recognise: every column a different number, so a
// swapped pair in the format string cannot pass the round trip.
static csvrow::Row makeRow() {
  csvrow::Row r = {};
  r.ts = 1789142400u; // 2026-09-12 00:00:00 UTC
  r.pvA = 1234.0f;
  r.pvB = 567.0f;
  r.s0 = 89.0f;
  r.tc = 41.2f;
  r.tb = -3.5f;
  r.th = 38.0f;
  r.l1 = 111.0f;
  r.l2 = 222.0f;
  r.l3 = 333.0f;
  r.bat = -450.0f;
  r.soc = 62.0f;
  r.g1 = -10.0f;
  r.g2 = 20.0f;
  r.g3 = 30.0f;
  r.faults = 0x0000004Au;
  r.island = 1.0f;
  r.pvATotal = 21001345.0f;
  r.pvBTotal = 9876543.0f;
  r.extTotal = 12345.0f;
  r.loadTotal = 55123456.0f;
  r.feedTotal = 44556677.0f;
  r.gridTotal = 22334455.0f;
  return r;
}

static bool near(float a, float b) {
  const float d = a - b;
  return (d < 0.001f && d > -0.001f);
}

static void testRoundTrip() {
  char line[csvrow::kLineCap];
  const csvrow::Row in = makeRow();
  const size_t n = csvrow::format(line, sizeof(line), in);
  check(n > 0, "format: a normal row is not truncated");
  check(strlen(line) == n, "format: return value is the string length");

  csvrow::Row out = {};
  check(csvrow::parse(line, out), "parse: a formatted row comes back");
  checkEq((long)out.ts, (long)in.ts, "round trip: ts");
  check(near(out.pvA, in.pvA) && near(out.pvB, in.pvB), "round trip: pv");
  check(near(out.s0, in.s0), "round trip: s0");
  check(near(out.tc, in.tc) && near(out.tb, in.tb) && near(out.th, in.th),
        "round trip: temperatures");
  check(near(out.l1, in.l1) && near(out.l2, in.l2) && near(out.l3, in.l3),
        "round trip: load");
  check(near(out.bat, in.bat), "round trip: battery power");
  check(near(out.soc, in.soc), "round trip: soc");
  check(near(out.g1, in.g1) && near(out.g2, in.g2) && near(out.g3, in.g3),
        "round trip: grid");
  checkEq((long)out.faults, (long)in.faults, "round trip: fault mask");
  check(near(out.island, 1.0f), "round trip: island flag");
  check(near(out.pvATotal, in.pvATotal) && near(out.pvBTotal, in.pvBTotal),
        "round trip: PV totals");
  check(near(out.extTotal, in.extTotal), "round trip: external total");
  check(near(out.loadTotal, in.loadTotal), "round trip: household total");
  check(near(out.feedTotal, in.feedTotal), "round trip: feed-in total");
  check(near(out.gridTotal, in.gridTotal), "round trip: grid total");
}

// The header of the format before the sums were logged. Pinned here because
// the whole backward compatibility rests on it: the old names have to be an
// unchanged prefix of today's header, otherwise every old row would have to be
// read by position and a new column could never be appended.
static const char kLegacyHeader[] =
    "ts,pv_a,pv_b,s0,temp_core,temp_bat,temp_hsink,"
    "load_l1,load_l2,load_l3,bat,soc,grid_l1,grid_l2,grid_l3,status";

// The column count is the thing a reordering breaks, and the exact text of the
// header is what a spreadsheet and the web download rely on. Pin both.
static void testHeader() {
  const char *h = csvrow::kHeader;
  int commas = 0;
  for (const char *p = h; *p != '\0'; p++) {
    if (*p == ',') {
      commas++;
    }
  }
  checkEq(commas + 1, csvrow::kColumns, "header: column count matches kColumns");
  checkEq(csvrow::kColumns - csvrow::kLegacyColumns, 7,
          "header: seven columns were appended");
  check(strncmp(h, kLegacyHeader, strlen(kLegacyHeader)) == 0,
        "header: the old names are an unchanged prefix");
  static const char kAppended[] = ",island,pv_a_total_wh";
  check(strncmp(h + strlen(kLegacyHeader), kAppended, strlen(kAppended)) == 0,
        "header: the appended names follow directly");
  check(strstr(h, "status") != nullptr, "header: status is still named");
  // The island flag is the state, not a duration: 0 must be a number, because
  // the manual says that 0 also covers "not answered yet".
  check(strstr(h, "island,") != nullptr, "header: island is a plain column");

  csvrow::Row r = {};
  check(!csvrow::parse(h, r), "parse: the header line is not a data row");
}

// A row as the firmware before format 2 wrote it: sixteen columns, no sums.
// It has to be read as a full row with the appended values at zero - that is the
// promise the whole change rests on, and the only place it can be broken
// silently.
static void testLegacyRow() {
  static const char kLegacyRow[] =
      "1789142400,1234,567,89,41.2,-3.5,38.0,111,222,333,-450,62,-10,20,30,4A";

  csvrow::Row r = {};
  check(csvrow::parse(kLegacyRow, r), "legacy: an old row parses");
  checkEq((long)r.ts, 1789142400L, "legacy: timestamp");
  check(near(r.pvA, 1234.0f) && near(r.th, 38.0f), "legacy: old values intact");
  checkEq((long)r.faults, 0x4AL, "legacy: fault mask");
  check(near(r.island, 0.0f), "legacy: island reads as 0");
  check(near(r.pvATotal, 0.0f) && near(r.pvBTotal, 0.0f),
        "legacy: PV totals are 0");
  check(near(r.extTotal, 0.0f) && near(r.loadTotal, 0.0f),
        "legacy: external and household total are 0");
  check(near(r.feedTotal, 0.0f) && near(r.gridTotal, 0.0f),
        "legacy: feed-in and grid total are 0");

  // And the appended fields are written, not merely left alone: a row struct
  // that comes in full of values must come back cleared in the old fields.
  r.pvATotal = 4711.0f;
  r.gridTotal = 815.0f;
  r.island = 1.0f;
  check(csvrow::parse(kLegacyRow, r), "legacy: parses again over stale values");
  check(near(r.pvATotal, 0.0f) && near(r.gridTotal, 0.0f) &&
            near(r.island, 0.0f),
        "legacy: the appended fields are overwritten with 0");

  // The chart reads an old row as far as it can, and that has to work.
  csvrow::Sample s = {};
  csvrow::toSample(r, s, kRctSemantics);
  checkEq((long)s.ts, 1789142400L, "legacy: the chart takes an old row");
  check(near(s.v[2], 1801.0f), "legacy: chart values are right");
}

static void testWorstCaseLength() {
  // Everything at its longest and negative: three-digit negative powers (six
  // characters), a temperature below zero with one decimal, and a full 8-digit
  // fault mask. kLineCap has to swallow this with room to spare, otherwise
  // format() truncates, parse() gives up and the row is lost without a trace.
  csvrow::Row r = {};
  r.ts = 4102444800u; // 2100-01-01, ten digits
  r.pvA = -1234.0f;
  r.pvB = -999999.0f;
  r.s0 = -10000.0f;
  r.tc = -99.9f;
  r.tb = 100.0f;
  r.th = -12.3f;
  r.l1 = -9999.0f;
  r.l2 = 999999.0f;
  r.l3 = -1000.0f;
  r.bat = -99999.0f;
  r.soc = -1.0f;
  r.g1 = -99999.0f;
  r.g2 = 99999.0f;
  r.g3 = -12345.0f;
  r.faults = 0xFFFFFFFFu;
  // Lifetime counters of a long-lived plant: an 8 kW inverter that has run for
  // years sits around 1e8 Wh, which is what the file really has to carry.
  r.island = 1.0f;
  r.pvATotal = 87654321.0f;
  r.pvBTotal = 87654321.0f;
  r.extTotal = 87654321.0f;
  r.loadTotal = 87654321.0f;
  r.feedTotal = 87654321.0f;
  r.gridTotal = 87654321.0f;

  char line[512];
  const size_t n = csvrow::format(line, sizeof(line), r);
  check(n > 0, "worst case: not truncated");
  check(n + 2 < csvrow::kLineCap, "worst case: fits kLineCap with margin");
  printf("      längste Zeile: %u von %u Zeichen (Puffer %u)\n", (unsigned)n,
         (unsigned)csvrow::kLineCap, (unsigned)sizeof(line));

  csvrow::Row out = {};
  check(csvrow::parse(line, out), "worst case: parses");
  checkEq((long)out.faults, 0xFFFFFFFFL, "worst case: full fault mask");
  checkEq((long)out.ts, 4102444800L, "worst case: 10-digit timestamp");
  check(near(out.tc, -99.9f) && near(out.g3, -12345.0f),
        "worst case: values");
  check(near(out.pvATotal, 87654321.0f) && near(out.gridTotal, 87654321.0f),
        "worst case: lifetime counters survive");

  // The precision contract: whole watts and tenths of a degree. A value with
  // more decimals comes back rounded, which is what the file says too.
  csvrow::Row p = makeRow();
  p.pvA = 1234.6f;  // -> 1235
  p.tc = 41.25f;    // -> 41.2
  char pline[csvrow::kLineCap];
  csvrow::format(pline, sizeof(pline), p);
  csvrow::Row pout = {};
  check(csvrow::parse(pline, pout), "precision: parses");
  check(near(pout.pvA, 1235.0f), "precision: powers are whole numbers");
  check(near(pout.tc, 41.2f), "precision: temperatures keep one decimal");
  // 41.25 is the halfway case: what the file loses is at most half a tenth,
  // which is the number the format promises.
  check(pout.tc - p.tc > -0.051f && pout.tc - p.tc < 0.051f,
        "precision: a temperature survives to its tenth");

  // The row is stored in a Slot of exactly kLineCap bytes, so the ring's own
  // limit has to accept what format() produces.
  check(n < csvrow::kLineCap, "worst case: fits into a ring slot");

  // A buffer that is one character short must be reported, not silently
  // truncated into an unparsable line.
  char small[8];
  checkEq((long)csvrow::format(small, sizeof(small), r), 0,
          "format: too-small buffer is reported");
  checkEq((long)csvrow::format(line, 0, r), 0, "format: cap 0 is reported");
  check((long)csvrow::format(nullptr, 0, r) == 0,
        "format: null out is safe");
}

// The CSV file is a data interface: decimal point, no thousands separator, one
// line per row. A locale or a formatting change here would silently corrupt
// every download.
static void testFileSyntax() {
  csvrow::Row r = makeRow();
  r.pvA = 1234.6f; // rounds to 1235, no comma
  r.soc = 62.4f;
  char line[csvrow::kLineCap];
  csvrow::format(line, sizeof(line), r);
  check(strstr(line, ",") != nullptr, "syntax: commas separate columns");
  check(strchr(line, ';') == nullptr, "syntax: no semicolon separators");
  check(strchr(line, '\r') == nullptr && strchr(line, '\n') == nullptr,
        "syntax: no newline inside the line");
  check(strstr(line, "1235") != nullptr, "syntax: powers are whole numbers");
  check(strstr(line, "41.2") != nullptr, "syntax: temperatures keep one decimal");
  check(strstr(line, "4A") != nullptr, "syntax: fault mask is hex");
  check(strstr(line, "1234.6") == nullptr, "syntax: no untruncated precision");
}

// The chart sums, through the panel's rules rather than through code of their
// own (src/device/Rules.h). That the household is meter plus external is a
// property of the RCT's meters, not of the panel: the same row read with a
// device whose meters see the external generator gives the meter alone.
static void testSampleMapping() {
  csvrow::Row r = makeRow();
  csvrow::Sample s = {};
  csvrow::toSample(r, s, kRctSemantics);
  checkEq((long)s.ts, (long)r.ts, "sample: timestamp");
  check(near(s.v[0], 40.0f), "sample: net = g1+g2+g3");
  check(near(s.v[1], 755.0f), "sample: house = l1+l2+l3+s0");
  check(near(s.v[2], 1801.0f), "sample: pv = pvA+pvB");
  check(near(s.v[3], 89.0f), "sample: s0 as logged");
  check(near(s.v[4], -450.0f), "sample: battery power");
  check(near(s.v[5], 62.0f), "sample: soc");

  // Grid import (positive in this firmware) and export.
  r.g1 = 100.0f;
  r.g2 = -200.0f;
  r.g3 = 0.0f;
  csvrow::toSample(r, s, kRctSemantics);
  check(near(s.v[0], -100.0f), "sample: export is negative grid power");

  // The same row through a device whose household meter already sees the
  // external generator: 666 W of meter, no addition, while the external
  // generator keeps its own series. Before the rules moved into
  // DeviceSemantics this was the sum written out in five places in C++ and once
  // in the browser, and all six would have been wrong here.
  csvrow::toSample(r, s, kSeesEverything);
  check(near(s.v[1], 666.0f),
        "sample: a meter that sees the external generator gets no addition");
  check(near(s.v[3], 89.0f),
        "sample: the external generator keeps its own series either way");
}

static void testParseRejects() {
  csvrow::Row r = {};
  check(!csvrow::parse(nullptr, r), "parse: null line");
  check(!csvrow::parse("", r), "parse: empty line");
  check(!csvrow::parse("1789142400,1,2,3", r), "parse: 4 columns");
  check(!csvrow::parse("1789142400,1,2,3,4,5,6,7,8,9,10,11,12,13,14", r),
        "parse: 15 columns");
  check(!csvrow::parse("1789142400,1,2,3,4,5,6,7,8,9,11,12,13,14,15,4A,99", r),
        "parse: 17 columns are refused, not half-read");
  // One column short of the current format: a row that was cut off after the
  // header grew is refused as well, instead of being read as an old row.
  check(!csvrow::parse("1789142400,1,2,3,4,5,6,7,8,9,11,12,13,14,15,4A,"
                       "0,1,2,3,4,5",
                       r),
        "parse: 22 columns are refused");
  check(!csvrow::parse("1789142400,1,2,3,4,5,6,7,8,9,11,12,13,14,15,4A\r", r),
        "parse: a line that still has its CR is refused");
  check(!csvrow::parse("1789142400,1,2,3,4,5,6,7,8,9,11,12,13,14,15,4A ", r),
        "parse: trailing space is refused");
  check(!csvrow::parse("nonsense", r), "parse: text");
  check(!csvrow::parse(",,,,,,,,,,,,,,,", r), "parse: separators only");
  // Truncated tail of a file that was cut off mid-write: the last row is lost,
  // but it must not be misread as a valid one.
  char line[csvrow::kLineCap];
  csvrow::format(line, sizeof(line), makeRow());
  char *cut = strchr(line, ',');
  *cut = '\0';
  check(!csvrow::parse(line, r), "parse: half a row is rejected");
}

// ---------------------------------------------------------------------------
// The ring. Storage is a plain array, exactly as in sdInit().
static rowq::Slot g_slots[288];
static rowq::Slot g_small[12];

static std::string line(int i) {
  char buf[csvrow::kLineCap];
  snprintf(buf, sizeof(buf), "row-%d", i);
  return std::string(buf);
}

static void testQueueBasics() {
  rowq::Queue q;
  q.attach(g_small, 12);
  check(q.empty(), "queue: empty after attach");
  checkEq(q.count(), 0, "queue: count 0");
  check(q.front() == nullptr, "queue: no front on an empty queue");

  q.push("a", "/hist/A.csv");
  q.push("b", "/hist/A.csv");
  checkEq(q.count(), 2, "queue: two rows parked");
  check(strcmp(q.front()->line, "a") == 0, "queue: oldest first");
  q.pop();
  check(strcmp(q.front()->line, "b") == 0, "queue: pop advances");
  checkEq(q.dropped(), 0, "queue: nothing lost yet");
  q.pop();
  q.pop(); // must not go negative
  checkEq(q.count(), 0, "queue: pop on empty is a no-op");
  checkEq(q.dropped(), 0, "queue: pop on empty loses nothing");

  // No storage attached at all (sdInit without PSRAM and before the fallback).
  rowq::Queue none;
  none.attach(nullptr, 0);
  none.push("a", "/hist/A.csv");
  checkEq(none.count(), 0, "queue: push without storage stores nothing");
  checkEq(none.dropped(), 1, "queue: push without storage counts the loss");

  // attach() is a reset: used at boot, must not inherit anything.
  rowq::Queue again;
  again.attach(g_small, 12);
  again.push("x", "/hist/A.csv");
  again.attach(g_small, 12);
  checkEq(again.count(), 0, "queue: attach clears the parked rows");
  checkEq(again.dropped(), 0, "queue: attach clears the loss counter");
}

// 288 slots is the point of the whole change: a card that is gone for a day
// loses nothing, and the rows come back in order.
static void testFullDay() {
  rowq::Queue q;
  q.attach(g_slots, 288);
  for (int i = 0; i < 288; i++) {
    const std::string l = line(i);
    q.push(l.c_str(), (i < 144) ? "/hist/RCT-202609.csv" : "/hist/RCT-202610.csv");
  }
  checkEq(q.count(), 288, "24 h: all 288 rows parked");
  checkEq(q.dropped(), 0, "24 h: nothing lost");
  check(q.cap() == 288, "24 h: capacity is 24 h");
  check(strcmp(q.front()->line, "row-0") == 0, "24 h: oldest row first");
  const rowq::Slot *last = &g_slots[(0 + 287) % 288];
  check(strcmp(last->line, "row-287") == 0, "24 h: newest row last");

  // The 289th row pushes the oldest one out - and says so.
  q.push("row-288", "/hist/RCT-202610.csv");
  checkEq(q.count(), 288, "overflow: queue stays full");
  checkEq(q.dropped(), 1, "overflow: one row lost");
  check(strcmp(q.front()->line, "row-1") == 0, "overflow: the oldest row went");
}

static void testWrapAround() {
  // Head and tail both wrap: park, drain, park again. A ring that loses the
  // order here would write the day backwards into the file.
  rowq::Queue q;
  q.attach(g_small, 4);
  for (int i = 0; i < 4; i++) {
    const std::string l = line(i);
    q.push(l.c_str(), "/hist/A.csv");
  }
  q.pop();
  q.pop();
  for (int i = 4; i < 6; i++) {
    const std::string l = line(i);
    q.push(l.c_str(), "/hist/A.csv");
  }
  checkEq(q.count(), 4, "wrap: full again");
  int expected = 2;
  bool order = true;
  while (!q.empty()) {
    if (strcmp(q.front()->line, line(expected).c_str()) != 0) {
      order = false;
    }
    q.pop();
    expected++;
  }
  check(order, "wrap: FIFO order survives the wrap");
  checkEq(expected, 6, "wrap: every row came out exactly once");
}

static void testOversizedRows() {
  rowq::Queue q;
  q.attach(g_small, 12);
  std::string exact(csvrow::kLineCap - 1, 'x'); // fits with the NUL
  std::string toolong(csvrow::kLineCap, 'x');   // would be truncated
  q.push(exact.c_str(), "/hist/A.csv");
  checkEq(q.count(), 1, "oversized: a row of exactly kLineCap-1 fits");
  q.push(toolong.c_str(), "/hist/A.csv");
  checkEq(q.count(), 1, "oversized: a row of kLineCap is refused");
  checkEq(q.dropped(), 1, "oversized: refusal counts as a loss");
  check(strlen(q.front()->line) == csvrow::kLineCap - 1,
        "oversized: the stored row is not truncated");

  // Same for the path: "/hist/RCT-202609.csv" is 21 characters, kPathCap 40.
  std::string path(csvrow::kPathCap - 1, 'p');
  std::string pathToolong(csvrow::kPathCap, 'p');
  q.push("row", path.c_str());
  checkEq(q.count(), 2, "oversized: a path of kPathCap-1 fits");
  q.push("row", pathToolong.c_str());
  checkEq(q.count(), 2, "oversized: a path of kPathCap is refused");
  checkEq(q.dropped(), 2, "oversized: refused path counts as a loss");

  q.push(nullptr, "/hist/A.csv");
  q.push("row", nullptr);
  checkEq(q.count(), 2, "oversized: null pointers are refused");
  checkEq(q.dropped(), 4, "oversized: null pointers count as losses");
}

// The sink is the card in the firmware; here it is a map of files. It records
// how often it was opened, because "one open per file instead of one per row"
// is the difference between 288 and 2 directory lookups after a day offline.
struct FakeCard {
  std::map<std::string, std::vector<std::string>> files;
  std::string current;
  int opens = 0;
  bool refuseOpen = false;
  int failAppendAt = -1; // fail the n-th append of this flush
  int appends = 0;

  bool open(const char *path) {
    if (refuseOpen) {
      return false;
    }
    opens++;
    current = path;
    files[current]; // create
    return true;
  }
  bool append(const char *l) {
    appends++;
    if (failAppendAt >= 0 && appends == failAppendAt) {
      return false;
    }
    files[current].push_back(l);
    return true;
  }
  void close() {}
};

static void testFlush() {
  rowq::Queue q;
  FakeCard card;
  q.attach(g_slots, 288);
  for (int i = 0; i < 5; i++) {
    const std::string l = line(i);
    q.push(l.c_str(), "/hist/RCT-202609.csv");
  }
  checkEq(q.flushGrouped(card), 5, "flush: five rows written");
  checkEq(card.opens, 1, "flush: one open for five rows of one file");
  check(q.empty(), "flush: queue empty afterwards");
  checkEq((int)card.files["/hist/RCT-202609.csv"].size(), 5,
          "flush: all rows in the file");

  // Month rollover during the outage: the rows must split, and each file must
  // get its own header - that is what appendRow's fresh-file branch did.
  rowq::Queue q2;
  FakeCard card2;
  q2.attach(g_slots, 288);
  for (int i = 0; i < 3; i++) {
    const std::string l = line(i);
    q2.push(l.c_str(), "/hist/RCT-202609.csv");
  }
  for (int i = 3; i < 6; i++) {
    const std::string l = line(i);
    q2.push(l.c_str(), "/hist/RCT-202610.csv");
  }
  checkEq(q2.flushGrouped(card2), 6, "flush: six rows written");
  checkEq((int)card2.files["/hist/RCT-202609.csv"].size(), 3,
          "flush: three rows in September");
  checkEq((int)card2.files["/hist/RCT-202610.csv"].size(), 3,
          "flush: three rows in October");
  checkEq(card2.opens, 2, "flush: two opens for two files");
  check(strcmp(card2.files["/hist/RCT-202609.csv"][0].c_str(), "row-0") == 0 &&
            strcmp(card2.files["/hist/RCT-202610.csv"][0].c_str(), "row-3") == 0,
        "flush: chronological inside each file");

  // Card gone for good: nothing is written and nothing is lost.
  rowq::Queue q3;
  FakeCard card3;
  q3.attach(g_slots, 288);
  q3.push("row-0", "/hist/A.csv");
  q3.push("row-1", "/hist/A.csv");
  card3.refuseOpen = true;
  checkEq(q3.flushGrouped(card3), 0, "flush: a card that will not open writes 0");
  checkEq(q3.count(), 2, "flush: the rows stay parked");
  checkEq(q3.dropped(), 0, "flush: a failed flush is not a lost row");

  // Short write in the middle: the failing row and everything after it stay,
  // and the rows already written are not written again.
  rowq::Queue q4;
  FakeCard card4;
  q4.attach(g_slots, 288);
  for (int i = 0; i < 6; i++) {
    const std::string l = line(i);
    q4.push(l.c_str(), "/hist/A.csv");
  }
  card4.failAppendAt = 3;
  checkEq(q4.flushGrouped(card4), 2, "flush: stopped at the failing row");
  checkEq(q4.count(), 4, "flush: the rest stays parked");
  check(strcmp(q4.front()->line, "row-2") == 0, "flush: the failed row is kept");
  checkEq((int)card4.files["/hist/A.csv"].size(), 2, "flush: no duplicates");
}

// The column count of a month file's first line. This is the number the log
// prints when a file was created before the row format grew, and it was read
// wrong: the caller reads a fixed 255 bytes, which reaches past a 130-byte
// header into the rows behind it, and every one of their commas was counted
// too - a 16-column file came out as 50 in the log.
static void testFirstLineColumns() {
  // A row as the firmware before format 2 wrote it, with the line end that
  // follows it in the file.
  static const char kLegacyRowLn[] =
      "1789142400,1234,567,89,41.2,-3.5,38.0,111,222,333,-450,62,-10,20,30,4A\r\n";

  // What the reader really gets: 255 bytes out of a file that is in use. The
  // header is shorter than that, so the block runs into the rows behind it.
  static char block[csvrow::kLineCap];
  const size_t n = snprintf(block, sizeof(block) - 1, "%s\r\n%s\r\n%s",
                            kLegacyHeader, kLegacyRowLn, kLegacyRowLn);
  check(n > csvrow::kLineCap - 1, "first line: the block really is overfilled");
  checkEq(csvrow::firstLineColumns(block, csvrow::kLineCap - 1),
          csvrow::kLegacyColumns,
          "first line: only the header counts, not the rows behind it");
  checkEq(csvrow::firstLineColumns(block, n), csvrow::kLegacyColumns,
          "first line: the same holds for a read that got everything");

  // Our own header has to stay inside the block, or the log would fall silent
  // on exactly the files it is meant to explain.
  snprintf(block, sizeof(block) - 1, "%s\r\n%s", csvrow::kHeader, kLegacyRowLn);
  check(csvrow::firstLineColumns(block, csvrow::kLineCap - 1) == csvrow::kColumns,
        "first line: our own header still counts 23 in a 255-byte block");

  // A month file from before the header existed: its first line is a row.
  checkEq(csvrow::firstLineColumns(kLegacyRowLn, strlen(kLegacyRowLn)),
          csvrow::kLegacyColumns,
          "first line: a file without a header counts its first row");

  // The honest answer when the line does not fit: no number, no log line.
  static const char kBare[] = "ts,pv_a";
  checkEq(csvrow::firstLineColumns(kBare, strlen(kBare)), -1,
          "first line: a line without its end is not counted");
  checkEq(csvrow::firstLineColumns("\r\nrest", 6), 0,
          "first line: an empty first line counts 0");
  checkEq(csvrow::firstLineColumns(nullptr, 0), -1,
          "first line: no buffer is not counted");
}

int main() {
  testRoundTrip();
  testHeader();
  testFirstLineColumns();
  testLegacyRow();
  testWorstCaseLength();
  testFileSyntax();
  testSampleMapping();
  testParseRejects();
  testQueueBasics();
  testFullDay();
  testWrapAround();
  testOversizedRows();
  testFlush();

  printf("%s: %d Prüfungen, %d fehlgeschlagen\n",
         g_failed == 0 ? "OK" : "FEHLER", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
