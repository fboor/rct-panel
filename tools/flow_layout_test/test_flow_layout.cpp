// Host test for the flow diagram's layout (src/gui/FlowLayout.h).
//
// The first case is the one that matters most: a full device has to come out
// pixel-identical to the fixed layout the panel has been drawing since the three
// pills arrived. A refactoring of the diagram is only worth doing if the common
// case does not move by a single pixel - otherwise every existing screenshot,
// every photo in the manual and every habit of looking at that page is wrong.
//
// The other cases are what a second device family asks for: a string inverter
// behind an OpenInverterGateway has a PV node and nothing else, and the point of
// the whole file is that it gets a diagram which says so instead of a house
// with a zero in it.
//
// SPDX-License-Identifier: MIT
#include <cstdio>

#include "device/DeviceCaps.h"
#include "gui/FlowLayout.h"

static int g_checks = 0;
static int g_failed = 0;
static int g_seen = 0;

static void check(bool ok, const char *what) {
  g_checks++;
  if (!ok) {
    g_failed++;
    printf("FAIL  %s\n", what);
  }
}

static void checkEq(long got, long want, const char *what) {
  g_checks++;
  if (got != want) {
    g_failed++;
    printf("FAIL  %s: got %ld, want %ld\n", what, got, want);
  }
}

static DeviceCaps capsOf(bool haus, bool akku, bool netz) {
  DeviceCaps c = {};
  c.houseMeter = haus;
  c.battery = akku;
  c.gridMeter = netz;
  c.islandFlag = true; // every family in the test has it; it is not the subject
  return c;
}

// A full device: exactly the numbers the fixed layout used. If this case moves,
// the panel's screenshots and the manual's pictures are stale.
static void testFullDeviceIsUnchanged() {
  const FlowLayout L = flowLayoutFor(capsOf(true, true, true));

  check(L.pv.visible && L.pv.x == 60 && L.pv.y == 80 && L.pv.d == 60,
        "voll: PV-Knoten unveraendert");
  check(L.house.visible && L.house.x == 240 && L.house.y == 82 && L.house.d == 92,
        "voll: Haus-Knoten unveraendert");
  check(L.grid.visible && L.grid.x == 420 && L.grid.y == 80 && L.grid.d == 60,
        "voll: Netz-Knoten unveraendert");
  check(L.battery.visible && L.battery.x == 240 && L.battery.y == 210 &&
            L.battery.d == 60,
        "voll: Akku-Knoten unveraendert");

  check(L.linkPv.x1 == 240 && L.linkPv.y1 == 82 && L.linkPv.x2 == 60 &&
            L.linkPv.y2 == 80,
        "voll: PV-Leitung unveraendert");
  check(L.linkGrid.x1 == 240 && L.linkGrid.y1 == 82 && L.linkGrid.x2 == 420 &&
            L.linkGrid.y2 == 80,
        "voll: Netz-Leitung unveraendert");
  check(L.linkBattery.x1 == 240 && L.linkBattery.y1 == 82 && L.linkBattery.x2 == 240 &&
            L.linkBattery.y2 == 210,
        "voll: Akku-Leitung unveraendert");

  check(L.valPv.x == 0 && L.valPv.y == 116 && !L.valPv.gross,
        "voll: PV-Wert unveraendert und klein");
  check(L.valHouse.x == 250 && L.valHouse.y == 120, "voll: Haus-Wert unveraendert");
  check(L.valGrid.x == 360 && L.valGrid.y == 118, "voll: Netz-Wert unveraendert");
  check(L.valBattery.x == 180 && L.valBattery.y == 247,
        "voll: Akku-Wert unveraendert");

  check(L.pills == 0x07, "voll: drei Pillen");
  checkEq(L.pillX[0], 6, "voll: erste Pille bei 6");
  checkEq(L.pillX[1], 164, "voll: zweite Pille bei 164");
  checkEq(L.pillX[2], 322, "voll: dritte Pille bei 322");
  check(L.islandMark, "voll: Insel-Zeichen vorhanden");
  check(L.batterySoc, "voll: Ladezustand im Akku-Knoten");
}

