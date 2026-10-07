// Host test for the device abstraction's rules (src/device/Rules.h,
// src/device/DeviceState.h).
//
// Both are header-only and free of Arduino, so this compiles the shipped
// headers with nothing but a host compiler - the same reason DataStatus.h and
// Charts.h are tested rather than looked at on a panel.
//
// What is checked here is the thing the panel used to get wrong in six places
// at once: the household is meter plus external generation on the RCT and
// nothing but the meter on a device whose meter sees the generator itself.
// Nothing in the firmware may decide that any more - it comes from
// DeviceSemantics, which a driver states once, and these tests hold both
// readings against the same input.
//
// SPDX-License-Identifier: MIT
#include <cstdio>

#include "device/DeviceState.h"
#include "device/Rules.h"

static int g_checks = 0;
static int g_failed = 0;

static void checkFlag(bool got, bool want, const char *what) {
  g_checks++;
  if (got != want) {
    g_failed++;
    printf("FAIL  %s: got %s, want %s\n", what, got ? "true" : "false",
           want ? "true" : "false");
  }
}

static void checkNear(float got, float want, const char *what) {
  g_checks++;
  if (got - want > 0.01f || want - got > 0.01f) {
    g_failed++;
    printf("FAIL  %s: got %.2f, want %.2f\n", what, (double)got, (double)want);
  }
}

// How the RCT meters read: the load meter subtracts the external generator, the
// generation counters do not see it either, the feed-in counter is negative.
static const DeviceSemantics kRct = {false, false, true};

// A device that measures everything cleanly: its household meter sees the
// external generator, its generation counters include it, its feed-in counter
// arrives positive.
static const DeviceSemantics kClean = {true, true, false};

// A device with a battery and no external generator at all: the same rules as
// kClean, with extW staying 0. That it needs no flag of its own is the point -
// a missing meter is a zero, not a special case.
static DeviceState makeState(const DeviceSemantics &sem) {
  DeviceState s = {};
  s.semantics = sem;
  return s;
}

// ---------------------------------------------------------------------------
// The measured case from the sign convention: PV 0 W | Haus 832 W | Netz +4 W
// | Batterie +810 W. With no production the battery cannot be charging, and
// 810 + 4 balances the 832 W the house draws - so positive is discharging.
static void testMeasuredEvening() {
  DeviceState s = makeState(kRct);
  s.genW[0] = 0.0f;
  s.genW[1] = 0.0f;
  s.houseW[0] = 832.0f;
  s.gridExchangeW = 4.0f;
  s.batW = 810.0f;
  s.socPct = 92.0f;

  checkNear(ruleHouseW(s), 832.0f, "abend: Haus ist der Zaehlerwert");
  checkNear(ruleGenerationW(s), 0.0f, "abend: keine Erzeugung");
  checkNear(ruleBatteryW(s), 810.0f, "abend: Akku entlaed sich, positiv");
  checkNear(ruleGridExchangeW(s), 4.0f, "abend: Netzbezug ist positiv");
  checkNear(ruleFeedInW(s), 0.0f, "abend: kein Export, also 0 statt Minus");

  // A feed-in moment: the grid value goes negative and the export is its
  // magnitude, which is what the relay's surplus mode switches on.
  s.gridExchangeW = -812.0f;
  checkNear(ruleGridExchangeW(s), -812.0f, "export: Netz bleibt negativ");
  checkNear(ruleFeedInW(s), 812.0f, "export: Einspeisung als Betrag");
}

// The house rule is the whole reason the rules exist. Same meter, same external
// generator, two devices: one whose load meter subtracts the generator and one
// whose meter already includes it.
static void testHouseholdDependsOnTheDevice() {
  DeviceState s = makeState(kRct);
  s.houseW[0] = 500.0f;
  s.houseW[1] = 100.0f;
  s.houseW[2] = 0.0f;
  s.extW = 900.0f;

  checkNear(ruleHouseW(s), 1500.0f,
            "RCT: Haus = alle Phasen + externer Ertrag");
  s.semantics = kClean;
  checkNear(ruleHouseW(s), 600.0f,
            "sauberer Zaehler: Haus = Phasen, der externe Ertrag steckt "
            "drin");

  // A device without an external generator: the rule adds a zero, which is why
  // no extra flag was needed.
  s.semantics = kClean;
  s.extW = 0.0f;
  checkNear(ruleHouseW(s), 600.0f, "ohne externen Ertrag: dieselbe Rechnung");

  // And the case that made this worth a rule: a device whose meter subtracts
  // the generator, with the generator producing more than the meter sees. The
  // household is positive; without the correction it would read the generator.
  s.semantics = kRct;
  s.houseW[0] = -400.0f;
  s.houseW[1] = 0.0f;
  s.houseW[2] = 0.0f;
  s.extW = 900.0f;
  checkNear(ruleHouseW(s), 500.0f,
            "RCT: ein negativer Zaehlerwert wird mit dem Ertrag gerettet");
}

