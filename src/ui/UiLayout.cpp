// The board's own numbers, and where they come from.
//
// One instance per board, chosen at compile time like the language is. Today there
// is one board and one instance, and the simulator renders that one.
//
// The numbers are the panel's, not chosen here: they are the ones GuiApp.cpp and
// FlowLayout.h carried as literals and #defines before this file existed, moved
// without being altered. Stage 5 of the hardware plan calls for exactly this move,
// pixel for pixel, and the proof is the seven pages rendered before and after.
//
// SPDX-License-Identifier: MIT
#include "UiLayout.h"

namespace {

// --- the Guition ESP32-S3-4848S040, 480 x 480, capacitive touch --------------
// The numbers below are the panel's measured ones. Each says what it measures, so
// that a second board is a second set of values rather than a second copy of this
// file with the numbers edited in place.
const UiLayout kGuition4848S040 = {
    /* screenW */ 480,
    /* screenH */ 480,
    /* rotation */ 0,
    /* statusH */ 44,
    /* navH */ 72,
    // 7 px, not 0: the heading moved to the top of the page to make room for the ring's
    // arrowhead at its apex (see flowY), and at 0 the text's ink started 4 px below the
    // status bar with nothing between it and the diagram. The 7 px are measured off the
    // screenshots, not chosen: the bold 16 px label's ink runs y 52..66 on both boards,
    // the ring's ink begins at 73 on the 480 and at 69 on the 800 - 6 px and 2 px of
    // air. Nothing below the heading moves for them; the label's box is 21 px tall and
    // reaches past flowY whatever its y is.
    /* headY */ 7,
    // The first row on the info and device pages, below the heading, and the pitch
    // there. 14 rows have to fit in contentH: 40 + 13*22 + 20 = 346 < 364, with
    // the 16 px font's 20 px line box leaving 2 px of air per row.
    /* rowY0 */ 40,
    /* rowPitch */ 22,
    /* flow */
    {
        /* hubY */ 126,      // the hub's centre, and the ring's with it
        /* sideD */ 60,      // PV, grid, battery
        // 70, and 5 px less than the 80 it was: the circle in the middle is the one
        // thing the eye lands on first, and next to a 116 px ring an 80 px house looked
        // heavy. The icon inside it does not shrink with it - the house glyph is 26 px of
        // ink either way, drawn at 100 % out of lv_font_montserrat_36 - so 70 px leaves
        // 22 px of white on each side where 80 left 27. A side effect worth having: the
        // house's value, which stands 53 px to the right and 29 px down, used to clip
        // the circle's rim by 2 px and now clears it by 6.
        /* hubD */ 70,
        /* alleinD */ 190,   // the PV as the only node on the page
        // 26 px ABOVE the hub row, which puts this one-node circle back at the y = 100 it
        // has always had: it is placed against the TOP of the container with a measured
        // 5 px of air over it, and the row is at 126 because the ring wanted it there.
        // A 190 px circle under a row at 126 would show 31 px of nothing above it.
        /* aloneDY */ -26,
        /* hubX */ 240,
        // The battery's centre, and it is NOT the ring's lowest point - it is where the
        // sector lines come out of the node's circle.
        //
        // The ring's lowest point is hubY + ry, 242, and the two sector lines run along
        // the node's sides and vanish behind it a few pixels before they get there: the
        // ring curves away faster than a 60 px circle does, and over the node's own
        // radius that difference is the sagitta (30^2) / (2 * 116) = 3.9 px. Put the
        // node's centre there and the lines leave it in the middle; put it at the ring's
        // lowest point, as it was, and they left it 4 px above the middle - which is
        // what "the spokes' lines no longer start in the middle" is.
        /* batDY */ 238,
        // 116 x 116: A CIRCLE, and that is the request - it was 120 and is 4 px less for
        // one reason. The top sector's arrowhead stands on the ring's HIGHEST point, and
        // its 21 px label has to fit inside the container, so the ring's top needs 10 px
        // of air and not 4. That is what the page gave up: the heading's line moved to
        // the top of the page and the drawing 10 px up with it. The battery's value ends
        // 4 px above the container's bottom, so there is nothing else to give.
        // The width stopped being a choice long ago, which is what lets the three sectors
        // be three equal 120 degree arcs.
        /* rx */ 116, /* ry */ 116,
        /* pillW */ 152,
        /* pillH */ 34,
        /* pillGap */ 6,
        /* pillY */ 318,
        /* pillDY */ 0,      // eine Reihe: alle drei auf pillY
        /* pageW */ 480,
        /* flowY */ 20,
        /* flowH */ 288,
        /* valW */ 120,
        // The three on the ring: a quarter less sideways - to the left for the PV, because
        // the house is on its right - and down by the same factor, so every value is 25 %
        // closer to its node's centre along the SAME ray it was on. The battery's sideways
        // is 60 and not 53: its node is 60 px across, and at 53 the number would start
        // inside the circle.
        /* pvValDx */ -53, /* gridValDx */ 53, /* batValDx */ 60, /* hubValDx */ 53,
        /* pvValDy */ 29,  /* gridValDy */ 29,  /* batValDy */ 19,  /* hubValDy */ 29,
        /* alleinValW */ 200,
    },
    // The "today" cards. Three columns on top, two below, and the cell width comes
    // out of the arithmetic both times: 3 columns and a 5 px gap give 146, two
    // columns and a 4 px gap give 222. Seven cards in three rows.
    /* cards */
    {
        /* x0 */ 16,
        /* rows */ 3,
        /* row */
        {
            /* cols */ 3, /* y */ 36,  /* h */ 96,  /* gap */ 5,
            /* cols */ 2, /* y */ 146, /* h */ 88,  /* gap */ 4,
            /* cols */ 2, /* y */ 248, /* h */ 100, /* gap */ 4,
        },
        // Produced, self use, fed in, consumed, imported, self, self rate - the
        // order the page reads in, which is content and not geometry.
        /* cardRow */ {0, 0, 0, 1, 1, 2, 2},
        /* cardCol */ {0, 1, 2, 0, 1, 0, 1},
        /* n */ 7,
    },
    // The info and device rows: a name 24 px in, its value at 156, 22 px pitch.
    /* rows */
    {
        /* nameX */ 24, /* valX */ 156, /* y0 */ 40, /* pitch */ 22, /* cols */ 1,
    },
    // The 24 h chart's card. 10 px from the left, 52 down, 458 x 280: the width is
    // not the full 480 minus two 10 px margins, because the scale markers sit
    // INSIDE the card and the width that would have gone into a gutter goes into
    // the plot area instead.
    /* chart */
    {
        /* x */ 10, /* y */ 52, /* w */ 458, /* h */ 280,
        /* pad */ 10,
        /* legendY */ 32, /* legendDot */ 10,
        /* legendTextDx */ 14, /* legendTextDy */ -3,
        /* legendGap */ 24,
    },
    // The energy bars: 480 px content, 20 px margin, so the track is 440 px and the
    // value's right edge lands exactly on the track's right edge (340 + 120 = 460 =
    // 20 + 440). These six numbers plus the value's x and width were literals in
    // GuiApp.cpp until the second board asked for a wider bar and could not get one.
    /* bars */
    {
        /* x */ 20, /* w */ 440, /* h */ 16,
        /* labelGap */ 24,
        /* row0Y */ 82, /* rowH */ 58,
        /* valX */ 340, /* valW */ 120,
    },
};

// --- a second size, as the plan asks: a hand-set 800 x 480 profile ------------
// Stufe 6 is the proof, and this is it: the same seven pages on a panel that is
// wider than it is tall. 480 x 480 is square, 800 x 480 is not, which is the whole
// reason the plan insists on a page builder per board instead of a scale factor -
// there is no factor that turns a square into a landscape without moving things.
//
// Each choice below is a board's, not a formula's, and says why:
//   bars    the same 44 and 72: they are text heights, not fractions of anything
//   flow    THE 480's DIAGRAM, 20 px wider and 20 px higher than centred, in the 540 px
//           to the right of a column of three stacked pills at the left - see below
//   cards   three columns instead of the 480's mixed three-and-two, and the same
//           three rows at the same heights - the board is 480 px TALL as well, so
//           there is no more height to spend, only width
//   rows    two columns, because half of 800 px is a comfortable row while one
//           column puts the name 700 px away from its value
//   chart   wider, and NOT shorter: see the card and chart notes below
const UiLayout kBreit800x480 = [] {
  UiLayout l = kGuition4848S040;
  l.screenW = 800;
  l.screenH = 480;
  // THE DIAGRAM IS THE 480's DIAGRAM, only bigger. It used to stretch the ring to
  // 240 x 116 to fill the width, which is not a wider version of the drawing on the
  // narrow board but a different drawing: the same nodes, the same sectors, the same two
  // measurements, and none of it where a reader has already learned it.
  //
  // So the drawing is the 480's and the ring is a CIRCLE again, 28 px bigger in both
  // directions - 144 where the 480 has 116. Two steps got here and the second undid the
  // first: 1.19 times the old 136 x 116 filled the height but left the ring 24 px wider
  // than tall, and a ring that is not round is not the ring the other board draws.
  //
  // Everything else in the drawing is UNCHANGED and is not in this list: the 60 px nodes,
  // the 70 px house, the 190 px one-node circle and all four value offsets. A scale of the
  // ring does not scale them, and that is the answer the question had to settle.
  //
  // The ring is not the whole of what a bigger drawing needs, which is why hubY, batDY,
  // flowH and aloneDY are in this profile at all: they follow from the radius and not from
  // the page.
  l.flow.rx = 144;
  l.flow.ry = 144;

  // THE HUB ROW DOWN BY 26, so the top arrowhead keeps its 8 px of room inside the
  // container. The drawing grows 56 px, all of it below the apex.
  l.flow.hubY = 152;

  // The battery where the sector lines come out of its circle, and that is 3 px above
  // the ring's lowest point: the sagitta over the node's radius, (30^2) / (2 * 144) = 3.1.
  l.flow.batDY = 293;

  // The box 20 px above its centred place, as before, and long enough for what is in it:
  // the 324 px of drawing end at y 351 on the page, and the box at 358.
  l.flow.flowY = 18;
  l.flow.flowH = 340;

  // 125 PX RIGHT OF THE MIDDLE, and that is the arithmetic again: the pills stand in the
  // 255 px at the left, the drawing is 410 px wide - the ring's 144 plus a value's offset
  // and half its text on either side - and (800 - 255 - 410) / 2 = 67 px of margin is
  // what is left to divide, so the drawing is centred in the space beside the column.
  // It stood 10 px further right for one round and is back where the two numbers put it.
  l.flow.hubX = 525;

  // The one-node circle keeps the 480's 5 px of air over it, which the hub row's move
  // takes away from it.
  l.flow.alleinDY = -52;

  // THE PILLS AS A COLUMN at the left, 5 px narrower on each side than the row they
  // replace, and one pillH + one pillGap below the last: 238, 278 and 318, ending 12 px
  // above the bottom, which is the margin the row has on the 480.
  l.flow.pillW = 244;
  l.flow.pillY = 238;
  l.flow.pillDY = 34 + 6;
  l.flow.pageW = 800;
  // CARDS: three columns, so a card is (800 - 32 - 16) / 3 = 250 px.
  //
  // MEASURED, why not four. Four columns give 186 px, and the longest label -
  // "Eigenverbrauchsquote" - runs out of its card at that width: measured on the
  // 480 build it needs at least 222 px, which is what its two-column rows have. So
  // four columns overflow by 36 px and three fit with 28 px to spare. The label is
  // the same text at the same size on both boards; only the card differs.
  //
  // The three rows keep the 480 heights (96 / 88 / 100 at y 36 / 146 / 248). Both
  // boards are 480 px tall, so the vertical arrangement that is known to fit fits
  // here unchanged. What changed before was the number of rows and their heights,
  // and that is what produced two rows of 150 px with 160 px of dead space below.
  //
  // Seven cards in three columns reads 3 + 3 + 1: the last row has one card and two
  // empty cells. Uniform widths were preferred over the 480's mixed 146/222, which
  // look wrong side by side on a landscape page.
  l.cards.rows = 3;
  l.cards.row[0] = UiCardRow{3, 36, 96, 8};
  l.cards.row[1] = UiCardRow{3, 146, 88, 8};
  l.cards.row[2] = UiCardRow{3, 248, 100, 8};
  l.cards.cardRow[0] = 0; l.cards.cardCol[0] = 0;
  l.cards.cardRow[1] = 0; l.cards.cardCol[1] = 1;
  l.cards.cardRow[2] = 0; l.cards.cardCol[2] = 2;
  l.cards.cardRow[3] = 1; l.cards.cardCol[3] = 0;
  l.cards.cardRow[4] = 1; l.cards.cardCol[4] = 1;
  l.cards.cardRow[5] = 2; l.cards.cardCol[5] = 0;
  l.cards.cardRow[6] = 2; l.cards.cardCol[6] = 1;
  l.cards.n = 7;
  l.rows.cols = 2;
  l.rows.valX = 216;
  // CHART: 778 px wide, and the 280 px height is inherited rather than reduced.
  // The chart starts below the 44 px page head, so 280 runs to y = 375 on a 480
  // screen and the gap summary fits under it inside contentH(). Measured on the
  // device and in the simulator alike: y = 96..375, 280 px. The 200 px this profile
  // carried left 80 px of empty screen below the chart for no reason - the board is
  // not taller than the one that gets 280.
  l.chart.w = 778;
  l.chart.legendGap = 32;
  // BARS: the same 20 px margin and the same heights, and the track takes the width
  // that is left over - 800 - 2*20 = 760. MEASURED before: the track ran x=20..460,
  // 440 px, and the right third of an 800 px screen stayed empty. The value label
  // keeps its right edge on the track's right edge, which is what 340 + 120 = 460 =
  // 20 + 440 was doing on the 480: 660 + 120 = 780 = 20 + 760.
  l.bars.w = 760;
  l.bars.valX = 660;
  return l;
}();

}  // namespace

// The firmware's board. Compile-time, and the guard is the whole mechanism: a
// simulator build (PANEL_SIM) does not get this one, it gets the profile it asked
// for from tools/panel_sim/sim_board.cpp. Same symbol, one definition either way,
// and no build machine has to edit this file to look at a second board.
#ifndef PANEL_SIM
const UiLayout &uiLayout() { return kGuition4848S040; }
#endif

// The size the build machine asks for, or nullptr if this tree has no board of that
// size - in which case the caller says so instead of quietly drawing at the wrong
// resolution.
const UiLayout *uiLayoutForSize(int screenW, int screenH) {
  if (screenW == 480 && screenH == 480) {
    return &kGuition4848S040;
  }
  if (screenW == 800 && screenH == 480) {
    return &kBreit800x480;
  }
  return nullptr;
}