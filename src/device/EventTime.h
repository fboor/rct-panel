// Turning the numbers of one event entry into text.
//
// The entries themselves are decoded in src/rct/RctEvents.h; what a *word* of
// one means is what this file knows. Two rules, and both are easy to get wrong
// in a way that produces a plausible-looking lie:
//
//   - The third word is an end time for some types and something else for
//     others (an object id, a version, a bit field), so "end - start" is a
//     duration only sometimes. Rather than carry a table of which type does
//     what, the check is a range: an object id is a large positive number, so
//     the difference is either negative or far beyond a day. An event that
//     really ran longer than a day does not exist in this table - the device
//     writes one entry per event.
//
//   - The device's clock is not reliable right after a power loss: the first
//     answers of the day carry a small number, or none at all. A timestamp
//     below 2001 is not printed as a date, because "1970-01-01" would read as
//     a measurement. The serial log and the events page both do this, which is
//     why it is here and not in either of them.
//
// Free of Arduino on purpose, like DeviceState.h: checked on the host in
// tools/rctevent_test.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_EVENT_TIME_H
#define RCT_EVENT_TIME_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

namespace eventtime {

// 2001-09-09. Anything below it did not come from a clock that was set.
const uint32_t kPlausibleFrom = 1000000000u;

// One timestamp as local time, in the caller's format. False when the number is
// not a time this panel can print - the caller decides what to show instead,
// because the log wants a word and a page wants a number.
inline bool formatUnix(uint32_t unixSeconds, const char *fmt, char *out,
                       size_t cap) {
  if (out == nullptr || cap == 0) {
    return false;
  }
  out[0] = '\0';
  if (unixSeconds <= kPlausibleFrom) {
    return false;
  }
  const time_t t = (time_t)unixSeconds;
  struct tm tmv;
  if (!localtime_r(&t, &tmv)) {
    return false;
  }
  return strftime(out, cap, fmt, &tmv) > 0;
}

// How long the event lasted, in seconds. False when the third word is not an
// end time at all, so the caller prints a dash instead of a number nobody can
// tell from a real duration.
inline bool durationSeconds(uint32_t startUnix, uint32_t endUnix,
                            uint32_t &seconds) {
  const int32_t d = (int32_t)(endUnix - startUnix);
  if (endUnix == 0 || startUnix <= kPlausibleFrom || d < 0 || d >= 86400) {
    return false;
  }
  seconds = (uint32_t)d;
  return true;
}

// The same as "hh:mm:ss", or "-" when there is no duration to print.
inline void formatDuration(uint32_t startUnix, uint32_t endUnix, char *out,
                           size_t cap) {
  uint32_t d = 0;
  if (!durationSeconds(startUnix, endUnix, d)) {
    snprintf(out, cap, "-");
    return;
  }
  snprintf(out, cap, "%02u:%02u:%02u", (unsigned)(d / 3600),
           (unsigned)((d / 60) % 60), (unsigned)(d % 60));
}

} // namespace eventtime

#endif // RCT_EVENT_TIME_H
