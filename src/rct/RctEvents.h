// The device's event log (logger.error_log_time_stamp, OID 0x6F3876BC), read
// the way the official app reads it.
//
// The table is asked for, not overheard. The panel's web page has a button that
// sends the app's own pair - a WRITE of a UNIX timestamp to this object id,
// then a READ of it - and the device answers the read with the table (see
// RctDriver::requestEventLog and rctSendWrite). The panel also decodes the
// answer whenever another client asks, because every client on the bus sees
// every frame; that is why the decoding here is independent of who asked.
//
// How the app gets it (decompiled, version 2.500, and the rctclient
// documentation, which says the same about the wire format):
//
//   - the app writes a UNIX timestamp to the object id: the end of the day the
//     user picked. The device answers with every entry OLDER than that,
//     several pages, each page after the first asked for with the oldest
//     entry's own timestamp.
//   - the answer is a LONG_RESPONSE (command 0x06): the query timestamp comes
//     back first, then entries of twenty bytes each - and an entry whose start
//     timestamp is 0 ends the table.
//   - one entry: type, start, end, value, value. What the last three mean
//     depends on the type; the app reads them as signed where a negative value
//     is possible (a duration is end - start, a float bit pattern is a
//     measurement) and this keeps them raw, because the meaning is per type.
//
// The type is a number in the SAME numbering as the 128 fault bits: the app
// looks it up in the same string array it uses for the bit field, and 18 of the
// 19 entries the rctclient documentation names land on exactly that name. The
// one that did not is the documentation's error, not the app's: it puts
// PHASE_POS at 0x61, where the app's array holds PIC_SOFT - PHASE_POS is
// 0x63. So the panel can name an entry with the fault texts it already has
// (T_FAULT_0 + type). The app's table counts three names more than the bits
// do - 0x80 BMS_CALIBRATION, 0x81 POWER_SW_RELAY, 0x82 BAT_COMPATIBILITY - so
// the event table's type range ends at 0x82, not 0x7f, and those three are
// texts of their own (T_FAULT_128..130, added from the app's array).
//
// SPDX-License-Identifier: Apache-2.0
#ifndef RCT_RCTEVENTS_H
#define RCT_RCTEVENTS_H

#include <stddef.h>
#include <stdint.h>

namespace rctevents {

// logger.error_log_time_stamp, index 391 of group 8 (LOGGER).
const uint32_t kTableOid = 0x6F3876BCu;

// Query timestamp in front of the entries.
const size_t kQueryBytes = 4;
// type, start, end, value, value - five four-byte words.
const size_t kEntryBytes = 20;

// How many entries the panel keeps. The page shows them newest first and the
// panel asks for one window ("everything older than now"), so a larger number
// would only hold entries the user can no longer get to - walking backwards
// through the log, page by page, is what the app does and this panel does not.
const size_t kMaxEntries = 40;

struct Entry {
  uint8_t type;       // same numbering as the fault bits 0..127
  uint32_t startUnix; // 0 marks the end of the table, never an entry
  uint32_t endUnix;   // end of a ranged event, or an object id
  uint32_t value1;    // type specific
  uint32_t value2;    // type specific
};

class Table {
public:
  Table() : mCount(0), mQueryUnix(0), mPages(0) {}

  // Decode one long response. Returns how many entries in it the panel had not
  // seen before - a client that asks the same window twice gets the same
  // answer, and the panel does not want the same entry twice. That is not the
  // same as how many the list grew by: a page with more entries than the panel
  // keeps counts all of them as new, and the oldest fall out at the back.
  //
  // A window of zero entries is not an error: the device answers an empty table
  // when there is nothing older than the timestamp it was given.
  size_t decode(const uint8_t *payload, size_t len) {
    if (len < kQueryBytes) {
      return 0;
    }
    mQueryUnix = be32(payload);
    mPages++;
    size_t seen = 0;
    size_t at = kQueryBytes;
    while (at + kEntryBytes <= len) {
      Entry e;
      e.type = (uint8_t)be32(payload + at);
      e.startUnix = be32(payload + at + 4);
      if (e.startUnix == 0) {
        break; // end of table
      }
      e.endUnix = be32(payload + at + 8);
      e.value1 = be32(payload + at + 12);
      e.value2 = be32(payload + at + 16);
      at += kEntryBytes;
      if (!known(e)) {
        seen++;
        insert(e);
      }
    }
    return seen;
  }

  size_t size() const { return mCount; }
  // Newest first: at(0) is the most recent event, whatever page it arrived on.
  const Entry &at(size_t i) const { return mEntries[i]; }
  // The timestamp the last answer echoed back, i.e. what the client asked for.
  uint32_t queryUnix() const { return mQueryUnix; }
  // How many answers arrived - pages of the same table count once each, and a
  // number that grows while the entries do not is the sign that the device has
  // more to give.
  uint32_t pages() const { return mPages; }
  uint32_t oldestUnix() const { return mCount ? mEntries[mCount - 1].startUnix : 0; }
  uint32_t newestUnix() const { return mCount ? mEntries[0].startUnix : 0; }

  void clear() {
    mCount = 0;
    mPages = 0;
  }

private:
  static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
  }

  static bool same(const Entry &a, const Entry &b) {
    return a.type == b.type && a.startUnix == b.startUnix &&
           a.endUnix == b.endUnix && a.value1 == b.value1 &&
           a.value2 == b.value2;
  }

  bool known(const Entry &e) const {
    for (size_t i = 0; i < mCount; i++) {
      if (same(mEntries[i], e)) {
        return true;
      }
    }
    return false;
  }

  // The list is sorted by start time, newest first, and stays sorted. It cannot
  // be order of arrival: the device answers one window per request and the
  // client walks BACKWARDS through the log, so a page of older entries arrives
  // last and would sit in front of everything else.
  //
  // A full list takes the entry only if it belongs in front of the oldest one
  // that is there; otherwise the entry is the one that falls out.
  void insert(const Entry &e) {
    size_t pos = 0;
    while (pos < mCount && mEntries[pos].startUnix >= e.startUnix) {
      pos++;
    }
    if (mCount == kMaxEntries && pos == mCount) {
      return;
    }
    if (mCount < kMaxEntries) {
      mCount++;
    }
    for (size_t i = mCount - 1; i > pos; i--) {
      mEntries[i] = mEntries[i - 1];
    }
    mEntries[pos] = e;
  }

  Entry mEntries[kMaxEntries]; // sorted: mEntries[0] is the newest
  size_t mCount;
  uint32_t mQueryUnix;
  uint32_t mPages;
};

} // namespace rctevents

#endif // RCT_RCTEVENTS_H