// The parked-rows ring of the SD logger.
//
// Header-only and free of Arduino on purpose: the queue is pure bookkeeping
// (oldest first, drop the oldest on overflow), and the only interesting failure
// mode - a card that is gone for a day - is exactly the one that cannot be
// reproduced with hardware on the desk. tools/sd_queue_test drives the shipped
// header directly, so the ring is tested instead of believed.
//
// Single-context by construction: only the SD worker task pushes and pops
// (docs/sd-history.md, "Why a task"), so nothing here needs a lock. The GUI
// thread only ever posts a message.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_STORAGE_ROWQUEUE_H
#define RCT_STORAGE_ROWQUEUE_H

#include <stdint.h>
#include <string.h>

#include "storage/CsvRow.h"

namespace rowq {

// One parked row: the CSV line already formatted (so it keeps its own
// timestamp) plus the file it belongs to. The path is remembered per row
// because a month rollover during the outage still has to split correctly
// across two files.
struct Slot {
  char line[csvrow::kLineCap];
  char path[csvrow::kPathCap];
};

// Copy with truncation, like strlcpy but without dragging <WString.h> into a
// header that has to compile on the build machine too. Always NUL-terminates
// when cap > 0.
inline size_t copyTrunc(char *dst, size_t cap, const char *src) {
  if (dst == nullptr || cap == 0) {
    return 0;
  }
  size_t n = 0;
  if (src != nullptr) {
    while (n + 1 < cap && src[n] != '\0') {
      dst[n] = src[n];
      n++;
    }
  }
  dst[n] = '\0';
  return n;
}

// Fixed-capacity FIFO of rows. The storage is not owned, so the same class
// serves the small always-present array in internal RAM and the large one in
// PSRAM (see sdInit()).
class Queue {
public:
  void attach(Slot *slots, int cap) {
    s_slots = (slots != nullptr && cap > 0) ? slots : nullptr;
    s_cap = (s_slots != nullptr) ? cap : 0;
    s_count = 0;
    s_head = 0;
    s_dropped = 0;
  }

  // Park one row. On overflow the oldest row goes, because recent data is what
  // the 24 h chart needs; the count of lost rows stays readable via dropped().
  // A row that does not fit the buffers is counted as lost rather than stored
  // as a truncated, unparsable line.
  void push(const char *line, const char *path) {
    if (s_slots == nullptr) {
      s_dropped++;
      return;
    }
    if (line == nullptr || path == nullptr) {
      s_dropped++;
      return;
    }
    if (strlen(line) >= sizeof(s_slots[0].line) ||
        strlen(path) >= sizeof(s_slots[0].path)) {
      s_dropped++; // would be truncated into an unusable row
      return;
    }
    if (s_count == s_cap) {
      s_head = (s_head + 1) % s_cap; // drop the oldest row
      s_count--;
      s_dropped++;
    }
    // Next free slot is one past the newest row. When the ring was just full,
    // head has already moved on by one, so this is exactly the freed slot.
    const int tail = (s_head + s_count) % s_cap;
    copyTrunc(s_slots[tail].line, sizeof(s_slots[tail].line), line);
    copyTrunc(s_slots[tail].path, sizeof(s_slots[tail].path), path);
    s_count++;
  }

  const Slot *front() const {
    return (s_slots != nullptr && s_count > 0) ? &s_slots[s_head] : nullptr;
  }

  // Called after front() was handed to the card. The slot is not wiped: the
  // caller compares the path of the next front() with the one it just wrote,
  // and the caller owns the copy it is looking at.
  void pop() {
    if (s_count <= 0) {
      return;
    }
    s_head = (s_head + 1) % s_cap;
    s_count--;
  }

  int count() const { return s_count; }
  int cap() const { return s_cap; }
  uint32_t dropped() const { return s_dropped; }
  bool empty() const { return s_count == 0; }

  // Count a row as lost without parking it. For the caller that could not hand
  // the row over at all (the worker's request queue is full).
  void noteDropped() { s_dropped++; }

  // Write the parked rows out, oldest first, one file open at a time, and pop
  // what the sink took. This is the whole promise of the ring: after a card was
  // gone for a day the rows arrive in the month file in the order they were
  // sampled, and a card that fails halfway keeps the rest instead of losing it
  // or writing it out of order.
  //
  // The sink is the card in the firmware and a std::map in the test, which is
  // why this lives here and not in sdlog.cpp: the ordering guarantee is exactly
  // the thing that cannot be checked on the desk.
  //
  //   sink.open(path)   -> false: stop and keep everything (card gone)
  //   sink.append(line) -> false: stop, keep this row and the rest (short write)
  //   sink.close()      -> called for every file that was opened
  template <typename Sink> int flushGrouped(Sink &sink) {
    int written = 0;
    char path[csvrow::kPathCap];
    while (!empty()) {
      copyTrunc(path, sizeof(path), front()->path);
      if (!sink.open(path)) {
        break; // card gone again; the rows stay parked
      }
      bool failed = false;
      while (true) {
        const Slot *q = front();
        if (q == nullptr || strcmp(q->path, path) != 0) {
          break; // next row belongs to another file (month rollover)
        }
        if (!sink.append(q->line)) {
          failed = true; // keep this row and everything after it
          break;
        }
        pop();
        written++;
      }
      sink.close();
      if (failed) {
        break;
      }
    }
    return written;
  }

private:
  Slot *s_slots = nullptr;
  int s_cap = 0;
  int s_count = 0;
  int s_head = 0;
  uint32_t s_dropped = 0;
};

} // namespace rowq

#endif // RCT_STORAGE_ROWQUEUE_H
