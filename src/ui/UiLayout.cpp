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
    /* headY */ 8,
    // The first row on the info and device pages, below the heading, and the pitch
    // there. 14 rows have to fit in contentH: 40 + 13*22 + 20 = 346 < 364, with
    // the 16 px font's 20 px line box leaving 2 px of air per row.
    /* rowY0 */ 40,
    /* rowPitch */ 22,
    /* flow */
    {
        /* rowY */ 80,       // the outer nodes' centre
        /* hubY */ 82,       // the hub's centre, 2 px lower
        /* sideD */ 60,      // PV, grid, battery
        /* hubD */ 92,       // the house
        /* alleinD */ 190,   // the PV as the only node on the page
        /* alleinDY */ 18,   // that circle stands 18 px below the hub row
        /* pvX */ 60,
        /* hubX */ 240,
        /* gridX */ 420,
        /* batDY */ 210,
        /* pillW */ 152,
        /* pillH */ 34,
        /* pillGap */ 6,
        /* pillY */ 316,
        /* pageW */ 480,
        /* flowY */ 35,
        /* valW */ 120,
        /* pvValDy */ 36,     /* gridValDy */ 38,   /* batValDy */ 37,
        // 10 px right of the hub's centre and 38 px below it: the vertical line
        // down to the battery runs through x = hubX, and the number stands beside it.
        /* hubValDx */ 10, /* hubValDy */ 38,
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
};

// --- a second size, as the plan asks: a hand-set 800 x 480 profile ------------
// Stufe 6 is the proof, and this is it: the same seven pages on a panel that is
// wider than it is tall. 480 x 480 is square, 800 x 480 is not, which is the whole
// reason the plan insists on a page builder per board instead of a scale factor -
// there is no factor that turns a square into a landscape without moving things.
//
// Each choice below is a board's, not a formula's, and says why:
//   bars    the same 44 and 72: they are text heights, not fractions of anything
//   flow    the row stretched to the wider page, the hub's size kept, three pills of
//           254 px so they still fill the width exactly
//   cards   four columns on top and four below - eight cells, seven cards
//   rows    two columns, because half of 800 px is a comfortable row while one
//           column puts the name 700 px away from its value
//   chart   wider and shorter: a landscape page has width to spend and no height
const UiLayout kBreit800x480 = [] {
  UiLayout l = kGuition4848S040;
  l.screenW = 800;
  l.screenH = 480;
  l.flow.pvX = 150;
  l.flow.hubX = 400;
  l.flow.gridX = 650;
  l.flow.pillW = 254;
  l.flow.pageW = 800;
  l.cards.x0 = 16;
  l.cards.rows = 2;
  l.cards.row[0] = UiCardRow{4, 36, 150, 8};
  l.cards.row[1] = UiCardRow{4, 200, 150, 8};
  l.cards.cardRow[0] = 0; l.cards.cardCol[0] = 0;
  l.cards.cardRow[1] = 0; l.cards.cardCol[1] = 1;
  l.cards.cardRow[2] = 0; l.cards.cardCol[2] = 2;
  l.cards.cardRow[3] = 1; l.cards.cardCol[3] = 0;
  l.cards.cardRow[4] = 1; l.cards.cardCol[4] = 1;
  l.cards.cardRow[5] = 1; l.cards.cardCol[5] = 2;
  l.cards.cardRow[6] = 1; l.cards.cardCol[6] = 3;
  l.cards.n = 7;
  l.rows.cols = 2;
  l.rows.valX = 216;
  l.chart.w = 778;
  l.chart.h = 200;
  l.chart.legendGap = 32;
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