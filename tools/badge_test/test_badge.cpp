// Host test for the data-status decision (src/DataStatus.h).
//
// Five states, four inputs, and the *order* of the cases is the whole logic: a
// device that never answered has to stay red instead of being softened to a
// yellow "waiting", and "waiting" may only happen with a link that is up and
// data that is merely old. On the panel those five cases are two switch
// statements with colours next to them - one moved line and the badge starts
// lying. Here it is a function call.
//
//   ./run.sh   (or: g++ -std=c++17 -I src -o /tmp/badge_test test_badge.cpp)
//
// SPDX-License-Identifier: MIT
#include <cstdio>

#include "DataStatus.h"

static int g_checks = 0;
static int g_failed = 0;

static void check(bool ok, const char *what) {
  g_checks++;
  if (!ok) {
    g_failed++;
    printf("FAIL  %s\n", what);
  }
}

static DataStatus of(bool connecting, bool haveData, bool connected,
                     uint32_t ageMs) {
  return dataStatus(connecting, haveData, connected, ageMs);
}

// The five states, each from the inputs that are supposed to produce it.
static void testStates() {
  check(of(true, false, false, 0) == DataStatus::Connecting,
        "connecting wins over everything");
  check(of(false, false, false, 0) == DataStatus::NoData,
        "no data: link up, never anything");
  check(of(false, true, false, 0) == DataStatus::Reconnect,
        "reconnect: data was there, stream stopped");
  check(of(false, true, true, kDataStaleMs) == DataStatus::Live,
        "live at the threshold");
  check(of(false, true, true, kDataStaleMs + 1) == DataStatus::Waiting,
        "waiting one millisecond past the threshold");
}

// The order of the cases. Each pair differs in exactly one input, so a case
// that moved up or down shows up here.
static void testOrder() {
  check(of(true, true, false, 999999) == DataStatus::Connecting,
        "connecting beats stale data");
  check(of(true, true, true, 999999) == DataStatus::Connecting,
        "connecting beats waiting");
  check(of(false, false, false, 999999) == DataStatus::NoData,
        "never answered beats reconnect: it is the stronger fault");
  check(of(false, false, true, 999999) == DataStatus::NoData,
        "never answered beats waiting");
  check(of(false, true, false, 999999) == DataStatus::Reconnect,
        "a dropped link is reported as such, not as waiting");
}

// The boundary. "60 s" has to mean one thing on the badge and in the line
// under the switching output, so the threshold itself is pinned.
static void testThreshold() {
  check(kDataStaleMs == 60000, "threshold is one minute");
  check(of(false, true, true, 0) == DataStatus::Live, "fresh frame");
  check(of(false, true, true, 59999) == DataStatus::Live,
        "one millisecond before the threshold is still live");
  check(of(false, true, true, 60000) == DataStatus::Live,
        "exactly at the threshold is still live");
  check(of(false, true, true, 60001) == DataStatus::Waiting,
        "one millisecond after it is waiting");
}

// The age helper, including the millis() wrap after 49 days: a wrap that
// compared as "young" would hide a device that has been silent for weeks, and
// one that compared as "ancient" would light the badge on a fresh boot.
static void testAge() {
  check(dataAgeMs(5000, 1000) == 4000, "age from a difference");
  check(dataAgeMs(1000, 1000) == 0, "age zero on a fresh frame");
  // A frame stamped before the wrap and read after it: the age has to come out
  // as the real distance - 500 ms past zero plus the 70000 ms before the wrap
  // plus the 4096 ms from that stamp to the wrap - not as a number near 2^32 and
  // not as a small one either.
  const uint32_t beforeWrap = 0xFFFFF000u - 70000u;
  const uint32_t wrapped = dataAgeMs(500u, beforeWrap);
  check(wrapped == 500u + 70000u + 4096u,
        "age across the millis() wrap stays a real duration");
  // 0xFFFFFFFF + 1 == 0: the frame arrives exactly at the wrap.
  check(dataAgeMs(0u, 0xFFFFFFFFu) == 1, "age one millisecond across the wrap");
}

int main() {
  printf("== data status (host test) ==\n");
  testStates();
  testOrder();
  testThreshold();
  testAge();
  printf("== %d checks, %d failed ==\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}