// Generation follows the counters, not the meters: on the RCT the e_dc_* family
// stops at the two DC inputs, so the external generator is added - and the
// overview's PV node has always been strings plus S0.
static void testGenerationDependsOnTheDevice() {
  DeviceState s = makeState(kRct);
  s.genW[0] = 3000.0f;
  s.genW[1] = 2750.0f;
  s.extW = 850.0f;

  checkNear(ruleGenerationW(s), 6600.0f, "RCT: zwei Strings + S0");
  checkNear(ruleGeneratorW(s), 5750.0f, "RCT: die beiden Strings allein");
  s.semantics = kClean;
  checkNear(ruleGenerationW(s), 5750.0f,
            "sauberer Zaehler: der externe Ertrag ist schon enthalten");
  checkNear(ruleGeneratorW(s), 5750.0f,
            "die beiden Strings bleiben die beiden Strings");
}

// The six series of the history. The external generator has its own series, so
// the generation series must not include it - otherwise the chart counts it
// twice. This is the asymmetry that used to need a comment at every call site.
static void testChartSeries() {
  DeviceState s = makeState(kRct);
  s.gridExchangeW = -120.0f;
  s.houseW[0] = 400.0f;
  s.genW[0] = 3000.0f;
  s.genW[1] = 2750.0f;
  s.extW = 850.0f;
  s.batW = -1500.0f;
  s.socPct = 62.0f;

  float v[6] = {};
  ruleChartSample(s, 42, v);
  checkNear(v[0], -120.0f, "Reihe 1: Netz, unveraendert mit Vorzeichen");
  checkNear(v[1], 1250.0f, "Reihe 2: Haus nach der Regel des Geraets");
  checkNear(v[2], 5750.0f, "Reihe 3: Erzeugung ohne den externen Ertrag");
  checkNear(v[3], 850.0f, "Reihe 4: der externe Ertrag als eigene Reihe");
  checkNear(v[4], -1500.0f, "Reihe 5: Akkuleistung, negativ = Ladung");
  checkNear(v[5], 62.0f, "Reihe 6: Ladezustand in Prozent");
}

