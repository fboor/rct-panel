// Where the flow diagram's nodes go, for one device family.
//
// The diagram used to be drawn once, with fixed positions, from what an RCT
// Power reports. That made it wrong for every other device: a plain string
// inverter behind an OpenInverterGateway has no household meter and no battery,
// and the page still drew a house with a battery under it and a household figure
// that was a zero - a number the display showed as if it had been measured.
//
// One rule produces every layout: the hub is the house if there is one, and the
// PV generation if there is not. The PV is the node that always exists, because
// a device that reports no generation at all is not what this panel is for.
//
// Header-only and free of LVGL, like Theme.h and DataStatus.h: what a layout is
// is a handful of integers, and getting one of them wrong is visible on a wall
// rather than in a log. tools/flow_layout_test checks the six cases.
//
// Coordinates are the ones the fixed layout already used, so an RCT Power comes
// out pixel-identical to what the panel showed before - the point of a refactor
// like this is that the common case does not move.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_GUI_FLOWLAYOUT_H
#define RCT_GUI_FLOWLAYOUT_H

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "../device/DeviceCaps.h"
#include "../ui/UiLayout.h"

// A node of the diagram: where its centre is, how big, and whether it is there
// at all. A node that is not drawn still has the other values set, so a caller
// that asks anyway gets a position rather than a zero it would have to guess.
struct FlowNode {
  bool visible;
  int16_t x, y; // centre
  int16_t d;    // diameter
};

// A connector between two nodes. Coordinates are the node centres, which is
// what lv_line_set_points() wants and what the arrow placement measures from.
struct FlowLink {
  bool visible;
  int16_t x1, y1, x2, y2;
};

// One of the three pills under the diagram.
enum FlowPill { FLOW_PILL_PRODUCTION = 0, FLOW_PILL_HOUSE, FLOW_PILL_BATTERY };

// One sector of the ring: a way between two nodes that does not pass the house.
//
// Angles are in degrees the way LVGL counts them - from 3 o'clock, clockwise - and
// run from vonGrad to bisGrad the short way round, which for all three sectors is
// the way the picture turns.
//
struct FlowArc {
  bool visible;
  float vonGrad, bisGrad;
  // Where this sector's ARROWHEAD stands, in degrees - and it is the sector's MIDDLE,
  // half way between its two ends, which is where the eye looks for a direction mark.
  //
  // It was once off the middle, on the axes at 300/180/360, because a glyph turned there
  // is a multiple of 90 and still looks like an arrow rather than a hook. That bought a
  // cleaner glyph and lost the one thing that matters: 300 degrees says nothing about the
  // middle of that sector. The mark is not a glyph any more (see flowKeilPfeil), so the
  // turn is not a question at all - only the place is.
  float pfeilGrad;
  int16_t x, y;   // the ellipse's centre
  int16_t rx, ry; // its radii
};

// A value under a node, or on the ring.
//
// x and y are the label's left edge and top edge - lv_obj_set_pos() on a label
// with a fixed width positions its corner, not its middle, which is the case all
// four values were drawn for until the one-node layout wanted its number in the
// middle of the band under the circle. So a value can also say that its y is the
// middle, and the width is stated rather than left at the label's own 120 px: the
// 36 px font needs more than that ("1,23 kW" is 133 px, and a label too narrow
// for its text wraps it onto a second line).
struct FlowValue {
  bool visible;
  int16_t x, y;    // left edge, or middle vertically when centreY
  int16_t w;       // the label's width; 0 leaves it as it is
  bool centreY;    // y is the middle of the value, not its top edge
  bool gross;      // the 36 px font, for the value of the big PV
};

