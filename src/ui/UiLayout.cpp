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
        /* hubValX */ 250,   // right of the vertical battery line, so the line does
        /* hubValY */ 120,   // not run through the digits
        /* pvValX */ 0,
        /* pvValY */ 116,
        /* gridValX */ 360,
        /* gridValY */ 118,
        /* batValX */ 180,
        /* batValY */ 247,
        /* alleinValW */ 200,
    },
};

}  // namespace

const UiLayout &uiLayout() { return kGuition4848S040; }