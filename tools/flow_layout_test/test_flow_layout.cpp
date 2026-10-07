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

  check(L.pv.visible && L.pv.x == 139 && L.pv.y == 68 && L.pv.d == 60,
        "voll: PV-Knoten links oben auf dem Ring");
  check(L.house.visible && L.house.x == 240 && L.house.y == 126 && L.house.d == 70,
        "voll: Haus-Knoten in der Mitte des Rings");
  check(L.grid.visible && L.grid.x == 340 && L.grid.y == 68 && L.grid.d == 60,
        "voll: Netz-Knoten rechts oben auf dem Ring");
  check(L.battery.visible && L.battery.x == 240 && L.battery.y == 238 &&
            L.battery.d == 60,
        "voll: Akku-Knoten unten auf dem Ring");

  check(L.linkPv.x1 == 240 && L.linkPv.y1 == 126 && L.linkPv.x2 == 139 &&
            L.linkPv.y2 == 68,
        "voll: PV-Leitung schraeg nach links oben");
  check(L.linkGrid.x1 == 240 && L.linkGrid.y1 == 126 && L.linkGrid.x2 == 340 &&
            L.linkGrid.y2 == 68,
        "voll: Netz-Leitung schraeg nach rechts oben");
  check(L.linkBattery.x1 == 240 && L.linkBattery.y1 == 126 && L.linkBattery.x2 == 240 &&
            L.linkBattery.y2 == 238,
        "voll: Akku-Leitung senkrecht nach unten");

  // The three on the ring stand in the free corner at their node's lower side: the PV
  // to the LEFT, because the house is on its right. 26 = 139 - 53 - 60, where the 60 is
  // half the label: an x is the left edge, the offset is the number's middle.
  check(L.valPv.x == 26 && L.valPv.y == 97 && !L.valPv.gross,
        "voll: PV-Wert links unten am Knoten und klein");
  check(L.valHouse.x == 233 && L.valHouse.y == 155, "voll: Haus-Wert rechts der Akkuleitung");
  check(L.valGrid.x == 333 && L.valGrid.y == 97,
        "voll: Netz-Wert rechts unten am Knoten");
  check(L.valBattery.x == 240 && L.valBattery.y == 257,
        "voll: Akku-Wert rechts unten am Knoten");

  // The battery node's centre is where the two sector lines come OUT of its circle, so
  // they start in its middle. The ring curves away faster than a 60 px circle does, and
  // over the node's own radius that difference is the sagitta (30^2) / (2 * 116) = 3.9 px
  // below the ring's lowest point - which is 4 px, and at the ring's lowest point the
  // lines left the circle 4 px above its middle.
  {
    const UiFlow &uf = ui().flow;
    const float rr = (float)uf.sideD / 2.0f;
    const float sagitta = rr * rr / (2.0f * (float)uf.ry);
    check(fabsf((uf.hubY + uf.ry - sagitta) - (float)L.battery.y) < 1.5f,
          "voll: die Keillinie setzt in der Mitte des Akku-Kreises an");
  }

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
  checkEq(L.ring.rx, 116, "Ring: 116 px breit");
  checkEq(L.ring.ry, 116, "Ring: 116 px hoch - ein KREIS auf dem kleinen Display");
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
  checkEq(ringWeite(L.valPv.y), 112, "Ring: auf der Hoehe des PV-Werts 112 px links aus der Mitte");
  checkEq(pvTextRechts, 113, "PV-Zahl: rechter Rand bei 113");
  // The gap is the BOG's x (a position on the page) less the text's right edge (also a
  // position), not the offset from the middle less the same edge: that mixed an offset
  // with a coordinate and only came out right while both were the same number.
  checkEq((f.hubX - ringWeite(L.valPv.y)) - pvTextRechts, 15,
          "Ring: 15 px Luft zwischen Bogen und PV-Zahl");
  const int16_t netzTextLinks = (int16_t)(L.valGrid.x + f.valW / 2 - textHalb);
  checkEq(netzTextLinks - (f.hubX + ringWeite(L.valGrid.y)), 14,
          "Ring: 14 px Luft zwischen Bogen und Netz-Zahl");
  checkEq((f.hubX + ringWeite(L.valHouse.y)) -
              (L.valHouse.x + f.valW / 2 + textHalb),
          32, "Ring: 32 px Luft zwischen Bogen und Haus-Zahl");
  // The house's value has a second circle to clear, its own node - and at the hub's
  // diameter it had when the value's offsets were set, it did not: at 29 px below the
  // centre an 80 px circle is 27.6 px wide, and the text's left edge was at 266 against
  // a rim at 267.6. A radius is the only thing between the two numbers.
  {
    const float dy = (float)(L.valHouse.y - L.house.y);
    const float halb =
        sqrtf((float)(f.hubD / 2) * (float)(f.hubD / 2) - dy * dy);
    checkEq((long)((L.valHouse.x + f.valW / 2 - textHalb) -
                   (L.house.x + halb)),
            6, "Haus-Zahl: 6 px Luft zum eigenen Kreis, nicht auf ihm");
  }
  // And the counter-check, so none of the three can pass by accident: 53 px the other
  // way, which is where the PV value would be on the inside, and its right edge lands
  // 40 px INSIDE the arc.
  check(pvTextRechts + 53 + 40 > f.hubX - ringWeite(L.valPv.y),
        "Ring: die Platzierung nach aussen ist noetig, nicht Kosmetik");
  // The battery is the ring's lowest point, so its value has only the container's
  // bottom below it - which is why the container is 288 and not 280: at 280 the line
  // this hangs on would end 4 px outside it. It ends 6 px higher than it did, because
  // the node it hangs under moved 6 px up to where the sector lines leave it.
  check(L.valBattery.y + 21 <= f.flowH, "Akku-Zahl: endet ueber dem unteren Rand");
  checkEq(f.flowH - (L.valBattery.y + 21), 10, "Akku-Zahl: 10 px bis zum unteren Rand");

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