// Where the ring's three sectors begin and end, and where their figures stand.
//
// The cut between the top sector and the two lower ones sits 0.2 radii above the
// centre - the design's 40 % line, expressed on an ellipse instead of a square - so
// its ends lie at asin(0.2) = 11.54 degrees. The three sectors are then 156.9, 101.5
// and 101.5 degrees and they tile the ring exactly: 360.0, no gap and no overlap at
// any size. Every sector's two ends land on two of the three outer nodes, which is
// what makes a lit sector read as a way from one node to another rather than as a
// piece of decoration.
//
// The figures do not stand at the sectors' middles. At 140 degrees the lower left
// one would be 8 px from the PV node and 17 px from the PV's own value, and at 125
// degrees both are clear. Same on the other side, at 55 instead of 40.
// Where the three outer nodes stand on the ring, and where the sectors begin and end,
// in degrees counted the way the point generator counts them: from 3 o'clock, clockwise,
// and a point at grad G stands at (hubX + rx*cos G, hubY + ry*sin G).
//
// So "left" is cos < 0, i.e. 90..270, and "above the middle" is sin < 0, i.e. 180..360.
// Both together for the upper left quadrant is 180..270, which is where the PV is:
//   PV      180 + 30 = 210
//   Netz    360 - 30 = 330
//   Akku    360 + 90 = 450
// and a sector runs from one of those to the next, upwards through the top.
//
// THIRTY is what makes the three sectors EQUAL: one node every 120 degrees around the
// ring, so the ring divides into three arcs of 120 and the four nodes stand at the corners
// of a triangle. The design has them nearer the horizontal - 0.22 radii, which is 12.7
// degrees - and it is the one thing of it this does not copy, because with the nodes that
// far out and a 480 px page the ring is 3.3 : 1 and reads as pulled apart rather than as
// a circle.
//
// ONE number decides the nodes AND the sectors, which is the point. Before this there
// were two rules, a 40 % cut line and the node angles, and they could disagree.
//
// Writing 167.3 here instead of 192.7 puts the PV node's angle on the wrong side of the
// centre line - it would be below the middle rather than above it, and every sector's
// end would be 25 degrees away from the node it is supposed to start on.
static const float kKnotenGrad = 30.0f;
static const float kGradPv = 180.0f + kKnotenGrad;   // 210
static const float kGradNetz = 360.0f - kKnotenGrad; // 330
static const float kGradAkku = 450.0f;               // straight down, unwrapped

// Everything the overview's diagram needs to know, and nothing about how it is
// drawn. guiUpdateFlow() applies this; it is the only thing that talks to LVGL.
struct FlowLayout {
  FlowNode pv, house, grid, battery;
  FlowLink linkPv, linkGrid, linkBattery;
  // The three direct ways. Named for the way, not for the sector: keilOben is the panels
  // into the grid, keilAkku the panels into the battery, keilNetz the battery and the
  // grid across each other.
  FlowArc keilOben, keilAkku, keilNetz;
  FlowValue valPv, valHouse, valGrid, valBattery;
  // The ring's own line, without a sector: shown whenever the ring has one, so the
  // diagram has its shape even when nothing is moving.
  FlowArc ring;
  uint8_t pills;        // bitmask over FlowPill
  int16_t pillX[3];     // left edge of the pills that exist
  bool batterySoc;      // the percentage inside the battery node
  bool islandMark;      // the warning triangle has a connector to sit on
  bool grossPvIco;      // the PV icon is scaled like the hub's
  bool pvIcoNative;     // the PV icon has a font of its own and is drawn 1:1
};

// The layout for one device family.
//
// The numbers live in src/ui/UiLayout.h, because they are the board's and not this
// file's: they were constants here until stage 5 of the hardware plan moved them,
// and the reason for moving them is that a second display cannot be laid out from a
// list of literals without a second copy of the file.
//
// Four positions are DERIVED rather than stated, and they are derived here so that
// every board gets them for free: the one-node circle's centre, the pills' row in
// the container's coordinates, and the position and width of the value under a PV
// that is the only node on the page.
static inline int16_t flowAlleinY(const UiLayout &u) {
  return (int16_t)(u.flow.hubY + u.flow.alleinDY);
}

