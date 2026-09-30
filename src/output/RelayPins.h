// Pins of the switched output ("Steckdose") of this panel.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_OUTPUT_RELAYPINS_H
#define RCT_OUTPUT_RELAYPINS_H

// GPIO 40 - the 1-way relay port of the 4848S040 board (silkscreen
// "1Way/3WayRelayPort"). It is the one pin the project does not use: display
// config and touch own 3..21/38/39, the SD card 41/42/47/48, 45..48 are free
// spares, 1 and 2 are the other two relay ports of the board and stay free for
// a second output later.
#define RELAY_PIN 40

// Which level switches the relay on.
//
//   1 = low-level trigger: the module pulls its coil in while the pin is LOW.
//       The usual "1Way" relay board, and what ESPHome's config for this board
//       describes (GPIO 40, inverted).
//   0 = high-level trigger: coil in while the pin is HIGH.
//
// This is the one constant that cannot be derived from code - it belongs to the
// module, not to the software. The "Test" button on the Service page (5 s on /
// 5 s off, twice) is the check: if the relay stays silent there, flip this and
// flash again. Nothing else has to change, because every switch goes through
// relayWrite() in Relay.cpp.
//
// Measured on the wall, on this board's own relay port (2026-09-30): the pin
// sits HIGH while the output is idle and the output follows a HIGH, so the
// module is high-level triggered. That is the setting below. relayInit() prints
// both levels into the log, so the setting that is actually compiled in can be
// read off the serial line without a meter.
//
// The one hardware caveat: an active-low module is *on* while the pin floats,
// and between reset and relayInit() the pin is an input. On the board's own
// relay port the surrounding hardware decides this; on a module wired by hand
// put 10 kOhm from the pin to 3V3. An active-high module has the mirrored
// problem, where a floating pin is the safe state - but it should still get
// 10 kOhm to GND so it cannot pick up noise while the firmware boots.
#define RELAY_ACTIVE_LOW 0

#if RELAY_ACTIVE_LOW
#define RELAY_LEVEL_ON LOW
#define RELAY_LEVEL_OFF HIGH
#else
#define RELAY_LEVEL_ON HIGH
#define RELAY_LEVEL_OFF LOW
#endif

#endif // RCT_OUTPUT_RELAYPINS_H