// The period arithmetic, which the energy page and the JSON endpoint both read.
// The switch over the four periods is the one place a wrong index would show a
// month's number on the daily page, so every period is checked.
static void testPeriods() {
  DeviceState s = makeState(kRct);
  s.caps.houseMeter = true;
  s.caps.gridMeter = true;
  s.caps.battery = true;
  s.dayGenWh = 32500.0f;
  s.dayHouseWh = 21000.0f;
  s.dayFeedInWh = -20100.0f; // measured: negative on the real device
  s.dayGridDrawWh = 0.0f;
  s.dayExtWh = 2830.0f;
  s.monthGenWh = 320000.0f;
  s.monthHouseWh = 210000.0f;
  s.monthFeedInWh = -190000.0f;
  s.monthGridDrawWh = 5000.0f;
  s.monthExtWh = 28000.0f;
  s.yearGenWh = 3200000.0f;
  s.yearHouseWh = 2100000.0f;
  s.yearFeedInWh = -1900000.0f;
  s.yearGridDrawWh = 50000.0f;
  s.yearExtWh = 280000.0f;
  s.totalGenWh = 32000000.0f;
  s.totalHouseWh = 21000000.0f;
  s.feedInTotalWh = -20100000.0f;
  s.gridDrawTotalWh = 549700.0f;
  s.totalExtWh = 2830000.0f;

  const PeriodValues d = rulePeriod(s, 0);
  checkNear(d.genWh, 35330.0f, "Tag: Erzeugung = DC + extern");
  checkNear(d.houseWh, 23830.0f, "Tag: Verbrauch = Haus + extern");
  checkNear(d.feedWh, 20100.0f, "Tag: Einspeisung als Betrag");
  checkNear(d.ownWh, 23830.0f,
            "Tag: Eigenverbrauch = Verbrauch - Bezug (23830 - 0)");
  checkNear(d.gridDrawWh, 0.0f, "Tag: Bezug wie gemessen");

  // The lifetime period reads the two lifetime grid counters, which are carried
  // under different names than the day ones - the switch must reach them.
  const PeriodValues t = rulePeriod(s, 3);
  checkNear(t.genWh, 34830000.0f, "Gesamt: Erzeugung = lifetime + extern");
  checkNear(t.feedWh, 20100000.0f, "Gesamt: lifetime-Einspeisung als Betrag");
  checkNear(t.gridDrawWh, 549700.0f, "Gesamt: lifetime-Bezug");

  const PeriodValues m = rulePeriod(s, 1);
  checkNear(m.genWh, 348000.0f, "Monat: Erzeugung = DC + extern");
  const PeriodValues y = rulePeriod(s, 2);
  checkNear(y.genWh, 3480000.0f, "Jahr: Erzeugung = DC + extern");

  // A device whose counters already include the external generator must not get
  // it added - that would be counted twice, in generation and in consumption.
  // Its feed-in counter arrives positive, which is the same kind of fact about
  // the same kind of device, so it belongs to the same state.
  DeviceState c = makeState(kClean);
  c.caps.houseMeter = true;
  c.caps.gridMeter = true;
  c.dayGenWh = 32500.0f;
  c.dayHouseWh = 21000.0f;
  c.dayFeedInWh = 20100.0f;
  c.dayExtWh = 2830.0f;
  const PeriodValues v = rulePeriod(c, 0);
  checkNear(v.genWh, 32500.0f, "sauberer Zaehler: Erzeugung ohne Zusatz");
  checkNear(v.houseWh, 21000.0f, "sauberer Zaehler: Verbrauch ohne Zusatz");
  checkNear(v.feedWh, 20100.0f,
            "positives Vorzeichen: Einspeisung bleibt wie sie ist");
  checkNear(v.ownWh, 21000.0f, "sauberer Zaehler: Eigenverbrauch ohne Zusatz");

  // The two signs are independent facts: the same device may report its
  // feed-in counter negated, and then the magnitude is what went in. The own
  // consumption does not read that counter at all, so it has to come out the
  // same either way.
  c.semantics.feedCounterNegative = true;
  c.dayFeedInWh = -20100.0f;
  checkNear(rulePeriod(c, 0).feedWh, 20100.0f, "verkehrtes Vorzeichen: Betrag");
  checkNear(rulePeriod(c, 0).ownWh, 21000.0f,
            "verkehrtes Vorzeichen: Eigenverbrauch bleibt derselbe");

  // The battery is the reason the own consumption is counted on the way out and
  // not on the way in: energy that went into the battery today is consumption
  // tomorrow, and the two days must not tell different stories about the same
  // kilowatt hours. Generated 12000 Wh, fed in 900 Wh, consumed 8000 Wh, drawn
  // 1000 Wh: the own consumption is 7000 Wh and not generation minus feed-in.
  // (kClean is the device whose feed-in counter arrives positive, so it is
  // written positive here.)
  DeviceState b = makeState(kClean);
  b.caps.houseMeter = true;
  b.caps.gridMeter = true;
  b.dayGenWh = 12000.0f;
  b.dayFeedInWh = 900.0f;
  b.dayHouseWh = 8000.0f;
  b.dayGridDrawWh = 1000.0f;
  const PeriodValues bp = rulePeriod(b, 0);
  checkNear(bp.ownWh, 7000.0f, "Eigenverbrauch = Verbrauch - Bezug");
  checkNear(bp.genWh - bp.feedWh, 11100.0f,
            "Erzeugung - Einspeisung waere ein anderer Wert (nur der Vergleich)");
  checkNear(bp.houseWh - bp.gridDrawWh, bp.ownWh,
            "Eigenverbrauch ist nie groesser als der Verbrauch");

  // Own use is clamped: the counters run apart for a moment after a device
  // restart, and a negative bar would be meaningless.
  DeviceState k = makeState(kClean);
  k.caps.houseMeter = true;
  k.caps.gridMeter = true;
  k.dayHouseWh = 1000.0f;
  k.dayGridDrawWh = 1200.0f;
  checkNear(rulePeriod(k, 0).ownWh, 0.0f, "Eigenverbrauch ist bei 0 geklammert");

  // A derived number needs both of its operands measured. A device without a
  // house meter or without a grid meter has no own consumption - and the caller
  // asks ruleOwnKnown() so it can leave the figure out instead of showing zero.
  checkFlag(ruleOwnKnown(s), true, "beide Zaehler da: Eigenverbrauch gibt es");
  DeviceState ohneHaus = makeState(kClean);
  ohneHaus.caps.gridMeter = true;
  checkFlag(ruleOwnKnown(ohneHaus), false, "ohne Hauszaehler: kein Eigenverbrauch");
  DeviceState ohneNetz = makeState(kClean);
  ohneNetz.caps.houseMeter = true;
  checkFlag(ruleOwnKnown(ohneNetz), false, "ohne Netzzaehler: kein Eigenverbrauch");
  DeviceState ohneBeide = makeState(kClean);
  checkFlag(ruleOwnKnown(ohneBeide), false,
            "ohne beide Zaehler: kein Eigenverbrauch und keine Quote");
}

