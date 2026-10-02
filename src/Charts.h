// The series of the history chart and their colours - one place, two users.
//
// The panel draws the 24 h chart (src/gui/GuiApp.cpp) and the web interface
// draws the same six series in the browser (src/web/pages.h). If the colours
// were written down twice, the same line would have two colours depending on
// where it is looked at, and nobody would find out why until a support case
// asked which colour is the battery.
//
// This is a header of constants, like src/DataStatus.h is a header of one
// function: it is shared because the rule is shared, not because there is code
// to put behind an interface.
//
// SPDX-License-Identifier: MIT
#ifndef CHARTS_H
#define CHARTS_H

#include <stdint.h>

// The number of series in the history: five powers on one axis, then the state
// of charge on its own 0..100 axis over the full height.
static const int kChartSeries = 6;

// The colours, in the order grid, consumption, PV, external generator, battery,
// state of charge. The first five are the portal palette; the state of charge
// is brightened yellow, because it has to lift off the dark card of the panel
// display and off the white card of the web page at the same time.
static const uint32_t kChartColor[kChartSeries] = {
    0xCA0C0F, 0xA45EE5, 0x3EC97A, 0x2E93E5, 0xF0A202, 0xFFEA00};

// The index of the state of charge: the last one, and the only series that is
// not a power. Both drawings need to skip it when they work out the scale of
// the power axis.
static const int kChartSoc = 5;

#endif // CHARTS_H
