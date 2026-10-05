// Host test for the chart palette (src/Charts.h).
//
// These six colours are not a taste question. They are read by two readers that
// have to agree - the 24 h chart on the panel and the same chart in the browser -
// and by two kinds of eye that nobody can ask whether they can still tell the
// lines apart. The case that made this test exist: the consumption purple
// 0xA45EE5 and a blue external generator 0x2E93E5 were 3/255 apart once simulated
// for deuteranopia. Same colour. Both were perfectly legible for everyone else,
// so nothing looked wrong on the panel in the living room, and the two lines were
// inseparable for the roughly six per cent of men with a red-green weakness.
//
// So the rule this test holds down is a measured one: every pair of the six has
// to stay far enough apart in the simulated view, and every colour has to stay
// readable on the dark card of the panel. A new colour chosen by eye passes the
// build and fails here, which is the point - the person choosing it gets told why
// instead of finding out from a support case a year later.
//
// The simulation is Vienot/Brettel/Mollon on linear RGB, which is the standard
// approximation used for this kind of check. It is not a perceptual model and
// does not claim to be one; the thresholds below are set where this palette
// actually sits, with a wide margin, not at a literature value.
//
// SPDX-License-Identifier: MIT
#include <math.h>
#include <stdio.h>

#include "Charts.h"

static int g_checks = 0;
static int g_failed = 0;

static void check(bool ok, const char *what) {
  g_checks++;
  if (!ok) {
    g_failed++;
    printf("FAIL  %s\n", what);
  }
}

// The names, so a failure says which pair of lines collided rather than which
// pair of numbers did.
static const char *kName[kChartSeries] = {"Netz", "Verbrauch", "PV", "EXT",
                                          "Akku", "SOC"};

