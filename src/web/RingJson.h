// The 24 h ring as the JSON that the Verlauf page reads, byte for byte.
//
// Header-only and free of Arduino, like Json.h, and for the same reason the test
// in tools/ring_json_test runs the shipped header rather than a copy: the
// separator between two entries is exactly the kind of thing that only misbehaves
// in a case nobody produces on purpose. It did: the comma was tied to "a sample
// has been written", so a ring whose first slots are still empty came out as
// `nullnullnull…` — no valid JSON, and the page answered "Daten konnten nicht
// geladen werden." A ring like that is what a panel produces for the first hour
// after a restart, so it was not an exotic case, only an unlucky moment to look.
//
// Two functions come in as parameters, so that neither the Arduino String nor the
// GUI's history access leaks into this file:
//
//   have(i, &ts, v)   fills the timestamp and the six values of slot i and
//                     returns whether that slot has a sample at all
//   put(text)         appends text to the answer
//
// SPDX-License-Identifier: MIT
#ifndef RCT_WEB_RINGJSON_H
#define RCT_WEB_RINGJSON_H

#include <stdint.h>
#include <stdio.h>

#include "Json.h"

namespace ringjson {

// The whole answer. A slot without a sample is null and not six zeros, so that
// the chart breaks its line there instead of drawing a dip to zero that never
// happened. `from` and `to` are the oldest and newest timestamp the ring still
// holds, and both are 0 when the ring is empty.
template <typename Put, typename Have>
void write(int n, const char *tz, Have have, Put put) {
  char b[32];
  put("{");
  put("\"tz\":\"");
  put(tz);
  put("\",\"points\":");
  snprintf(b, sizeof(b), "%d", n);
  put(b);
  put(",\"series\":[\"grid\",\"load\",\"pv\",\"ext\",\"battery\",\"soc\"],"
      "\"unit\":[\"W\",\"W\",\"W\",\"W\",\"W\",\"%\"],\"data\":[");
  uint32_t from = 0, to = 0;
  for (int i = 0; i < n; i++) {
    // The comma belongs to the *position*, not to whether there is a sample: the
    // second null needs a comma just as much as the first object does.
    if (i > 0) {
      put(",");
    }
    uint32_t ts = 0;
    float v[6];
    if (!have(i, &ts, v)) {
      put("null");
      continue;
    }
    if (from == 0) {
      from = ts; // oldest sample the ring still holds
    }
    to = ts;
    put("{\"t\":");
    snprintf(b, sizeof(b), "%lu", (unsigned long)ts);
    put(b);
    put(",\"v\":[");
    for (int k = 0; k < 6; k++) {
      if (k > 0) {
        put(",");
      }
      if (jsonNum(b, sizeof(b), (double)v[k], 1) > 0) {
        put(b);
      }
    }
    put("]}");
  }
  put("],\"from\":");
  snprintf(b, sizeof(b), "%lu", (unsigned long)from);
  put(b);
  put(",\"to\":");
  snprintf(b, sizeof(b), "%lu", (unsigned long)to);
  put(b);
  put("}");
}

} // namespace ringjson

#endif // RCT_WEB_RINGJSON_H