// The pill row is on the page, the nodes are in the container, so the band between
// the circle and the pill can only be measured in the container. That is what this
// offset is for, and it is why the container's y has to be known here.
static inline int16_t flowPillYInFlow(const UiLayout &u) {
  return (int16_t)(u.flow.pillY - u.flow.flowY);
}

// Centred horizontally: a 200 px label whose text is centred in itself has its left
// edge at hubX - 100, and 200 px is what the 36 px font needs for "1,23 kW".
static inline int16_t flowAlleinValX(const UiLayout &u) {
  return (int16_t)(u.flow.hubX - u.flow.alleinValW / 2);
}

// In the middle of the empty band between the circle above and the pill below. Both
// ends of the band are in the container, where this value lives too: the circle's
// lower edge and the pill's upper edge, halved. The band is 69 px for a number of
// 45 - the tightest it gets while the circle still fits, which is why the circle
// cannot grow further without taking some of the number's air away.
static inline int16_t flowAlleinValY(const UiLayout &u) {
  const int16_t kreisEnde = (int16_t)(flowAlleinY(u) + u.flow.alleinD / 2);
  // What closes the band under the circle is whatever is BELOW THE DIAGRAM, and that is
  // not the same thing on the two boards: the pill row on the narrow one, and the
  // container's bottom edge on the wide one, where the pills are a column BESIDE the
  // drawing. Their top edge says nothing about how far the drawing may reach, and with
  // it the band came out 30 px tall - too little for a 45 px number, which is where the
  // one-node value would have ended up.
  const int16_t unten = (u.flow.pillDY != 0)
                            ? u.flow.flowH
                            : (int16_t)(flowPillYInFlow(u) - u.flow.pillH / 2);
  return (int16_t)((kreisEnde + unten) / 2);
}

// Where a stacked pill column starts. The row on the narrow board leaves 6 px because
// three pills of 152 px and two gaps of 6 fill 480 px exactly and the centring works out
// to 6; the stack on the wide one has no such arithmetic to fall out of, so it is a
// number of its own: 11 px, 5 more than the row's, which is where the column was asked
// to stand.
static const int16_t kPilleRand = 11;