static double toLinear(double v) {
  v /= 255.0;
  return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

// The distance between two colours as a value of 0..255 in the simulated view.
// Zero means the reader cannot tell them apart at all.
enum Cvd { kNormal, kProtan, kDeutan, kTritan, kCvdCount };

static const char *kCvdName[kCvdCount] = {"normal", "protan", "deutan",
                                           "tritan"};

static double distance(uint32_t a, uint32_t b, Cvd how) {
  double ar = toLinear((a >> 16) & 0xFF), ag = toLinear((a >> 8) & 0xFF);
  double ab = toLinear(a & 0xFF);
  double br = toLinear((b >> 16) & 0xFF), bg = toLinear((b >> 8) & 0xFF);
  double bb = toLinear(b & 0xFF);

  // Vienot 1999 / Brettel 1997 for linear RGB. The two equations a dichromat
  // collapses to one axis are given separately so the matrices stay readable.
  double ar2, ag2, ab2, br2, bg2, bb2;
  switch (how) {
    case kProtan:
      ar2 = 0.170 * ar + 0.829 * ag;
      ag2 = ar2;
      ab2 = 0.004 * ar + 0.004 * ag + 1.000 * ab;
      br2 = 0.170 * br + 0.829 * bg;
      bg2 = br2;
      bb2 = 0.004 * br + 0.004 * bg + 1.000 * bb;
      break;
    case kDeutan:
      ar2 = 0.336 * ar + 0.664 * ag;
      ag2 = ar2;
      ab2 = -0.016 * ar + 0.017 * ag + 0.999 * ab;
      br2 = 0.336 * br + 0.664 * bg;
      bg2 = br2;
      bb2 = -0.016 * br + 0.017 * bg + 0.999 * bb;
      break;
    case kTritan:
      ar2 = ar;
      ag2 = 0.952 * ar + 0.048 * ag;
      ab2 = 0.301 * ar + 0.699 * ag;
      br2 = br;
      bg2 = 0.952 * br + 0.048 * bg;
      bb2 = 0.301 * br + 0.699 * bg;
      break;
    default:
      ar2 = ar; ag2 = ag; ab2 = ab;
      br2 = br; bg2 = bg; bb2 = bb;
      break;
  }
  double dr = ar2 - br2, dg = ag2 - bg2, db = ab2 - bb2;
  return sqrt(dr * dr + dg * dg + db * db) * 255.0;
}

static double luminance(uint32_t c) {
  return 0.2126 * toLinear((c >> 16) & 0xFF) +
         0.7152 * toLinear((c >> 8) & 0xFF) +
         0.0722 * toLinear(c & 0xFF);
}

static double contrastOn(uint32_t c, uint32_t bg) {
  double a = luminance(c), b = luminance(bg);
  double hi = a > b ? a : b, lo = a > b ? b : a;
  return (hi + 0.05) / (lo + 0.05);
}

// Two thresholds, both deliberate.
//
// 30/255 for the simulated distance: the palette sits at 51 after the fix, so
// the rule has half the distance in hand and still says no to anything that
// drifts back towards the old pair. 3:1 is the graphical-object level on the dark
// card - the panel card is what the series were measured against, and a line
// below 3:1 stops being a line you can follow across the card.
static const double kMinAbstand = 30.0;
static const double kMinKontrastSchwarz = 3.0;

int main() {
  printf("=== Diagrammfarben: %d Reihen ===\n", kChartSeries);

  // 1) No two series may fall together in the simulated view. This is the
  //    check the purple/blue pair failed.
  for (int i = 0; i < kChartSeries; i++) {
    for (int j = i + 1; j < kChartSeries; j++) {
      for (int c = 1; c < kCvdCount; c++) {
        double d = distance(kChartColor[i], kChartColor[j], (Cvd)c);
        bool ok = d >= kMinAbstand;
        g_checks++;
        if (!ok) {
          g_failed++;
          printf("FAIL  %s / %s bei %s: %.0f/255, noetig %.0f\n", kName[i],
                 kName[j], kCvdName[c], d, kMinAbstand);
        }
      }
    }
  }

  // 2) Every series has to stay readable on the dark card of the panel. The web
  //    card is white and weaker by decision - the palette was measured for the
  //    panel - so this does not fail on the white card and does not pretend to.
  for (int i = 0; i < kChartSeries; i++) {
    double k = contrastOn(kChartColor[i], 0x000000);
    check(k >= kMinKontrastSchwarz, "Reihe unter 3:1 auf der dunklen Karte");
    if (k < kMinKontrastSchwarz) {
      printf("        %s #%06X bei %.2f:1\n", kName[i], kChartColor[i], k);
    }
  }

  // 3) The same palette has to be distinguishable without any simulation too -
  //    that is the ordinary case, and a fix for one kind of weakness must not
  //    cost the common one.
  for (int i = 0; i < kChartSeries; i++) {
    for (int j = i + 1; j < kChartSeries; j++) {
      double d = distance(kChartColor[i], kChartColor[j], kNormal);
      check(d >= kMinAbstand, "zwei Reihen fallen schon ohne Simulation zusammen");
      if (d < kMinAbstand) {
        printf("        %s / %s: %.0f/255\n", kName[i], kName[j], d);
      }
    }
  }

  // 4) The state of charge has its own axis and is the only series that is not
  //    a power. Its index is read by both drawings, so it is the one index in
  //    this header that a reordering would silently break.
  check(kChartSoc == kChartSeries - 1,
        "kChartSoc zeigt auf die letzte Reihe");

  printf("--- Karte ---  ");
  for (int i = 0; i < kChartSeries; i++) {
    printf("%s #%06X", i ? "  " : "", kChartColor[i]);
    if (i != kChartSeries - 1) printf(" %s", kName[i]);
  }
  printf("\n");
  printf("engstes Paar ohne Simulation: ");
  double minNormal = 1e9;
  int mnA = 0, mnB = 0;
  for (int i = 0; i < kChartSeries; i++)
    for (int j = i + 1; j < kChartSeries; j++) {
      double d = distance(kChartColor[i], kChartColor[j], kNormal);
      if (d < minNormal) { minNormal = d; mnA = i; mnB = j; }
    }
  printf("%s/%s %.0f/255\n", kName[mnA], kName[mnB], minNormal);

  printf("engstes Paar mit Simulation: ");
  double minCvd = 1e9;
  int cmnA = 0, cmnB = 0;
  const char *cmnWie = "";
  for (int c = 1; c < kCvdCount; c++)
    for (int i = 0; i < kChartSeries; i++)
      for (int j = i + 1; j < kChartSeries; j++) {
        double d = distance(kChartColor[i], kChartColor[j], (Cvd)c);
        if (d < minCvd) { minCvd = d; cmnA = i; cmnB = j; cmnWie = kCvdName[c]; }
      }
  printf("%s/%s bei %s %.0f/255\n", kName[cmnA], kName[cmnB], cmnWie, minCvd);

  double minK = 1e9;
  int mk = 0;
  for (int i = 0; i < kChartSeries; i++) {
    double k = contrastOn(kChartColor[i], 0x000000);
    if (k < minK) { minK = k; mk = i; }
  }
  printf("schwaechste Reihe auf Schwarz: %s #%06X %.2f:1\n", kName[mk],
         kChartColor[mk], minK);

  printf("%s: %d Pruefungen, %d Fehler\n", g_failed ? "FEHLER" : "ok",
         g_checks, g_failed);
  return g_failed ? 1 : 0;
}