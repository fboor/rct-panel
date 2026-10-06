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

// A value under a node.
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

// Everything the overview's diagram needs to know, and nothing about how it is
// drawn. guiUpdateFlow() applies this; it is the only thing that talks to LVGL.
struct FlowLayout {
  FlowNode pv, house, grid, battery;
  FlowLink linkPv, linkGrid, linkBattery;
  FlowValue valPv, valHouse, valGrid, valBattery;
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
  return (int16_t)((flowAlleinY(u) + u.flow.alleinD / 2 + flowPillYInFlow(u) -
                    u.flow.pillH / 2) / 2);
}

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
  L.pv.x = hubIstPv ? f.hubX : f.pvX;
  L.pv.y = hubIstPv ? (pvAllein ? flowAlleinY(ui) : f.hubY) : f.rowY;
  L.pv.d = hubIstPv ? (pvAllein ? f.alleinD : f.hubD) : f.sideD;

  L.grid.visible = kenneNetz;
  L.grid.x = f.gridX;
  L.grid.y = f.rowY;
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
  L.valHouse.visible = kenneHaus;
  L.valHouse.x = f.hubValX;
  L.valHouse.y = f.hubValY;
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
    L.valPv.x = f.hubValX;
    L.valPv.y = f.hubValY;
  } else if (hubIstPv) {
    // Nothing below the node and nothing above but the circle: the value is the
    // content of the page, so it goes in the middle of the band between the two -
    // which is the only thing that makes it belong to the circle rather than to
    // the pill. Its y is therefore a middle, not a top edge.
    L.valPv.x = flowAlleinValX(ui);
    L.valPv.y = flowAlleinValY(ui);
    L.valPv.centreY = true;
  } else {
    L.valPv.x = f.pvValX;
    L.valPv.y = f.pvValY;
  }
  L.grossPvIco = hubIstPv;
  // In the one-node layout the icon is drawn from its own font at its own size;
  // everywhere else it is the 28 px font, scaled like the hub's or left alone.
  // A 28 px glyph at 480 % is a blur, and that layout is the whole page.
  L.pvIcoNative = pvAllein;

  L.valGrid.visible = kenneNetz;
  L.valGrid.x = f.gridValX;
  L.valGrid.y = f.gridValY;
  L.valGrid.w = f.valW;
  L.valGrid.centreY = false;
  L.valGrid.gross = false;

  L.valBattery.visible = kenneAkku;
  L.valBattery.x = f.batValX;
  L.valBattery.y = f.batValY;
  L.valBattery.w = f.valW;
  L.valBattery.centreY = false;
  L.valBattery.gross = false;

  L.batterySoc = kenneAkku;
  // The triangle marks the grid link, so it only exists where there is one.
  L.islandMark = kenneNetz;

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
  const int16_t gesamt = (int16_t)(n * f.pillW + (n > 0 ? (n - 1) * f.pillGap : 0));
  const int16_t start = (int16_t)((f.pageW - gesamt) / 2);
  int k = 0;
  for (int i = 0; i < 3; i++) {
    if (p & (1u << i)) {
      L.pillX[i] = (int16_t)(start + k * (f.pillW + f.pillGap));
      k++;
    }
  }
  return L;
}

// The arrow on a connector sits on its midpoint, and the island triangle 25 px
// above the grid link - the same places as before, computed from the layout so
// they follow it.
static inline void flowLinkMidpoint(const FlowLink &l, int16_t *cx, int16_t *cy) {
  *cx = (int16_t)((l.x1 + l.x2) / 2);
  *cy = (int16_t)((l.y1 + l.y2) / 2);
}

#endif // RCT_GUI_FLOWLAYOUT_H