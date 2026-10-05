// Host test for the ring JSON writer (src/web/RingJson.h).
//
// The writer is what /api/verlauf.json consists of, and the page shows "Daten
// konnten nicht geladen werden." for every answer it cannot parse. The case that
// made the comma go missing is the one this test exists for: a ring whose first
// slots are still empty, which is what a panel produces for the first hours after
// a restart. Nobody writes that case on purpose, so it is written here.
//
// The header is compiled as shipped, not copied, and it is free of Arduino: the
// two functions come in as parameters. So the test can also check that the answer
// is parsable at all - the scanner from src/device/Json.h reads it, which is the
// same scanner the second device family reads its values with.
//
// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <string.h>

#include <string>

#include "device/Json.h"
#include "web/RingJson.h"

static int g_checks = 0;
static int g_failed = 0;

static void check(bool ok, const char *what) {
  g_checks++;
  if (!ok) {
    g_failed++;
    printf("FAIL  %s\n", what);
  }
}

static void checkStr(const std::string &got, const std::string &want,
                     const char *what) {
  g_checks++;
  if (got != want) {
    g_failed++;
    printf("FAIL  %s\n        got  %s\n        want %s\n", what, got.c_str(),
           want.c_str());
  }
}

// One ring: slots, of which the ones listed as present have a sample.
struct Ring {
  int n;
  uint32_t ts[8];
  float v[8][6];
  bool da[8];

  bool have(int i, uint32_t *outTs, float *outV) const {
    if (i < 0 || i >= n || !da[i]) {
      return false;
    }
    *outTs = ts[i];
    for (int k = 0; k < 6; k++) {
      outV[k] = v[i][k];
    }
    return true;
  }
};

static std::string write(const Ring &r) {
  std::string s;
  ringjson::write(r.n, "CET-1CEST,M3.5.0,M10.5.0/3",
                  [&r](int i, uint32_t *ts, float *v) {
                    return r.have(i, ts, v);
                  },
                  [&s](const char *t) { s += t; });
  return s;
}

// The ring of a panel that was restarted: the oldest slots are still empty.
// This is the case that answered `nullnullnull`.
static void testEmptyFront() {
  Ring r{};
  r.n = 8;
  for (int i = 0; i < 8; i++) {
    r.da[i] = i >= 5; // the five oldest slots are still empty
    r.ts[i] = 1791122829u + (uint32_t)i * 300u;
    // Halbe Schritte: im Binärsystem genau darstellbar, damit die erwartete
    // Zeichenkette eindeutig ist - 100.0 schreibt jsonNum() als "100", weil es
    // die Nachkommanullen abschneidet, und das gehört mit geprüft.
    for (int k = 0; k < 6; k++) {
      r.v[i][k] = 100.0f * (float)(i + 1) + 0.5f * (float)k;
    }
  }
  const std::string s = write(r);
  checkStr(s,
           "{\"tz\":\"CET-1CEST,M3.5.0,M10.5.0/3\",\"points\":8,"
           "\"series\":[\"grid\",\"load\",\"pv\",\"ext\",\"battery\",\"soc\"],"
           "\"unit\":[\"W\",\"W\",\"W\",\"W\",\"W\",\"%\"],"
           "\"data\":[null,null,null,null,null,"
           "{\"t\":1791124329,\"v\":[600,600.5,601,601.5,602,602.5]},"
           "{\"t\":1791124629,\"v\":[700,700.5,701,701.5,702,702.5]},"
           "{\"t\":1791124929,\"v\":[800,800.5,801,801.5,802,802.5]}],"
           "\"from\":1791124329,\"to\":1791124929}",
           "a ring with empty slots at the front");

  // Two entries of nothing in a row must not run into each other either.
  check(s.find("null,null,null,null,null") != std::string::npos,
        "every empty slot gets its own comma");
}

// A full ring, and a gap in the middle of it: the gap is one null, not six zeros.
static void testFullAndGap() {
  Ring r{};
  r.n = 4;
  for (int i = 0; i < 4; i++) {
    r.da[i] = i != 2;
    r.ts[i] = 1791000000u + (uint32_t)i * 300u;
    for (int k = 0; k < 6; k++) {
      r.v[i][k] = -2047.0f + 0.5f * (float)k;
    }
  }
  const std::string s = write(r);
  check(s.find("\"data\":[{\"t\":1791000000,\"v\":[-2047,-2046.5,-2046,"
               "-2045.5,-2045,-2044.5]},{\"t\":1791000300,") !=
            std::string::npos,
        "a full ring starts with its object, without a comma");
  check(s.find("},null,{\"t\":1791000900") != std::string::npos,
        "a gap in the middle is one null");
  check(s.find("-2046.5") != std::string::npos,
        "the numbers carry one decimal where they have one");
  check(s.find("-2047.0") == std::string::npos,
        "and no trailing zero on a whole number");

  // The scanner the second device family reads its values with has to be able to
  // read the answer: json::getNumber() walks the same bytes the browser parses,
  // so it is the same question asked a second way.
  double t0 = 0.0, to = 0.0;
  check(json::getNumber(s.c_str(), s.size(), "from", &t0),
        "the scanner finds \"from\" in the answer");
  check(json::getNumber(s.c_str(), s.size(), "to", &to), "and \"to\"");
  check(t0 == 1791000000.0 && to == 1791000900.0,
        "with the oldest and newest sample of the ring");
  check(json::getNumber(s.c_str(), s.size(), "points", &t0) && t0 == 4.0,
        "and the point count");
}

// An empty ring: no entries at all, and both ends at 0 rather than at a
// timestamp that never existed.
static void testEmpty() {
  Ring r{};
  r.n = 3;
  const std::string s = write(r);
  checkStr(s,
           "{\"tz\":\"CET-1CEST,M3.5.0,M10.5.0/3\",\"points\":3,"
           "\"series\":[\"grid\",\"load\",\"pv\",\"ext\",\"battery\",\"soc\"],"
           "\"unit\":[\"W\",\"W\",\"W\",\"W\",\"W\",\"%\"],"
           "\"data\":[null,null,null],\"from\":0,\"to\":0}",
           "an empty ring answers three nulls and no timestamps");

  // A NaN must not make it into the answer as the word "nan", which would stop a
  // parser dead. jsonNum() writes null for it.
  Ring n{};
  n.n = 1;
  n.da[0] = true;
  n.ts[0] = 1791000000u;
  for (int k = 0; k < 6; k++) {
    n.v[0][k] = 0.0f / 0.0f;
  }
  const std::string s2 = write(n);
  check(s2.find("nan") == std::string::npos, "no nan in the answer");
  check(s2.find("null") != std::string::npos, "a NaN value becomes null");
}

int main() {
  testEmptyFront();
  testFullAndGap();
  testEmpty();
  printf("OK: %d Prüfungen, %d fehlgeschlagen\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}