// No battery: the house stays the hub, the battery and its connector go.
static void testWithoutBattery() {
  const FlowLayout L = flowLayoutFor(capsOf(true, false, true));
  check(L.pv.x == 60 && L.pv.y == 80 && L.pv.d == 60, "ohne Akku: PV bleibt aussen");
  check(L.house.x == 240 && L.house.y == 82 && L.house.d == 92,
        "ohne Akku: Haus bleibt die Mitte");
  check(!L.battery.visible, "ohne Akku: Akku-Knoten weg");
  check(!L.linkBattery.visible, "ohne Akku: Akku-Leitung weg");
  check(!L.valBattery.visible, "ohne Akku: Akku-Wert weg");
  check(!L.batterySoc, "ohne Akku: kein Ladezustand im Knoten");
  check(L.pills == 0x03, "ohne Akku: zwei Pillen");
  // Fewer pills are centred as a group: two x 152 plus one gap of 6 is 310, and
  // (480 - 310) / 2 = 85. A single pill at the left edge with 320 px of empty
  // space next to it looks broken; the order does not change.
  checkEq(L.pillX[FLOW_PILL_PRODUCTION], 85, "ohne Akku: die zwei Pillen sind mittig");
  checkEq(L.pillX[FLOW_PILL_HOUSE], 85 + 158, "ohne Akku: die zweite daneben");
  // The row keeps its place. A diagram that moves when a device changes is
  // less readable on a wall than one with room left empty.
  check(L.pv.y == 80, "ohne Akku: die Reihe rueckt nicht nach");
}

// No household meter: the PV becomes the hub - big, in the middle, with the
// large value. This is the case a plain string inverter produces.
static void testWithoutHouseMeter() {
  const FlowLayout L = flowLayoutFor(capsOf(false, true, true));
  check(!L.house.visible, "ohne Hauszaehler: Haus-Knoten weg");
  check(!L.valHouse.visible, "ohne Hauszaehler: Haus-Wert weg");

  check(L.pv.x == 240 && L.pv.y == 82 && L.pv.d == 92,
        "ohne Hauszaehler: PV sitzt als Mittelpunkt in der Mitte und ist gross");
  check(L.valPv.gross, "ohne Hauszaehler: PV-Wert gross");
  check(L.valPv.x == kFlowHubValX && L.valPv.y == kFlowHubValY,
        "ohne Hauszaehler: mit Akku und Netz bleibt der Wert, wo der Hauswert war");
  check(L.grossPvIco, "ohne Hauszaehler: das PV-Icon wird wie das eines "
                      "Mittelpunkts skaliert");

  // The connectors now start at the PV node, which is the hub.
  check(!L.linkPv.visible,
        "ohne Hauszaehler: die PV hat keine Verbindung - sie ist die Mitte");
  check(L.linkGrid.x1 == 240 && L.linkGrid.y1 == 82 && L.linkGrid.x2 == 420,
        "ohne Hauszaehler: Netz haengt an der PV");
  check(L.linkBattery.x1 == 240 && L.linkBattery.y1 == 82 &&
            L.linkBattery.x2 == 240 && L.linkBattery.y2 == 210,
        "ohne Hauszaehler: Akku haengt an der PV");

  check(L.pills == 0x05, "ohne Hauszaehler: zwei Pillen (Erzeugung, Akku)");
  // Two pills centred at 85: Erzeugung first, then the battery.
  checkEq(L.pillX[FLOW_PILL_PRODUCTION], 85, "ohne Hauszaehler: Erzeugung zuerst");
  checkEq(L.pillX[FLOW_PILL_BATTERY], 85 + 158, "ohne Hauszaehler: Akku danach");
}

