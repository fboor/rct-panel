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
  int16_t hubValX, hubValY;
  int16_t pvValX, pvValY;
  int16_t gridValX, gridValY;
  int16_t batValX, batValY;
  // The value under the PV when it is the only node: centred, and in the middle of
  // the empty band between the circle above and the pill below. A 200 px label with
  // its text centred puts its left edge at hubX - 100, and 200 px is what the 36 px
  // font needs for "1,23 kW".
  int16_t alleinValW;
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
};

// The board's layout. One per board, chosen at compile time; the simulator passes
// the same one it renders at, which is what makes a second resolution testable.
const UiLayout &uiLayout();

#endif  // RCT_UI_UILAYOUT_H