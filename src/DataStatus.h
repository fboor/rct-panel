// How fresh the values on the display (or on the web page) actually are.
//
// Header-only and free of Arduino and LVGL, like NumFmt.h: the decision is
// five cases over four booleans and one age, and the *order* of the cases is
// the whole logic - a case moved up shows a device that never answered as
// merely "waiting". That is exactly the kind of thing that has to be checked
// on the host rather than looked for on a panel (tools/badge_test).
//
// The inputs are what the snapshot and the config layer already know:
//   networkConnecting  the Wi-Fi link is still being set up
//   haveData           a frame has arrived at least once
//   connected          the TCP link is up right now
//   dataAgeMs          millis() - lastUpdateMs
//
// The age threshold is the same one the status line uses, so "wartet" on the
// badge and "(letzte Messung)" under the switching output always mean the same
// thing: the number on screen is older than a minute.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_DATA_STATUS_H
#define RCT_DATA_STATUS_H

#include <stdint.h>

// Six missed polls of 10 s. Long enough that one dropped frame does not show,
// short enough that a real gap is on screen within a minute.
static const uint32_t kDataStaleMs = 60000;

enum class DataStatus {
  Connecting, // Wi-Fi or the link is still coming up
  NoData,     // link is up, but nothing has ever arrived
  Reconnect,  // data was there, the stream stopped
  Waiting,    // link is up and we had data - but nothing new for a while
  Live,       // fresh values
};

// One function, so panel and web cannot drift apart: both call this and only
// map the result to their own texts and colours.
inline DataStatus dataStatus(bool networkConnecting, bool haveData,
                             bool connected, uint32_t dataAgeMs) {
  if (networkConnecting) {
    return DataStatus::Connecting;
  }
  if (!haveData) {
    return DataStatus::NoData;
  }
  if (!connected) {
    return DataStatus::Reconnect;
  }
  if (dataAgeMs > kDataStaleMs) {
    return DataStatus::Waiting;
  }
  return DataStatus::Live;
}

// Age of the newest frame, safe across the millis() wrap (49 days). Callers
// pass the raw difference; this is the same cast Relay.cpp uses for its own
// data-age rule, so both read "old" at the same moment.
inline uint32_t dataAgeMs(uint32_t nowMs, uint32_t lastUpdateMs) {
  return (uint32_t)(int32_t)(nowMs - lastUpdateMs);
}

#endif // RCT_DATA_STATUS_H