// The Growatt305 behind the stick: generation and nothing else. The diagram is
// one node, and one pill.
static void testOnlyGeneration() {
  const FlowLayout L = flowLayoutFor(capsOf(false, false, false));
  // The only thing the device can report, so twice the size again: 92 * 2.
  check(L.pv.visible && L.pv.x == 240 && L.pv.y == 82 && L.pv.d == kFlowAlleinD,
        "nur Erzeugung: PV in der Mitte und noch einmal doppelt so gross");
  check(L.valPv.gross, "nur Erzeugung: Wert gross");
  // Centred under the node, and lower: the space the battery used is free.
  check(L.valPv.x == kFlowAlleinValX && L.valPv.y == kFlowAlleinValY,
        "nur Erzeugung: der Wert sitzt mittig darunter und weiter unten");
  check(!L.linkPv.visible, "nur Erzeugung: keine Verbindung nach links");
  check(!L.house.visible && !L.grid.visible && !L.battery.visible,
        "nur Erzeugung: nur der PV-Knoten");
  check(!L.linkGrid.visible && !L.linkBattery.visible,
        "nur Erzeugung: keine Verbindung nach aussen");
  check(!L.islandMark, "nur Erzeugung: kein Insel-Zeichen ohne Netzknoten");
  check(L.pills == 0x01, "nur Erzeugung: eine Pille");
  // One pill centred: (480 - 152) / 2 = 164.
  checkEq(L.pillX[FLOW_PILL_PRODUCTION], 164, "nur Erzeugung: die Pille ist mittig");
}

// Before the first answer: the full layout, because that is what an RCT Power
// shows and a diagram that rearranges itself ten seconds after every boot is
// worse than one that starts out right.
static void testUnknownCaps() {
  DeviceCaps leer = {};
  check(!leer.isKnown(), "leere Faehigkeiten sind nicht bekannt");
  const FlowLayout L = flowLayoutFor(leer);
  check(L.house.visible && L.battery.visible && L.grid.visible,
        "unbekannt: alles gezeichnet");
  check(L.pv.d == 60 && !L.valPv.gross, "unbekannt: die PV ist ein normaler Knoten");
  check(L.pills == 0x07, "unbekannt: drei Pillen");
}

// The pill row has to fill the page exactly with three, and be centred with
// fewer - otherwise a single pill sits at the left edge with a third of the page
// empty next to it.
static void testPillGeometry() {
  for (int n = 1; n <= 3; n++) {
    int16_t erste = 0, letzte = 0;
    // Die Bitreihenfolge ist Produktion, Haus, Akku; fuer die Geometrie zaehlt
    // nur, wie viele es sind. Also: alle drei, zwei (Haus+Akku) oder eine.
    // Erzeugung gibt es immer, also sind es drei Pillen bei allem, zwei bei
    // Haus oder Akku und eine bei der blossen Erzeugung.
    const DeviceCaps c = (n == 3)   ? capsOf(true, true, true)
                                  : (n == 2 ? capsOf(true, false, false)
                                            : capsOf(false, false, false));
    g_seen = 0;
    const FlowLayout L = flowLayoutFor(c);
    for (int i = 0; i < 3; i++) {
      if (L.pills & (1u << i)) {
        if (g_seen == 0) {
          erste = L.pillX[i];
        }
        letzte = (int16_t)(L.pillX[i] + kFlowPillW);
        g_seen++;
      }
    }
    const int gesehen = g_seen;
    char msg[80];
    snprintf(msg, sizeof(msg), "%d Pillen: so viele gibt es", n);
    checkEq(gesehen, n, msg);
    const int16_t rand = (int16_t)((kFlowPageW - (letzte - erste)) / 2);
    checkEq(erste, rand, "Pillen mittig");
    snprintf(msg, sizeof(msg), "%d Pillen: der Rand ist gleich links wie rechts",
             n);
    checkEq(kFlowPageW - letzte, rand, msg);
  }
  // Und der Sonderfall, den es vorher nie gab: mit drei Pillen ist der Rand 6,
  // was die Zahl im Kommentar über den Steckplatz ist.
  checkEq(kFlowPageW - (6 + 3 * kFlowPillW + 2 * kFlowPillGap), 6,
          "drei Pillen fuellen die Seite mit 6 px Rand");
}

int main() {
  testFullDeviceIsUnchanged();
  testWithoutBattery();
  testWithoutHouseMeter();
  testOnlyGeneration();
  testUnknownCaps();
  testPillGeometry();

  printf("%s: %d Prüfungen, %d fehlgeschlagen\n",
         g_failed == 0 ? "OK" : "FEHLER", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}