// The arrowhead on the ring: three points, tip ON the ring at the sector's middle, the
// two arms out in the open.
//
// THE THIRD SECTOR CANNOT BE LIT IN THE EMULATOR. Its two states are the battery
// paying the meter and the meter paying the battery, and the simulator's balance
// charges the battery out of a surplus and discharges it into a shortfall - never the
// other way round. So this is the only test of it that there is.
static void testTheRingArrow() {
  const FlowLayout L = flowLayoutFor(capsOf(true, true, true), ui());
  const UiFlow &f = ui().flow;
  checkEq((int)(L.keilNetz.pfeilGrad + 0.5f), 390,
          "Pfeil: in der Mitte des rechten unteren Keils");

  const FlowPfeil fp = flowKeilPfeil(f, L.keilNetz.pfeilGrad, true);
  checkEq(fp.spitzeX, 340, "Spitze: auf dem Ring bei x = 340");
  checkEq(fp.spitzeY, 184, "Spitze: auf dem Ring bei y = 184, unter dem Netz-Knoten");

  // ON the ring, which is the whole of the first half of the request. Measured on the
  // ellipse and not as a distance, because the ring is 240 x 116 on the wide board and
  // a circle's radius would be wrong there.
  const float ex = (float)(fp.spitzeX - f.hubX) / (float)f.rx;
  const float ey = (float)(fp.spitzeY - f.hubY) / (float)f.ry;
  const float abweichung = fabsf(sqrtf(ex * ex + ey * ey) - 1.0f);
  check(abweichung < 0.01f, "Spitze: liegt auf dem Ring, nicht daneben");

  // THE TWO PROPERTIES THE REQUEST NAMES. The tip stands on the ring - see above - and
  // the two barbs behind it are the same distance from the ring and on OPPOSITE sides of
  // it. Both were wrong before: the arms were mirrored about the RADIUS, so both ends
  // stood OUTSIDE the line, and two ends on one side of a line is not an arrowhead but
  // a tick that leans.
  //
  // Measured against the radius out of the ellipse at that angle, which is the
  // perpendicular to the line - and not against the sector's bisector: on an ellipse those
  // two differ by up to 8 degrees, which the 116 px circle of the 480 hides and the
  // 240 x 116 of the wide board would not.
  const float g = L.keilNetz.pfeilGrad * 0.0174532925f;
  const float nx = (float)f.ry * cosf(g), ny = (float)f.rx * sinf(g);
  const float nl = sqrtf(nx * nx + ny * ny);
  float abstand[2];
  for (int i = 0; i < 2; i++) {
    const float ax = (float)(fp.armX[i] - fp.spitzeX);
    const float ay = (float)(fp.armY[i] - fp.spitzeY);
    check(fabsf(sqrtf(ax * ax + ay * ay) - kPfeilLaenge) < 0.8f, "Arm: 13 px lang");
    abstand[i] = (ax * nx + ay * ny) / nl;
  }
  // SIGNED, and the sign is the point: one end has to be outside the ring and the other
  // inside it. 0.8 px of slack on the magnitude, and that is the pixel grid talking - an
  // arm 13 px long is rounded to whole pixels at each end, which is up to 0.9 px of
  // length. A check without the slack fails on a mark that is right to the eye, and a
  // check that has to be loosened later is one nobody reads.
  check(fabsf(abstand[0]) > 5.0f && fabsf(abstand[1]) > 5.0f,
        "Arme: beide 6.5 px von der Linie");
  check((abstand[0] > 0.0f) != (abstand[1] > 0.0f),
        "Arme: auf verschiedenen Seiten der Linie, nicht beide ausserhalb");
  check(fabsf(fabsf(abstand[0]) - fabsf(abstand[1])) < 0.8f,
        "Arme: gleicher Abstand zur Linie auf beiden Seiten");
  // And both of them BEHIND the tip: the wedge points the way the power runs along the
  // ring, which is what makes the barbs trailing rather than leading.
  const float tx = -(float)f.rx * sinf(g), ty = (float)f.ry * cosf(g);
  const float tl = sqrtf(tx * tx + ty * ty);
  for (int i = 0; i < 2; i++) {
    const float ax = (float)(fp.armX[i] - fp.spitzeX);
    const float ay = (float)(fp.armY[i] - fp.spitzeY);
    check((ax * tx + ay * ty) / tl < -8.0f, "Arm: liegt hinter der Spitze");
  }
  // On the top sector the radius runs straight down the pixel grid, and there the two
  // ends have to be EXACTLY as far from the tip as each other - no slack, because there
  // is no rounding to hide behind: the offsets are +-6.5 and a truncation would make them
  // 6 and 7.
  const FlowPfeil fo0 = flowKeilPfeil(f, L.keilOben.pfeilGrad, true);
  check(abs((int)fo0.armX[0] - fo0.spitzeX) == abs((int)fo0.armX[1] - fo0.spitzeX) &&
            abs((int)fo0.armY[0] - fo0.spitzeY) == abs((int)fo0.armY[1] - fo0.spitzeY),
        "Pfeil oben: beide Enden gleich weit von der Spitze");


  // And nothing may be in the way. The tip stands at the ring's lower right, 60 px
  // below the netz node and 116 px out from the middle, so the distances are what say
  // so - not an axis, because it is on none.
  auto knotenAbstand = [&](int16_t nx2, int16_t ny2) {
    const int dx = fp.spitzeX - nx2, dy = fp.spitzeY - ny2;
    return (int16_t)sqrtf((float)(dx * dx + dy * dy));
  };
  check(knotenAbstand(L.grid.x, L.grid.y) > f.sideD / 2,
        "Spitze: 60 px vom Netzknoten, nicht auf ihm");
  check(knotenAbstand(L.battery.x, L.battery.y) > f.sideD / 2, "Spitze: vom Akku-Knoten weg");
  // 116 px out from the middle - the ring's radius - so it clears the house by 81 px
  // rather than by a margin on one axis.
  check(knotenAbstand(L.house.x, L.house.y) > f.hubD / 2, "Spitze: 116 px vom Haus, nicht auf ihm");

  // The top sector's arms reach UP, out of the ring, and that is the one place where
  // there is something to hit: the container's top edge is 10 px above the ring's
  // highest point and the arms are 9 long, so this is the check that says the mark fits.
  checkEq(fo0.spitzeY, 10, "Spitze oben: 10 px unter der Oberkante des Containers");
  check(fo0.armY[0] > 0 && fo0.armY[1] > 0,
        "Pfeil oben: beide Arme bleiben im Container");
}

