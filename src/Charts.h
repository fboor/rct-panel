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
//
// Two of them are not a matter of taste but a measurement. The consumption
// purple (0xA45EE5) and a blue external generator were 3/255 apart once
// simulated for deuteranopia - the same colour. For the roughly six per cent of
// men with a red-green weakness those two lines could not be told apart at all,
// and nobody had noticed because both were legible for everyone else. The
// external generator is therefore a desaturated grey-blue: it separates from the
// purple by its saturation, which survives every kind of colour blindness,
// instead of by its hue. The grid red was brightened because 0xCA0C0F sat at
// 3.57:1 on the dark card and turned almost black under protanopia.
//
// tools/chart_color_test/ holds this down: it fails if a pair comes closer than
// 30/255 again, or if a colour falls below 3:1 on the dark card.
static const uint32_t kChartColor[kChartSeries] = {
    0xE63946, 0xA45EE5, 0x3EC97A, 0x7A8CA0, 0xF0A202, 0xFFEA00};

// The index of the state of charge: the last one, and the only series that is
// not a power. Both drawings need to skip it when they work out the scale of
// the power axis.
static const int kChartSoc = 5;

#endif // CHARTS_H