// The layout for one device family. caps.isKnown() false means "nothing has
// answered yet": the full layout is drawn, because an RCT Power is what most of
// them are and a diagram that rearranges itself ten seconds after every boot is
// worse than one that starts out right.
static inline FlowLayout flowLayoutFor(const DeviceCaps &caps,
                                       const UiLayout &ui) {
  const UiFlow &f = ui.flow;
  FlowLayout L;
  L.batterySoc = false;
  L.islandMark = true;

  // What the device can report - or, until it has answered once, everything:
  // an RCT Power is what most of them are, and a diagram that rearranges itself
  // ten seconds after every boot is worse than one that starts out right.
  bool kenneHaus = caps.houseMeter || !caps.isKnown();
  bool kenneAkku = caps.battery || !caps.isKnown();
  bool kenneNetz = caps.gridMeter || !caps.isKnown();

  // The hub, and which node it is.
  const bool hubIstPv = !kenneHaus;
  L.house.visible = kenneHaus;
  L.house.x = f.hubX;
  L.house.y = f.hubY;
  L.house.d = f.hubD;

  // The PV node: an outer node of the row, the hub when there is no house, and on
  // its own - alone, 160 px and 8 px lower - when there is neither a house nor
  // anything else below it. Decided once, because both its size and its place
  // follow from it and a second, later test could read a d that is not set yet.
  const bool pvAllein = hubIstPv && !kenneAkku && !kenneNetz;
  L.pv.visible = true;
  // On the ring, 12.7 degrees above its horizontal, unless the PV is the hub - then it
  // is the hub and has no place on it.
  const float kc = kKnotenGrad * 0.0174532925f;
  const int16_t seiteY = (int16_t)(f.hubY - f.ry * sinf(kc));
  L.pv.x = hubIstPv ? f.hubX : (int16_t)(f.hubX - f.rx * cosf(kc));
  L.pv.y = hubIstPv ? (pvAllein ? flowAlleinY(ui) : f.hubY) : seiteY;
  L.pv.d = hubIstPv ? (pvAllein ? f.alleinD : f.hubD) : f.sideD;

  L.grid.visible = kenneNetz;
  L.grid.x = (int16_t)(f.hubX + f.rx * cosf(kc));
  L.grid.y = seiteY;
  L.grid.d = f.sideD;

  L.battery.visible = kenneAkku;
  L.battery.x = f.hubX;
  L.battery.y = f.batDY;
  L.battery.d = f.sideD;

  // The connectors all start at the hub's centre: from the house to the three
  // around it, or from the PV node when there is no house.
  const int16_t hubX = f.hubX, hubY = f.hubY;
  // A connector only exists between two nodes. When the PV is the hub there is
  // nothing to connect it to, so the line is not drawn and the arrow on it goes
  // with it - an arrow on a link of zero length is a dash with no meaning, and it
  // was the one leftover of the layout that nobody had looked at.
  L.linkPv.visible = !hubIstPv;
  L.linkPv.x1 = hubX;
  L.linkPv.y1 = hubY;
  L.linkPv.x2 = L.pv.x;
  L.linkPv.y2 = L.pv.y;
  L.linkGrid.visible = kenneNetz;
  L.linkGrid.x1 = hubX;
  L.linkGrid.y1 = hubY;
  L.linkGrid.x2 = L.grid.x;
  L.linkGrid.y2 = L.grid.y;
  L.linkBattery.visible = kenneAkku;
  L.linkBattery.x1 = hubX;
  L.linkBattery.y1 = hubY;
  L.linkBattery.x2 = f.hubX;
  L.linkBattery.y2 = f.batDY;

  // The values. The hub's value keeps the place it had as the house's: right of
  // the vertical line, so the line does not run through the digits - and free in
  // a layout without a house.
  // Every value's x is its label's LEFT edge, and every offset is stated as the MIDDLE of
  // the number relative to its node - so "move it" is one number everywhere and the label
  // width does not have to be re-read to work out where the text lands.
  L.valHouse.visible = kenneHaus;
  L.valHouse.x = (int16_t)(f.hubX + f.hubValDx - f.valW / 2);
  L.valHouse.y = (int16_t)(f.hubY + f.hubValDy);
  L.valHouse.w = f.valW;
  L.valHouse.centreY = false;
  L.valHouse.gross = false;

  L.valPv.visible = true;
  L.valPv.gross = hubIstPv;
  L.valPv.w = hubIstPv ? f.alleinValW : f.valW;
  L.valPv.centreY = false;
  if (hubIstPv && kenneAkku) {
    // A battery hangs below the PV node, and with it the vertical line - so the
    // value keeps the place the house's value had: right of that line, so the
    // line does not run through the digits. It is not centred here: there is
    // still a node below, and the number belongs to the one above.
      L.valPv.x = (int16_t)(f.hubX + f.hubValDx - f.valW / 2);
    L.valPv.y = (int16_t)(f.hubY + f.hubValDy);
  } else if (hubIstPv) {
    // Nothing below the node and nothing above but the circle: the value is the
    // content of the page, so it goes in the middle of the band between the two -
    // which is the only thing that makes it belong to the circle rather than to
    // the pill. Its y is therefore a middle, not a top edge.
    L.valPv.x = flowAlleinValX(ui);
    L.valPv.y = flowAlleinValY(ui);
    L.valPv.centreY = true;
  } else {
      // 70 px to the LEFT of the node, not to the right: the house is on the PV
      // node's right, and the right is where the house's number goes. The grid's
      // is mirrored, so the two read as a pair, each between its node and the edge.
      L.valPv.x = (int16_t)(L.pv.x + f.pvValDx - f.valW / 2);
      L.valPv.y = (int16_t)(L.pv.y + f.pvValDy);
    }
    L.grossPvIco = hubIstPv;
    // In the one-node layout the icon is drawn from its own font at its own size;
    // everywhere else it is the 28 px font, scaled like the hub's or left alone.
  // A 28 px glyph at 480 % is a blur, and that layout is the whole page.
  L.pvIcoNative = pvAllein;

  L.valGrid.visible = kenneNetz;
  L.valGrid.x = (int16_t)(L.grid.x + f.gridValDx - f.valW / 2);
  L.valGrid.y = (int16_t)(L.grid.y + f.gridValDy);
  L.valGrid.w = f.valW;
  L.valGrid.centreY = false;
  L.valGrid.gross = false;

  L.valBattery.visible = kenneAkku;
  L.valBattery.x = (int16_t)(L.battery.x + f.batValDx - f.valW / 2);
  L.valBattery.y = (int16_t)(L.battery.y + f.batValDy);
  L.valBattery.w = f.valW;
  L.valBattery.centreY = false;
  L.valBattery.gross = false;

  L.batterySoc = kenneAkku;
  // The triangle marks the grid link, so it only exists where there is one.
  L.islandMark = kenneNetz;

  // --- the ring and its three sectors ---
  //
  // All of it needs a house and a grid: the ring is drawn through the outer nodes, so
  // without a grid there is no right-hand end for the lower right sector to start at
  // and without a house there is no centre to be drawn around. What is left without a
  // battery is the top sector, which is the one way that does not touch it.
  //
  // Not: a sector per pair of nodes. The panels have no way in, so a battery on its own
  // is missing two of the three and shows one, and the arc for a way nobody can measure
  // would be a picture of a guess.
  const bool ringDa = kenneHaus && kenneNetz;
  L.ring.visible = ringDa;
  L.ring.vonGrad = 0.0f;
  L.ring.bisGrad = 360.0f;
  L.ring.x = f.hubX;
  L.ring.y = f.hubY;
  L.ring.rx = f.rx;
  L.ring.ry = f.ry;

  // The three sectors. Each one's two ends are on two of the outer nodes: the top one
  // from the PV round over the head to the grid, the lower left from the battery round to
  // the PV, the lower right from the grid round to the battery. Which is which is not a
  // choice: the nodes are at the three ends of kKnotenGrad and of 90, so the sectors are
  // what is left between them.
  auto keil = [&f](float von, float bis, float pfeil, bool sichtbar) {
    FlowArc a;
    a.visible = sichtbar;
    a.vonGrad = von;
    a.bisGrad = bis;
    a.pfeilGrad = pfeil;
    a.x = f.hubX;
    a.y = f.hubY;
    a.rx = f.rx;
    a.ry = f.ry;
    return a;
  };
  // The three arrowheads, each on its sector's middle - half way between the two ends,
  // which is where the eye looks for a direction mark and what makes a mark on an arc an
  // arrow rather than a decoration. Where the mark points is flowKeilPfeil()'s business
  // and is the same for all three: at the ring, from outside.
  //
  //   oben  270 Grad: the ring's highest point, and the 10 px the mark's 9 px arms need
  //         above the arc are why the ring is 116 and not 120.
  //   Akku  150 Grad: between the battery at 90 and the PV at 210.
  //   Netz  390 Grad: between the meter at 330 and the battery at 90.
  L.keilOben = keil(kGradPv, kGradNetz, 270.0f, ringDa);
  L.keilAkku = keil(kGradAkku - 360.0f, kGradPv, 150.0f, ringDa && kenneAkku);
  // Without a battery the lower right sector grows over the whole lower half, so the ring
  // stays closed: left out, it would stop in mid-air under where the battery node used
  // to be, on a page with nothing there. Its middle is then 360 of a 240 degree sector.
  L.keilNetz =
      keil(kGradNetz, kenneAkku ? kGradAkku : kGradPv + 360.0f,
           kenneAkku ? 390.0f : 360.0f, ringDa);

  // One pill per quantity that is drawn: a pill for something the diagram does
  // not show would be saying something about a measurement that is not being
  // taken. "keine Batterie" was true but redundant next to a diagram without a
  // battery, and the consumption pill's "nothing is happening" was false - there
  // was nothing being measured at all.
  uint8_t p = 0;
  if (true) { // the PV always exists
    p |= 1u << FLOW_PILL_PRODUCTION;
  }
  if (kenneHaus) {
    p |= 1u << FLOW_PILL_HOUSE;
  }
  if (kenneAkku) {
    p |= 1u << FLOW_PILL_BATTERY;
  }
  L.pills = p;

  int n = 0;
  for (int i = 0; i < 3; i++) {
    if (p & (1u << i)) {
      n++;
    }
  }
  // A ROW on the board whose width is the pills': centred as a group, because a single
  // pill at the left edge with 300 px of empty space beside it looks broken. A STACK on
  // the wide one, where the column they stand in is a column and all three are at its left
  // edge - a centred stack would be three rows of nothing in the middle of the page.
  const bool stapel = f.pillDY != 0;
  const int16_t gesamt =
      stapel ? f.pillW
             : (int16_t)(n * f.pillW + (n > 0 ? (n - 1) * f.pillGap : 0));
  const int16_t start =
      stapel ? kPilleRand : (int16_t)((f.pageW - gesamt) / 2);
  int k = 0;
  for (int i = 0; i < 3; i++) {
    if (p & (1u << i)) {
      L.pillX[i] = (int16_t)(stapel ? start
                                    : start + k * (f.pillW + f.pillGap));
      k++;
    }
  }
  return L;
}

