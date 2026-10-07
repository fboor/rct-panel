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

// A full device. If this case moves, the panel's screenshots and the manual's
// pictures are stale.
//
// THE FIGURES BELOW ALL MOVED ON PURPOSE, once, when the three outer nodes were put
// on a ring instead of in an inverted T: PV from 60 to 68, the grid from 420 to 412,
// the row from 80 to 116, the battery from 210 to 208. The one-node case was the one
// thing that did not move (see testOnlyGeneration), because that circle is placed
// against the top of the container and not against the row.
static void testFullDeviceIsUnchanged() {
  const FlowLayout L = flowLayoutFor(capsOf(true, true, true), ui());

  check(L.pv.visible && L.pv.x == 136 && L.pv.y == 64 && L.pv.d == 60,
        "voll: PV-Knoten links oben auf dem Ring");
  check(L.house.visible && L.house.x == 240 && L.house.y == 124 && L.house.d == 92,
        "voll: Haus-Knoten in der Mitte des Rings");
  check(L.grid.visible && L.grid.x == 343 && L.grid.y == 64 && L.grid.d == 60,
        "voll: Netz-Knoten rechts oben auf dem Ring");
  check(L.battery.visible && L.battery.x == 240 && L.battery.y == 244 &&
            L.battery.d == 60,
        "voll: Akku-Knoten unten auf dem Ring");

  check(L.linkPv.x1 == 240 && L.linkPv.y1 == 124 && L.linkPv.x2 == 136 &&
            L.linkPv.y2 == 64,
        "voll: PV-Leitung schraeg nach links oben");
  check(L.linkGrid.x1 == 240 && L.linkGrid.y1 == 124 && L.linkGrid.x2 == 343 &&
            L.linkGrid.y2 == 64,
        "voll: Netz-Leitung schraeg nach rechts oben");
  check(L.linkBattery.x1 == 240 && L.linkBattery.y1 == 124 && L.linkBattery.x2 == 240 &&
            L.linkBattery.y2 == 244,
        "voll: Akku-Leitung senkrecht nach unten");

  // The three on the ring stand in the free corner at their node's lower side: the PV
  // to the LEFT, because the house is on its right. 6 = 136 - 70 - 60, where the 60 is
  // half the label: an x is the left edge, the offset is the number's middle.
  check(L.valPv.x == 6 && L.valPv.y == 102 && !L.valPv.gross,
        "voll: PV-Wert links unten am Knoten und klein");
  check(L.valHouse.x == 250 && L.valHouse.y == 162, "voll: Haus-Wert rechts der Akkuleitung");
  check(L.valGrid.x == 353 && L.valGrid.y == 102,
        "voll: Netz-Wert rechts unten am Knoten");
  check(L.valBattery.x == 250 && L.valBattery.y == 259,
        "voll: Akku-Wert rechts unten am Knoten");

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

// The ring and its three sectors. These are the parts that are new, and the checks
// are the properties rather than the pixels, because a property survives the next
// board while a pixel does not.
static void testTheRingAndItsSectors() {
  const FlowLayout L = flowLayoutFor(capsOf(true, true, true), ui());
  const UiFlow &f = ui().flow;

  check(L.ring.visible, "Ring: vorhanden, wenn Haus und Netz da sind");
  checkEq(L.ring.x, f.hubX, "Ring: Mittelpunkt ist der Mittelpunkt des Hauses");
  checkEq(L.ring.y, f.hubY, "Ring: und auf derselben Hoehe");
  checkEq(L.ring.rx, 120, "Ring: 120 px breit");
  checkEq(L.ring.ry, 120, "Ring: 120 px hoch - ein KREIS auf dem kleinen Display");
  checkEq(L.ring.rx, L.ring.ry, "Ring: und beide gleich, das ist der Unterschied");

  // ONE number decides where the nodes stand and where the sectors begin and end. 30
  // degrees above the horizontal puts one node every 120 degrees round the ring, so the
  // ring divides into three EQUAL arcs and the four nodes stand at the corners of a
  // triangle - which is the whole of what was asked for.
  checkEq((int)(kKnotenGrad * 10.0f + 0.5f), 300, "Knoten: 30 Grad ueber der Waagerechten");
  const float oben = L.keilOben.bisGrad - L.keilOben.vonGrad;
  const float akku = L.keilAkku.bisGrad - L.keilAkku.vonGrad;
  const float netz = L.keilNetz.bisGrad - L.keilNetz.vonGrad;
  checkEq((int)(oben + 0.5f), 120, "Keil oben: 120 Grad");
  checkEq((int)(akku + 0.5f), 120, "Keil Akku: 120 Grad");
  checkEq((int)(netz + 0.5f), 120, "Keil Netz: 120 Grad");
  checkEq((int)(oben + akku + netz + 0.5f), 360,
          "die drei Keile sind gleich und fuellen den Ring genau");

  // Each sector's two ends are ON the two nodes it joins. That is the whole property: a
  // lit sector reads as a way from one node to the next because both ends are on one.
  checkEq((int)(L.keilOben.vonGrad + 0.5f), 210, "Keil oben: beginnt am PV-Winkel 210");
  checkEq((int)(L.keilOben.bisGrad + 0.5f), 330, "Keil oben: endet am Netz-Winkel 330");
  checkEq((int)(L.keilAkku.vonGrad + 0.5f), 90, "Keil Akku: beginnt unten bei 90");
  checkEq((int)(L.keilAkku.bisGrad + 0.5f), 210, "Keil Akku: endet am PV-Winkel");
  checkEq((int)(L.keilNetz.vonGrad + 0.5f), 330, "Keil Netz: beginnt am Netz-Winkel");
  checkEq((int)(L.keilNetz.bisGrad + 0.5f), 450, "Keil Netz: endet unten bei 450");
  check(L.keilOben.visible && L.keilAkku.visible && L.keilNetz.visible,
        "Keile: alle drei da");

  // The four values stand in the free corner at their node's lower side, the same rule
  // the house has used all along. The PV's goes LEFT because the house is on its right -
  // which is the one place this does not follow the house.
  //
  // What has to hold is that no arc reaches any of the four. Half of "1,76 kW" is 27 px.
  const float textHalb = 27.0f;
  auto ringWeite = [&f](int16_t y) {
    const float tt = (float)(y - f.hubY) / (float)f.ry;
    return (int16_t)(f.rx * sqrtf(1.0f - tt * tt));
  };
  const int16_t pvTextRechts = (int16_t)(L.valPv.x + f.valW / 2 + textHalb);
  checkEq(ringWeite(L.valPv.y), 117, "Ring: auf der Hoehe des PV-Werts 117 px links aus der Mitte");
  checkEq(pvTextRechts, 93, "PV-Zahl: rechter Rand bei 93");
  checkEq(ringWeite(L.valPv.y) - pvTextRechts, 24,
          "Ring: 24 px Luft zwischen Bogen und PV-Zahl");
  const int16_t netzTextLinks = (int16_t)(L.valGrid.x + f.valW / 2 - textHalb);
  checkEq(netzTextLinks - (f.hubX + ringWeite(L.valGrid.y)), 29,
          "Ring: 29 px Luft zwischen Bogen und Netz-Zahl");
  checkEq((f.hubX + ringWeite(L.valHouse.y)) -
              (L.valHouse.x + f.valW / 2 + textHalb),
          16, "Ring: 16 px Luft zwischen Bogen und Haus-Zahl");
  // And the counter-check, so none of the three can pass by accident: 70 px the other
  // way, which is where the PV value was before it moved to the outside, and its right
  // edge lands 25 px INSIDE the arc.
  check(pvTextRechts + 70 + 25 > f.hubX - ringWeite(L.valPv.y),
        "Ring: die Platzierung nach aussen ist noetig, nicht Kosmetik");
  // The battery is the ring's lowest point, so its value has only the container's
  // bottom below it and 15 px is what fits.
  check(L.valBattery.y + 21 <= 280, "Akku-Zahl: endet ueber dem unteren Rand");
  checkEq(280 - (L.valBattery.y + 21), 0, "Akku-Zahl: genau am Rand, kein Platz mehr");

  // Without a battery the lower right sector grows over the whole lower half, so the ring
  // stays closed and the two still tile it.
  const FlowLayout oAkku = flowLayoutFor(capsOf(true, false, true), ui());
  check(oAkku.ring.visible && !oAkku.keilAkku.visible,
        "ohne Akku: der Ring bleibt, nur sein unterer linker Keil faellt weg");
  checkEq((int)(oAkku.keilNetz.bisGrad - oAkku.keilNetz.vonGrad + 0.5f), 240,
          "ohne Akku: der untere rechte Keil waechst auf 240 Grad");
  checkEq((int)(oAkku.keilOben.bisGrad - oAkku.keilOben.vonGrad +
                oAkku.keilNetz.bisGrad - oAkku.keilNetz.vonGrad + 0.5f),
          360, "ohne Akku: die beiden fuellen den Ring genau");
  // Without a grid there is no right-hand end for the lower right sector, and without a
  // house there is no centre: no ring at all.
  const FlowLayout oNetz = flowLayoutFor(capsOf(true, true, false), ui());
  check(!oNetz.ring.visible && !oNetz.keilOben.visible,
        "ohne Netzknoten: kein Ring, denn kein rechter Endpunkt");
  const FlowLayout oHaus = flowLayoutFor(capsOf(false, true, true), ui());
  check(!oHaus.ring.visible, "ohne Hauszaehler: kein Ring, denn keine Mitte");
}

// The one arrow on the ring, both directions.
//
// THE THIRD SECTOR CANNOT BE LIT IN THE EMULATOR. Its two states are the battery
// paying the meter and the meter paying the battery, and the simulator's balance
// charges the battery out of a surplus and discharges it into a shortfall - never the
// other way round. So this is the only test of it that there is.
static void testTheRingArrow() {
  const FlowLayout L = flowLayoutFor(capsOf(true, true, true), ui());
  const UiFlow &f = ui().flow;
  const float grad = L.keilNetz.pfeilGrad; // 360: the ring's right extreme
  checkEq((int)(grad + 0.5f), 360, "Pfeil: am rechten Extrem des Rings");

  int16_t x, y;
  float dreh;
  // On the ring, and only the turn says which way. At 360 the tangent of a circle runs
  // straight down, so the two directions are 90 and -90 degrees - and the glyph is a clean
  // arrow either way instead of a turned one.
  flowKeilPfeil(ui().flow, grad, true, &x, &y, &dreh);
  checkEq(x, 360, "Pfeil: auf dem Ring bei x = 360");
  checkEq(y, 124, "Pfeil: auf dem Ring bei y = 124, auf der Höhe des Hauses");
  check(dreh > 82.0f && dreh < 98.0f, "Pfeil Netz->Akku: 90 Grad, gerade nach unten");
  flowKeilPfeil(ui().flow, grad, false, &x, &y, &dreh);
  checkEq(x, 360, "Pfeil: dieselbe Stelle");
  checkEq(y, 124, "Pfeil: dieselbe Stelle");
  check(dreh < -82.0f && dreh > -98.0f, "Pfeil Akku->Netz: -90 Grad, gerade nach oben");
  // And it must not land on anything. It stands at the ring's right extreme, level with
  // the house and 62 px below the netz node, so the distances are what say so - not an
  // axis, because it is off both.
  auto abstand = [&](int16_t nx, int16_t ny) {
    const int dx = x - nx, dy = y - ny;
    return (int16_t)sqrtf((float)(dx * dx + dy * dy));
  };
  check(abstand(L.grid.x, L.grid.y) > f.sideD / 2, "Pfeil: 62 px vom Netzknoten, nicht auf ihm");
  check(abstand(L.battery.x, L.battery.y) > f.sideD / 2, "Pfeil: vom Akku-Knoten weg");
  // Level with the house, 120 px out - the ring's radius - so it clears the house by
  // 74 px rather than by a margin on one axis.
  check(abstand(L.house.x, L.house.y) > f.hubD / 2, "Pfeil: 120 px vom Haus, nicht auf ihm");
}

// No battery: the house stays the hub, the battery and its connector go.
static void testWithoutBattery() {
  const FlowLayout L = flowLayoutFor(capsOf(true, false, true), ui());
  check(L.pv.x == 136 && L.pv.y == 64 && L.pv.d == 60, "ohne Akku: PV bleibt aussen");
  check(L.house.x == 240 && L.house.y == 124 && L.house.d == 92,
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
  check(L.pv.y == 64, "ohne Akku: die PV bleibt, wo sie war");
}

// No household meter: the PV becomes the hub - big, in the middle, with the
// large value. This is the case a plain string inverter produces.
static void testWithoutHouseMeter() {
  const FlowLayout L = flowLayoutFor(capsOf(false, true, true), ui());
  check(!L.house.visible, "ohne Hauszaehler: Haus-Knoten weg");
  check(!L.valHouse.visible, "ohne Hauszaehler: Haus-Wert weg");

  check(L.pv.x == 240 && L.pv.y == 124 && L.pv.d == 92,
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
  check(L.valPv.x == ui().flow.hubX + ui().flow.hubValDx - ui().flow.valW / 2 &&
        L.valPv.y == ui().flow.hubY + ui().flow.hubValDy,
        "ohne Hauszaehler: mit Akku und Netz bleibt der Wert, wo der Hauswert war");
  check(L.grossPvIco, "ohne Hauszaehler: das PV-Icon wird wie das eines "
                      "Mittelpunkts skaliert");

  // The connectors now start at the PV node, which is the hub.
  check(!L.linkPv.visible,
        "ohne Hauszaehler: die PV hat keine Verbindung - sie ist die Mitte");
  check(L.linkGrid.x1 == 240 && L.linkGrid.y1 == 124 && L.linkGrid.x2 == 343,
        "ohne Hauszaehler: Netz haengt an der PV");
  check(L.linkBattery.x1 == 240 && L.linkBattery.y1 == 124 &&
            L.linkBattery.x2 == 240 && L.linkBattery.y2 == 244,
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
  // The band between circle and pill, measured where the value lives: in the container,
  // which sits ui().flow.flowY lower than the page the pills are on. Both ends of the band
  // in one coordinate system, and flowPillYInFlow() is the function that converts - the
  // test used to write the conversion out as (316 - 35), which is how a move of flowY goes
  // unnoticed and the value lands 3 px low.
  checkEq(flowAlleinValY(ui()), 232, "Band zwischen Kreis und Pille");
  checkEq(L.valPv.y,
          (int16_t)((flowAlleinY(ui()) + ui().flow.alleinD / 2 + flowPillYInFlow(ui()) -
                      34 / 2) / 2),
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
  testTheRingAndItsSectors();
  testTheRingArrow();
  testWithoutBattery();
  testWithoutHouseMeter();
  testOnlyGeneration();
  testUnknownCaps();
  testPillGeometry();

  printf("%s: %d Prüfungen, %d fehlgeschlagen\n",
         g_failed == 0 ? "OK" : "FEHLER", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}