// A single-phase device has to work without a flag: phases it does not have stay
// zero, and the sums are the sums.
static void testSinglePhase() {
  DeviceState s = makeState(kClean);
  s.houseW[0] = 240.0f;
  s.genW[0] = 4200.0f;
  s.gridExchangeW = -3960.0f;
  s.socPct = 100.0f;
  checkNear(ruleHouseW(s), 240.0f, "einphasig: Haus");
  checkNear(ruleGenerationW(s), 4200.0f, "einphasig: Erzeugung");
  checkNear(ruleFeedInW(s), 3960.0f, "einphasig: Einspeisung");

  // One generator instead of two, no flag.
  s.genW[0] = 0.0f;
  checkNear(ruleGenerationW(s), 0.0f, "ein Generator zaehlt wie zwei leere");
}

// The direct ways on the flow diagram (ruleFlowSplit). The seven figures are a split
// of four measurements, so the test that matters is not "does case 3 return 3000" but
// "does every node balance": what arrives at it equals what leaves it, and what leaves
// the panels is what the device says the panels produce. A split that adds up at the
// panels and nowhere else is a picture that lies.
static void checkFlowBalanced(DeviceState s, const char *what) {
  const FlowSplit f = ruleFlowSplit(s);
  // Panels: everything they produce is placed somewhere.
  checkNear(f.pvToHouse + f.pvToBattery + f.pvToGrid, ruleGenerationW(s),
            "Biline: die Panels geben ab, was sie erzeugen");
  // House: what fills its consumption.
  checkNear(f.pvToHouse + f.battToHouse + f.gridToHouse, ruleHouseW(s),
            "Biline: das Haus bekommt so viel, wie es verbraucht");
  // Battery: + = discharge, so what leaves it minus what arrives is batW.
  checkNear(f.battToHouse + f.battToGrid - f.pvToBattery - f.gridToBattery, s.batW,
            "Biline: der Akku liefert minus nimmt = seine Leistung");
  // Grid: what goes out over it minus what comes in is the export, - gridExchangeW.
  checkNear(f.pvToGrid + f.battToGrid - f.gridToHouse - f.gridToBattery,
            -s.gridExchangeW, "Biline: ueber das Netz geht hinaus minus hinein");
  // And the conservation law over all seven: everything that moves has to end up in
  // the house, out over the meter, or in the battery. This is what says there is no
  // way into the panels - a figure that had nowhere to go could not be added here.
  const float einspeisung = s.gridExchangeW < 0.0f ? -s.gridExchangeW : 0.0f;
  const float geladen = s.batW < 0.0f ? -s.batW : 0.0f;
  checkNear(f.pvToHouse + f.pvToBattery + f.pvToGrid + f.battToHouse + f.battToGrid +
                f.gridToHouse + f.gridToBattery,
            ruleHouseW(s) + einspeisung + geladen,
            "Biline: alles Bewegte landet im Haus, im Netz oder im Akku");
  (void)what;
}

static DeviceState flowState(float pvW, float houseW, float batW, float gridW) {
  DeviceState s = makeState(kClean); // the household meter sees the generator
  s.haveData = true;
  s.genW[0] = pvW;
  s.genW[1] = 0.0f;
  s.houseW[0] = houseW;
  s.houseW[1] = 0.0f;
  s.houseW[2] = 0.0f;
  s.extW = 0.0f;
  s.batW = batW;
  s.gridExchangeW = gridW;
  return s;
}

