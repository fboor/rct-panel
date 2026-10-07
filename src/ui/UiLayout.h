// What a board answers about its own geometry.
//
// Until this file existed, the panel's screen was a set of literals: 480 in seven
// places, six #defines at the top of GuiApp.cpp, and thirty-odd kFlow* constants in
// FlowLayout.h. Every one of them was correct and none of them said what they were
// a measurement OF, so a second display could not be laid out without copying the
// file and changing the numbers - which is the same code with two truths in it.
//
// The rule the hardware plan sets is that placement is per board and content is
// once: what page there is, which caption, which value, which colour and which
// formula stay in the tree; coordinates, sizes, font sizes and what sits where move
// here. One UiLayout per board, chosen at compile time like the language is.
//
// At 480 x 480 the numbers in this file are the ones the panel has been drawing,
// unchanged. That is not a claim, it is checked: tools/flow_layout_test asserts the
// flow invariants, and the simulator renders all seven pages and compares the
// pictures before and after the move.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_UI_UILAYOUT_H
#define RCT_UI_UILAYOUT_H

#include <stdint.h>

// The flow diagram's numbers, which are its own because it is the busiest thing on
// the screen and the only layout that has four arrangements rather than one.
//
// The reasoning behind each number moved here with it and is not repeated in
// FlowLayout.h, which now only works out WHERE things go from WHAT this board says.
struct UiFlow {
  // --- nodes, in the container's own coordinates ---
  //
  // The PV's and the grid's place is NOT here: they stand ON the ring, at an angle
  // off its horizontal, and that angle is the same on every board while the pixels
  // that come out of it are not. FlowLayout.h derives them from rx and ry below, and
  // what used to be pvX, gridX and rowY is gone - a number in this list that has to be
  // kept in step with an angle is one more thing to forget.
  int16_t hubY;      // the hub's centre, and the ring's
  int16_t sideD;     // PV, grid, battery
  int16_t hubD;      // the house
  // The PV as the only node on the page: bigger than a node that has company,
  // because it is then the whole diagram. Its glyph's ink is 114 px, so 138 px left
  // 12 px of white inside the circle - the icon looked pressed against the rim - and
  // 190 px gives 38 px on each side. Bounded by the page's height: the next 10 px
  // would need the centre at y = 110 to keep its top edge off the frame.
  int16_t alleinD;
  int16_t alleinDY;  // where the one-node circle stands, relative to the hub row. It
                   // follows the row only as far as the top of the container allows:
                   // the circle is 190 px and the row is at 124, so anything below 0
                   // here would push it off the top of the page.
  int16_t hubX;
  int16_t batDY;     // the battery's centre: the ring's lowest point

  // --- the ring the three outer nodes stand on ---
  //
  // PV at the left end, grid at the right end, battery at the bottom, house in the
  // middle: the design this follows, where the ring IS the connection and the three
  // sectors cut out of it are the three ways between two nodes that do not pass the
  // house.
  //
  // It is an ellipse and not a circle, because the page is wider than it is tall: a
  // circle that fits 280 px of height is 280 px wide and would leave 100 px of empty
  // board on either side of a 480 px page. 172 x 92 fills the width without the
  // battery needing a second row. lv_arc cannot draw it - it takes its radius from
  // min(width, height), so it can only ever be round - and the sectors are polylines
  // instead.
  int16_t rx, ry;

  // --- the three pills ---
  // 152 px wide with 6 px between them and 6 px of margin fills the 480 px page
  // exactly. Fewer pills are centred as a group: one pill at the far left with
  // 320 px of empty space beside it looks broken.
  int16_t pillW;
  int16_t pillH;     // GuiApp.cpp uses this for the pills it builds
  int16_t pillGap;
  int16_t pillY;
  // How far each pill stands from the one before it DOWNWARDS. 0 is the row on the narrow
  // board, where the three are next to each other and pillY is their line; pillH + pillGap
  // is the stack on the wide one, where they are three rows of a column at the left.
  int16_t pillDY;
  int16_t pageW;

  // The diagram's container starts this far down the page (GuiApp.cpp's FLOW_Y).
  // The nodes and values live in the container, the pills on the page, so the band
  // between the circle and the pill can only be measured in the container - which is
  // what flowPillYInFlow below is for.
  //
  // 20 puts the drawing 10 px higher than the 30 that once centred it, and the
  // heading's line moved to the top of the page to pay for it: the ring's highest
  // point is 10 px below this, and at 30 that left the arc 5 px under "Ueberschrift"
  // while the battery's value was 6 px above the pills. There is no way to move the
  // drawing up without also making room for the ring at the top, and "Ueberschrift"
  // is short enough that a line it does not fill loses nothing.
  int16_t flowY;
  // The container's height, and it is a number of its own because the drawing is not
  // 280 px any more: the battery's value hangs 19 px below the battery node and its
  // line is 21 px tall, so the lowest ink is at 284. 288 leaves 4 px under it, which
  // is what it had against 280, and costs the gap to the pills 8 of its 18 px - the
  // pills hang off the page and not off this, so there is nothing above to hit.
  int16_t flowH;

