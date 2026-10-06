// Pins of the switched output ("Steckdose") of this panel.
//
// The numbers live in the board's pin map now - see src/board/BoardPins.h. This file
// stays because RelayPins.h is the name three call sites already use, and because
// the comment below is the one thing about a relay that cannot be read off a pin
// number: which level switches it on.
//
//   1 = low-level trigger: the module pulls its coil in while the pin is LOW.
//       The usual "1Way" relay board, and what ESPHome's config for this board
//       describes (GPIO 40, inverted).
//   0 = high-level trigger: coil in while the pin is HIGH.
//
// Measured on the wall, on this board's own relay port (2026-09-30): the pin sits
// HIGH while the output is idle and the output follows a HIGH, so the module is
// high-level triggered. relayInit() prints both levels into the log, so the setting
// that is actually compiled in can be read off the serial line without a meter.
//
// The one hardware caveat: an active-low module is *on* while the pin floats, and
// between reset and relayInit() the pin is an input. On the board's own relay port
// the surrounding hardware decides this; on a module wired by hand put 10 kOhm from
// the pin to 3V3. An active-high module has the mirrored problem, where a floating
// pin is the safe state - but it should still get 10 kOhm to GND so it cannot pick
// up noise while the firmware boots.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_OUTPUT_RELAYPINS_H
#define RCT_OUTPUT_RELAYPINS_H

#include "board/BoardPins.h"

#endif // RCT_OUTPUT_RELAYPINS_H