static void testFlowSplit() {
  // 5 kW from the panels, 2 kW in the house, nothing from the battery: 3 kW over the
  // top of the ring into the grid. The state in the reference design's screenshot.
  {
    DeviceState s = flowState(5000.0f, 2000.0f, 0.0f, -3000.0f);
    const FlowSplit f = ruleFlowSplit(s);
    checkNear(f.pvToHouse, 2000.0f, "Ueberschuss: 2 kW ins Haus");
    checkNear(f.pvToGrid, 3000.0f, "Ueberschuss: 3 kW direkt ins Netz");
    checkNear(f.pvToBattery, 0.0f, "Ueberschuss: nichts in den Akku");
    checkFlowBalanced(s, "Ueberschuss");
  }
  // Surplus goes into the battery before it goes to the grid.
  {
    DeviceState s = flowState(4000.0f, 1000.0f, -3000.0f, 0.0f);
    const FlowSplit f = ruleFlowSplit(s);
    checkNear(f.pvToHouse, 1000.0f, "Laden: 1 kW ins Haus");
    checkNear(f.pvToBattery, 3000.0f, "Laden: der Rest in den Akku");
    checkNear(f.pvToGrid, 0.0f, "Laden: nichts ins Netz, es ist ein Ueberschuss");
    checkFlowBalanced(s, "Laden aus PV");
  }
  // Panels and battery between them cover the house: the meter is idle.
  {
    DeviceState s = flowState(1000.0f, 3000.0f, 2000.0f, 0.0f);
    const FlowSplit f = ruleFlowSplit(s);
    checkNear(f.pvToHouse, 1000.0f, "Dach: 1 kW PV");
    checkNear(f.battToHouse, 2000.0f, "Dach: 2 kW Akku");
    checkNear(f.gridToHouse, 0.0f, "Dach: das Netz ruhig");
    checkNear(f.battToGrid, 0.0f, "Dach: nichts in den Ueberschuss");
    checkFlowBalanced(s, "PV und Akku decken das Haus");
  }
  // Both direct ways out of the battery and the panels at once. This is the case an
  // earlier version counted twice: it returned houseToGrid == pvToGrid, so the grid
  // spoke and the top wedge would each have shown 4 kW.
  {
    DeviceState s = flowState(5000.0f, 1000.0f, 2000.0f, -6000.0f);
    const FlowSplit f = ruleFlowSplit(s);
    checkNear(f.pvToGrid, 4000.0f, "beide Wege: 4 kW ueber den Ring ins Netz");
    checkNear(f.battToGrid, 2000.0f, "beide Wege: 2 kW aus dem Akku ins Netz");
    checkNear(f.gridToHouse, 0.0f, "beide Wege: der Netzspeer hat nichts Fuenftes");
    checkFlowBalanced(s, "Panels und Akku speisen ein");
  }
  // The meter pays for the charging, which is the lower right of the ring running
  // upwards.
  {
    DeviceState s = flowState(0.0f, 1000.0f, -500.0f, 1500.0f);
    const FlowSplit f = ruleFlowSplit(s);
    checkNear(f.gridToBattery, 500.0f, "Laden aus dem Netz: 500 W in den Akku");
    checkNear(f.gridToHouse, 1000.0f, "Laden aus dem Netz: der Rest ins Haus");
    checkNear(f.pvToBattery, 0.0f, "Laden aus dem Netz: die Panels haben nichts");
    checkFlowBalanced(s, "Laden aus dem Netz");
  }
  // A dark evening: nothing is generated, so every direct way is empty and only the
  // grid spoke to the house carries something.
  {
    DeviceState s = flowState(0.0f, 500.0f, 0.0f, 500.0f);
    const FlowSplit f = ruleFlowSplit(s);
    checkNear(f.pvToHouse + f.pvToBattery + f.pvToGrid, 0.0f, "Abend: kein Ring-Weg");
    checkNear(f.gridToHouse, 500.0f, "Abend: das Haus aus dem Netz");
    checkFlowBalanced(s, "Abend");
  }
  // Before the device has answered once, nothing flows - even with numbers in the
  // state. A diagram that splits figures nobody measured shows a guess.
  {
    DeviceState s = flowState(5000.0f, 2000.0f, 0.0f, -3000.0f);
    s.haveData = false;
    const FlowSplit f = ruleFlowSplit(s);
    checkNear(f.pvToHouse + f.pvToBattery + f.pvToGrid + f.battToHouse +
                  f.battToGrid + f.gridToHouse + f.gridToBattery,
              0.0f, "vor der ersten Antwort: nichts fliesst");
  }
}

int main() {
  testMeasuredEvening();
  testHouseholdDependsOnTheDevice();
  testGenerationDependsOnTheDevice();
  testFlowSplit();
  testChartSeries();
  testPeriods();
  testSinglePhase();

  printf("%s: %d Prüfungen, %d fehlgeschlagen\n", g_failed == 0 ? "OK" : "FEHLER",
         g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}