  // --- values ---
  // The hub's value is right of the vertical battery line, so the line does not run
  // through the text - and that is also where the big PV's value goes in a layout
  // without a house, because the place is free then. 120 px is the width the value
  // labels are created with and the only one that fits the 16 px font; the one-node
  // layout states a wider one for its 36 px number.
  int16_t valW;
  // A value's place is stated RELATIVE TO ITS NODE, never in page coordinates. That
  // is the change a wider board forces: with absolute positions, moving a node
  // leaves its number behind, and the 800 x 480 profile put the grid's value in the
  // middle of the battery's line.
  //
  // A value's place is stated RELATIVE TO ITS NODE - both as a middle, not as a corner,
  // and below or above. One rule for all four, so that "move it" is one number.
  //
  // The three around the ring do NOT stand under their node, and the reason is the ring:
  // a node stands ON it, so the two sectors that meet there come up to it from below and
  // sweep the band a value would sit in. They stand in the free corner at their node's
  // lower side instead - PV to the left, grid to the right, battery to the right - which
  // is what the house has always done, only that the house's corner is inward.
  //
  // The battery's is only 15 px down, not 38: its node is the ring's lowest point, so
  // there is nothing under it to be in the way of and nothing left below either - 38 px
  // would put the number past the bottom of the container.
  int16_t pvValDx, gridValDx, batValDx, hubValDx;
  int16_t pvValDy, gridValDy, batValDy, hubValDy;
  // The value under the PV when it is the only node: centred, and in the middle of
  int16_t alleinValW;
};

// The "today" page as a grid: how many columns a row has, how wide a cell is, and
// which cell each card occupies.
//
// Not a uniform grid, and the reason is worth writing down because a second board
// will hit it too. On this panel the first row carries three cards of 146 px and the
// two rows below carry two of 222 px each. A cell is therefore NOT a span of the
// narrow cell - 222 is neither 146 nor 2 x 146 + gap. What IS true is that each
// row states its own column count and its own gap, and the cell width follows:
//   row 1: 3 columns, 5 px gap  -> (480 - 32 - 2*5) / 3 = 146
//   row 2: 2 columns, 4 px gap  -> (480 - 32 - 4)   / 2 = 222
// Both exact, and both with the same 16 px margin at each side. So the board states
// columns and gap, and the arithmetic gives the width - which is also what a wider
// board would need, since it only has to state how many cards it wants in a row.
struct UiCardRow {
  int cols;
  int y;
  int h;
  int gap;
};

struct UiCardGrid {
  int x0;                 // margin at the left and, mirrored, at the right
  int rows;
  UiCardRow row[3];
  // Which cell each of the seven cards takes. Row and column SEPARATELY, and not as
  // one running index: the first version numbered the cells across all rows and the
  // cards below the first row landed in the first row as well - which put four
  // cards on top of three others and left the page with three cards on it.
  //
  // Which caption a card carries and in what order the page reads is CONTENT and
  // belongs to the page builder; only the cell it stands in is the board's business.
  uint8_t cardRow[7];
  uint8_t cardCol[7];
  int n;                  // how many of the seven are used
};

// The rows on the info and device pages: a name at the left, its value at a fixed x,
// both on a fixed pitch.
//
// cols is 1 on this panel, and it is here rather than nowhere because a wider board
// wants two columns of half the width and twice the rows, and that is the same
// question asked twice.
struct UiInfoRows {
  int nameX;
  int valX;
  int y0;
  int pitch;
  int cols;
};

// The 24 h chart: the card, its inner padding, and the legend above it.
struct UiChartPlot {
  int x, y, w, h;
  int pad;                // inside the card; the scale markers live in here
  int legendY;            // top of the legend row
  int legendDot;          // the coloured dot's diameter
  int legendTextDx;       // text offset from the dot's left edge
  int legendTextDy;
  int legendGap;          // space between two legend entries
};

// The energy page's bars: one row per series, a label line (name left, value right)
// over a full-width bar. The name doubles as the legend, so the bar can use the whole
// width and there is no separate legend to keep in step.
//
// This used to be six literals in GuiApp.cpp, which is why a second board could not
// widen its bars: the page asked for 440 px and got 440 px on an 800 px screen. The
// numbers are the panel's, so they belong with the rest of the panel's numbers.
struct UiBars {
  int x;        // left edge of the bar, and of the name above it
  int w;        // bar length; the widest value fills it
  int h;        // bar height
  int labelGap; // name line to bar: the label's line box plus air
  int row0Y;    // first name line, below the heading and the period selector
  int rowH;     // per-series pitch: label + gap + bar + air
  int valX;     // the value's left edge, right-aligned so it ends with the bar
  int valW;     // the value label's width
};


struct UiLayout {
  // --- the screen ---
  int screenW;
  int screenH;
  // 0, 90 or 180. A board that mounts its panel turned answers 90, and the touch
  // mapping with it - see note in the board profile, because the rotation belongs to
  // the board and the mapping to the driver.
  int rotation;

  // --- the two bars ---
  int statusH;
  int navH;
  int contentH() const { return screenH - statusH - navH; }

  // Y of the page heading inside a page root. Every page uses this one value, so
  // the heading sits at the same spot on all seven.
  int headY;

  // --- the row pages (info, device) ---
  int rowY0;
  int rowPitch;

  // --- the flow diagram ---
  UiFlow flow;

  UiCardGrid cards;
  UiInfoRows rows;
  UiChartPlot chart;
  UiBars bars;
};

// How wide a card is in a row with this many columns and this gap. Derived, because
// it is what makes the row fill the width exactly - see UiCardGrid for why it is
// per row and not a span of a narrower cell.
inline int uiCardW(const UiLayout &u, int row) {
  const UiCardRow &r = u.cards.row[row];
  return (u.screenW - 2 * u.cards.x0 - (r.cols - 1) * r.gap) / r.cols;
}

// The board's layout. One per board, chosen at compile time; the simulator passes
// the same one it renders at, which is what makes a second resolution testable.
const UiLayout &uiLayout();

// The layout for a given screen size, for the simulator. The firmware calls
// uiLayout() and gets its own board; this is how a build machine asks what a
// different size would look like without a second board in the tree.
const UiLayout *uiLayoutForSize(int screenW, int screenH);

#endif  // RCT_UI_UILAYOUT_H