// The arrow on a connector sits on its midpoint, and the island triangle 25 px
// above the grid link - the same places as before, computed from the layout so
// they follow it.
// A line's place and its middle, in the container's coordinates.
static inline void flowLinkMidpoint(const FlowLink &l, int16_t *cx, int16_t *cy) {
  *cx = (int16_t)((l.x1 + l.x2) / 2);
  *cy = (int16_t)((l.y1 + l.y2) / 2);
}

// The arrowhead on the ring: three points for ONE polyline, the tip in the middle and
// the two arm ends behind it.
//
// WHY LINES AND NOT A GLYPH. It was LV_SYMBOL_RIGHT, turned onto the arc. A font's
// arrowhead is a 4 px solid stroke and cannot be made thinner, and its ink does not sit
// where its own label box says: LVGL turns a label about its TOP LEFT CORNER by default,
// so an arrowhead placed at the sector's middle turned up to 10 px away from it - and the
// same default had to be corrected by hand on the two node icons for exactly this reason.
// Drawing the three points here puts the tip on the arc to the pixel and leaves the
// stroke width a number of its own.
//
// WHY THE ARMS STRADDLE THE ARC. The tip is ON it and the two ends are the same distance
// from it - one on each side. They were both OUTSIDE it, mirrored about the radius, and
// two ends on the same side of the line is not an arrowhead: it is a tick that leans.
// So the two arms are mirrored about the ARC instead, both leaning the same way ALONG
// it and behind the tip, which is what an arrowhead is: a point and two barbs trailing
// away from it. The bisector of the two arms therefore lies along the ring, and laeuftAuf
// is the direction it points - which is also why the direction is a parameter again: a
// mark that lies along the line can carry it, and one across it could not.
struct FlowPfeil {
  int16_t spitzeX, spitzeY;
  int16_t armX[2], armY[2];
};

