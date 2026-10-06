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
  int16_t rowY;      // the outer nodes' centre
  int16_t hubY;      // the hub's centre, 2 px lower than the row
  int16_t sideD;     // PV, grid, battery
  int16_t hubD;      // the house
  // The PV as the only node on the page: bigger than a node that has company,
  // because it is then the whole diagram. Its glyph's ink is 114 px, so 138 px left
  // 12 px of white inside the circle - the icon looked pressed against the rim - and
  // 190 px gives 38 px on each side. Bounded by the page's height: the next 10 px
  // would need the centre at y = 110 to keep its top edge off the frame.
  int16_t alleinD;
  int16_t alleinDY;  // how far below the hub row the one-node circle stands
  int16_t pvX;
  int16_t hubX;
  int16_t gridX;
  int16_t batDY;     // the battery's centre, below the hub

  // --- the three pills ---
  // 152 px wide with 6 px between them and 6 px of margin fills the 480 px page
  // exactly. Fewer pills are centred as a group: one pill at the far left with
  // 320 px of empty space beside it looks broken.
  int16_t pillW;
  int16_t pillH;     // GuiApp.cpp uses this for the pills it builds
  int16_t pillGap;
  int16_t pillY;
  int16_t pageW;

  // The diagram's container starts this far down the page (GuiApp.cpp's FLOW_Y).
  // The nodes and values live in the container, the pills on the page, so the band
  // between the circle and the pill can only be measured in the container - which is
  // what flowPillYInFlow below is for.
  int16_t flowY;

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
  // Three of the four values are centred on their node and stand this far below its
  // centre. The three distances are 36, 38 and 37 px and are stated separately
  // because they were measured separately - one shared value would move two of the
  // three by a pixel, and a refactor that moves a pixel is not a refactor.
  int16_t pvValDy, gridValDy, batValDy;
  // The hub's value is the exception: it stands RIGHT of the vertical battery line,
  // so the line does not run through the digits. Its width is valW like the rest.
  int16_t hubValDx, hubValDy;
  // The value under the PV when it is the only node: centred, and in the middle of
  // the empty band between the circle above and the pill below. A 200 px label with
  // its text centred puts its left edge at hubX - 100, and 200 px is what the 36 px
  // font needs for "1,23 kW".
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