// No battery: the house stays the hub, the battery and its connector go.
static void testWithoutBattery() {
  const FlowLayout L = flowLayoutFor(capsOf(true, false, true), ui());
  check(L.pv.x == 139 && L.pv.y == 68 && L.pv.d == 60, "ohne Akku: PV bleibt aussen");
  check(L.house.x == 240 && L.house.y == 126 && L.house.d == 70,
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
  check(L.pv.y == 68, "ohne Akku: die PV bleibt, wo sie war");
}

// No household meter: the PV becomes the hub - big, in the middle, with the
// large value. This is the case a plain string inverter produces.
static void testWithoutHouseMeter() {
  const FlowLayout L = flowLayoutFor(capsOf(false, true, true), ui());
  check(!L.house.visible, "ohne Hauszaehler: Haus-Knoten weg");
  check(!L.valHouse.visible, "ohne Hauszaehler: Haus-Wert weg");

  check(L.pv.x == 240 && L.pv.y == 126 && L.pv.d == 70,
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
  check(L.linkGrid.x1 == 240 && L.linkGrid.y1 == 126 && L.linkGrid.x2 == 340,
        "ohne Hauszaehler: Netz haengt an der PV");
  check(L.linkBattery.x1 == 240 && L.linkBattery.y1 == 126 &&
            L.linkBattery.x2 == 240 && L.linkBattery.y2 == 238,
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
  checkEq(flowAlleinValY(ui()), 238, "Band zwischen Kreis und Pille");
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