// 13 px of arm at 30 degrees off the arc on each side, which is the usual arrowhead: the
// two barbs 60 degrees apart, and each end 6.5 px from the line the tip is on. The arm
// has to be long enough for a barb to get 4 px clear of the 5 px arc it lies across - at
// 9 px the barbs ended inside the arc's own thickness and there was nothing to see.
static const float kPfeilLaenge = 13.0f;

static inline FlowPfeil flowKeilPfeil(const UiFlow &f, float grad, bool laeuftAuf) {
  const float t = grad * 0.0174532925f;
  const float rx = (float)f.rx, ry = (float)f.ry;
  const float st = sinf(t), ct = cosf(t);
  FlowPfeil p;
  p.spitzeX = (int16_t)(f.hubX + rx * ct);
  p.spitzeY = (int16_t)(f.hubY + ry * st);
  // The tangent at that angle - the direction the power runs along the ring - and the
  // radius out of the ellipse at the same place, perpendicular to it by construction.
  const float tx = -rx * st, ty = ry * ct;
  const float nx = ry * ct, ny = rx * st;
  const float tl = sqrtf(tx * tx + ty * ty), nl = sqrtf(nx * nx + ny * ny);
  const float c = 0.8660254f, s = 0.5f; // cos und sin von 30 Grad
  const float lauf = laeuftAuf ? 1.0f : -1.0f;
  for (int i = 0; i < 2; i++) {
    const float seite = (i == 0) ? 1.0f : -1.0f;
    // Both arms lean BACKWARD along the arc, away from where the power goes; the plus and
    // the minus are the two sides of the line, and nothing else separates them.
    const float dx = kPfeilLaenge * (seite * s * nx / nl - c * lauf * tx / tl);
    const float dy = kPfeilLaenge * (seite * s * ny / nl - c * lauf * ty / tl);
    // The offset is rounded and then added, and not the sum: (int16)(240 + 4.5) is 244
    // and (int16)(240 - 4.5) is 235, which leaves the two arm ends 4 px and 5 px from
    // the tip - one pixel apart along the mark's own axis, on the one thing the two ends
    // of a chevron must not be. lroundf rounds halves away from zero, so +4.5 and -4.5
    // become +5 and -5 and the ends are 10 px apart either way.
    p.armX[i] = p.spitzeX + (int16_t)lroundf(dx);
    p.armY[i] = p.spitzeY + (int16_t)lroundf(dy);
  }
  return p;
}

