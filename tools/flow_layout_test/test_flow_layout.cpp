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
#include "ui/UiLayout.h"

// The board under test. The test asks the same questions the panel asks, so it has
// to name the same board - and it is the only place besides UiLayout.cpp that does.
static const UiLayout &ui() { return uiLayout(); }

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
  const FlowLayout L = flowLayoutFor(capsOf(true, true, true), ui());

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
  // Die Eigenheiten des einen-Knoten-Falls: alle vier Werte sitzen wie bisher
  // nach ihrer linken Oberkante, und das PV-Icon bleibt das 28px-Icon des
  // Mittelpunkts (320 %) - hier ist ein 60px-Knoten, da waere eine eigene
  // 136px-Schrift nur Speicher fuer nichts.
  check(!L.pvIcoNative, "voll: das 28px-Icon, wie der Mittelpunkt es auch hat");
  check(!L.valPv.centreY && L.valPv.w == ui().flow.valW,
        "voll: PV-Wert wie die anderen drei");
}

// No battery: the house stays the hub, the battery and its connector go.
static void testWithoutBattery() {
  const FlowLayout L = flowLayoutFor(capsOf(true, false, true), ui());
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
  const FlowLayout L = flowLayoutFor(capsOf(false, true, true), ui());
  check(!L.house.visible, "ohne Hauszaehler: Haus-Knoten weg");
  check(!L.valHouse.visible, "ohne Hauszaehler: Haus-Wert weg");

  check(L.pv.x == 240 && L.pv.y == 82 && L.pv.d == 92,
        "ohne Hauszaehler: PV sitzt als Mittelpunkt in der Mitte und ist gross");
  check(L.valPv.gross, "ohne Hauszaehler: PV-Wert gross");
  // With a node below it the number belongs to the node above, so it stays where
  // the house's value was - and it is placed by its top edge like the others.
  check(!L.valPv.centreY, "ohne Hauszaehler: mit Akku ist y die Oberkante");
  checkEq(L.valPv.w, ui().flow.alleinValW,
          "ohne Hauszaehler: auch hier die breite Zahl, der Wert ist gross");
  // 92 px is a node with company, so the 28 px font scaled to 320 % is fine -
  // that is what the house uses as well.
  check(!L.pvIcoNative,
        "ohne Hauszaehler: mit Akku und Netz das 28px-Icon wie im vollen Bild");
  // Nur der eine Knoten rueckt ab: bei einem Akku darunter waere eine andere
  // Mitte genau der Fehler, den diese Aenderung sonst wiederholt.
  check(L.pv.y == ui().flow.hubY, "ohne Hauszaehler: mit Akku in der Zeile geblieben");
  check(L.valPv.x == ui().flow.hubValX && L.valPv.y == ui().flow.hubValY,
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
  const FlowLayout L = flowLayoutFor(capsOf(false, false, false), ui());
  // The only thing the device can report, so bigger than a hub: 138 px.
  check(L.pv.visible && L.pv.x == 240 && L.pv.y == flowAlleinY(ui()) &&
            L.pv.d == ui().flow.alleinD,
        "nur Erzeugung: PV in der Mitte, 190 px gross und 18 px tiefer");
  check(L.valPv.gross, "nur Erzeugung: Wert gross");
  // Centred under the node, and in the middle of the band between the circle's
  // lower edge and the pill's upper edge: 151 and 299, so 225.
  check(L.valPv.x == flowAlleinValX(ui()) && L.valPv.y == flowAlleinValY(ui()),
        "nur Erzeugung: der Wert sitzt mittig darunter und weiter unten");
  // The band between circle and pill, measured where the value lives: in the
  // container, which sits ui().flow.flowY lower than the page the pills are on. Both
  // ends of the band in one coordinate system - 162 and 264, so 213.
  checkEq(flowAlleinValY(ui()), 229, "Band zwischen Kreis und Pille");
  checkEq(L.valPv.y,
          (int16_t)((flowAlleinY(ui()) + ui().flow.alleinD / 2 + (316 - 35) - 34 / 2) / 2),
          "nur Erzeugung: die Mitte ist gerechnet, nicht geraten");
  // Room for the glyph: 114 px of ink inside the circle. 138 px left 12 px on
  // each side and the icon looked pressed against the rim; 190 px leaves 38.
  checkEq(ui().flow.alleinD - 114, 76, "nur Erzeugung: 38 px Luft links und rechts");
  // And the circle fits under the heading: its top edge is inside the container,
  // which starts 9 px below where the heading ends.
  check(flowAlleinY(ui()) - ui().flow.alleinD / 2 >= 0,
        "nur Erzeugung: der Kreis passt unter die Kopfzeile");
  checkEq(flowAlleinY(ui()) - ui().flow.alleinD / 2, 5,
          "nur Erzeugung: und hat 5 px Luft nach oben");
  checkEq(flowAlleinY(ui()), 100, "nur Erzeugung: die Mitte des Kreises");
  // Its y is a middle, so the caller puts the top edge half a line above it -
  // a label is placed by its corner, and getting that wrong is what put the
  // number 16 px too low the first time.
  check(L.valPv.centreY, "nur Erzeugung: y ist die Mitte des Wertes");
  // 200 px, because "1,23 kW" in the 36 px font is 133 px and a narrower label
  // wraps its text onto a second line.
  checkEq(L.valPv.w, ui().flow.alleinValW, "nur Erzeugung: der Wert bekommt 200 px");
  checkEq(L.valPv.w, 200, "nur Erzeugung: 200 px fuer den 36px-Wert");
  checkEq(L.valPv.x, 140, "nur Erzeugung: 200 px mittig auf 240 heisst 140");
  // 190 px is still inside the page, and its bottom leaves a band the value fits
  // into: 195 to 264 is 69 px for a number of 45 - which is the limit. The next
  // 10 px of circle would need the centre at y = 110, and then the band would be
  // 59 px for a number of 45 with 7 px of air on each side of it.
  check(ui().flow.alleinD <= ui().flow.pageW, "nur Erzeugung: der Kreis passt in die Seite");
  // Its own font at its own size: the 28 px glyph at 480 % was a blur.
  check(L.pvIcoNative, "nur Erzeugung: das PV-Icon aus eigener Schrift, 1:1");
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
  const FlowLayout L = flowLayoutFor(leer, ui());
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
    const FlowLayout L = flowLayoutFor(c, ui());
    for (int i = 0; i < 3; i++) {
      if (L.pills & (1u << i)) {
        if (g_seen == 0) {
          erste = L.pillX[i];
        }
        letzte = (int16_t)(L.pillX[i] + ui().flow.pillW);
        g_seen++;
      }
    }
    const int gesehen = g_seen;
    char msg[80];
    snprintf(msg, sizeof(msg), "%d Pillen: so viele gibt es", n);
    checkEq(gesehen, n, msg);
    const int16_t rand = (int16_t)((ui().flow.pageW - (letzte - erste)) / 2);
    checkEq(erste, rand, "Pillen mittig");
    snprintf(msg, sizeof(msg), "%d Pillen: der Rand ist gleich links wie rechts",
             n);
    checkEq(ui().flow.pageW - letzte, rand, msg);
  }
  // Und der Sonderfall, den es vorher nie gab: mit drei Pillen ist der Rand 6,
  // was die Zahl im Kommentar über den Steckplatz ist.
  checkEq(ui().flow.pageW - (6 + 3 * ui().flow.pillW + 2 * ui().flow.pillGap), 6,
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