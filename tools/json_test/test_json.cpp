// Host test for the JSON number formatting (src/web/Json.h).
//
// The browser builds its charts from what these numbers look like, so the rules
// are worth more than a glance: fixed-point without an exponent, no trailing
// zeros, null for a value that is not a number. The cases below are the ones
// that actually appear in the answers - the counters in Wh (whole numbers that
// are often exactly round, because a counter counts), the power values in W (one
// decimal, and the negative ones), a percentage between 0 and 100, and the
// values that are not there at all.
//
// A parser meets "nan" and stops reading the document, so the NaN case is the
// one with teeth.
//
// SPDX-License-Identifier: MIT
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#include "web/Json.h"

static int g_failed = 0;
static int g_checks = 0;

static void expect(double v, int decimals, const char *want,
                   const char *what) {
  char b[32];
  g_checks++;
  const int n = jsonNum(b, sizeof(b), v, decimals);
  if (n <= 0 || b[0] == '\0') {
    g_failed++;
    std::printf("  FEHLER %-40s lieferte nichts (cap 32)\n", what);
    return;
  }
  if (std::string(b) != want) {
    g_failed++;
    std::printf("  FEHLER %-40s ist \"%s\", sollte \"%s\" sein\n", what, b,
                want);
  }
  if ((size_t)n != std::strlen(b)) {
    g_failed++;
    std::printf("  FEHLER %-40s Laenge %d passt nicht zu \"%s\"\n", what, n, b);
  }
}

int main() {
  // --- counters in Wh: whole numbers, no fraction asked for ----------------
  expect(23680.0, 0, "23680", "erzeugt, ganzzahlig");
  expect(0.0, 0, "0", "null Wh");
  expect(-1.0, 0, "-1", "negative Wh (Netzeinspeisung kam negativ)");
  expect(3890.4, 0, "3890", "Wh gerundet");
  expect(3890.6, 0, "3891", "Wh aufgerundet");

  // --- power in W: one decimal, trailing zeros cut -------------------------
  expect(0.0, 1, "0", "0,0 W -> 0");
  expect(2.5, 1, "2.5", "2,5 W");
  expect(2.0, 1, "2", "2,0 W -> 2");
  expect(1200.0, 1, "1200", "1200,0 W -> 1200");
  expect(-450.0, 1, "-450", "-450,0 W -> -450");
  expect(1.0 / 3.0, 2, "0.33", "Drittel auf zwei Stellen");
  expect(0.125, 3, "0.125", "Achtel auf drei Stellen");

  // --- the negative zero: -0.04 with one decimal is "-0.0" ----------------
  expect(-0.04, 1, "0", "-0,04 W -> 0 statt -0");
  expect(-0.001, 2, "0", "-0,001 W -> 0 statt -0");
  expect(-0.4, 1, "-0.4", "-0,4 W bleibt negativ");

  // --- percentages ---------------------------------------------------------
  expect(53.46, 1, "53.5", "Autarkie 53,5 %");
  expect(100.0, 1, "100", "100,0 % -> 100");
  expect(0.0, 1, "0", "0,0 % -> 0");

  // --- values that are not numbers ----------------------------------------
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  expect(nan, 1, "null", "NaN wird null, nicht nan");
  expect(-nan, 0, "null", "-NaN wird null");
  expect(inf, 1, "null", "unendlich wird null");
  expect(1e12, 0, "null", "verlaufener Wert wird null");
  expect(-1e12, 0, "null", "verlaufener Wert wird null (negativ)");

  // --- no exponent, whatever the value ------------------------------------
  expect(0.000001, 6, "0.000001", "eine Millionstel (Exponent wäre 1e-06)");
  expect(0.0000001, 6, "0", "eine Zehnmillionstel rundet auf 0");
  expect(0.000001, 8, "0.000001", "eine Millionstel auf acht Stellen");
  expect(123456789.0, 2, "123456789", "neun Stellen ohne Punkt");

  // --- a buffer that is too small must not lie ------------------------------
  {
    char small[4];
    g_checks++;
    if (jsonNum(small, sizeof(small), 12345.0, 0) != 0) {
      g_failed++;
      std::printf("  FEHLER %-40s passte in einen zu kleinen Puffer\n",
                  "zu kleiner Puffer");
    }
    char tight[5];
    g_checks++;
    if (jsonNum(tight, sizeof(tight), nan, 1) != 4 ||
        std::string(tight) != "null") {
      g_failed++;
      std::printf("  FEHLER %-40s \"null\" passte nicht genau\n",
                  "Puffer genau fuer null");
    }
  }

  std::printf("== %d checks, %d failed ==\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