// The connector's arrowhead: the same mark as the ring's, on the line instead of the
// arc, and the same 2 px of stroke - a 4 px glyph on a 2 px line says two different
// things at once.
//
// The tip stands on the connector at its middle and the two barbs trail behind it, so the
// mark straddles the line the way the ring's straddles the arc. It always points TOWARDS
// THE HUB: a connector is only lit when the power runs that way, so the arrow has one
// direction to be and it is the one the link is stored against - the links run hub to
// node, and the power runs the other way round.
static inline FlowPfeil flowLinkPfeil(const FlowLink &l) {
  const float dx = (float)(l.x1 - l.x2), dy = (float)(l.y1 - l.y2);
  const float len = sqrtf(dx * dx + dy * dy);
  FlowPfeil p;
  p.spitzeX = (int16_t)((l.x1 + l.x2) / 2);
  p.spitzeY = (int16_t)((l.y1 + l.y2) / 2);
  // The unit direction of travel towards the hub, and the two sides of the line: the
  // same perpendicular, rotated, without an ellipse to take it from.
  const float ux = (len > 0.0f) ? dx / len : 0.0f, uy = (len > 0.0f) ? dy / len : 0.0f;
  const float c = 0.8660254f, s = 0.5f; // cos und sin von 30 Grad
  for (int i = 0; i < 2; i++) {
    const float seite = (i == 0) ? 1.0f : -1.0f;
    const float ox = kPfeilLaenge * (-c * ux - seite * s * uy);
    const float oy = kPfeilLaenge * (-c * uy + seite * s * ux);
    p.armX[i] = p.spitzeX + (int16_t)lroundf(ox);
    p.armY[i] = p.spitzeY + (int16_t)lroundf(oy);
  }
  return p;
}

#endif // RCT_GUI_FLOWLAYOUT_H