// The CH422G IO expander of the Waveshare ESP32-S3-Touch-LCD-7.
//
// Twelve outputs on the same I2C bus as the touch. Four of them are what the rest
// of the firmware needs: the display's reset, the touch's reset, the TF card's
// chip select and the USB/CAN selector. The fifth - the backlight - is left
// alone, because on this panel it hangs on GPIO 6 after a hardware mod and not on
// the expander (see the board profile).
//
// WHERE THE NUMBERS COME FROM
//
// The register map is out of the vendor's own expander driver
// (ESP32_IO_Expander/src/port/esp_io_expander_ch422g.c, Apache-2.0, Espressif):
// the chip answers at 0x20 and its registers sit at 0x24, 0x23, 0x38 and 0x26.
// The I2C address and the pin roles are out of the configuration their own demo
// compiles (esp_panel_board_custom_conf.h) and out of the wiki's pin table; both
// say the same thing, which is what the profile relies on. The register writes
// here are our own, through Wire - the vendor's driver drives them through IDF's
// i2c_master_write_to_device(), which this project does not use anywhere.
//
// ONE DEVIATION FROM THE VENDOR'S DRIVER, ON PURPOSE
//
// write_output_reg() skips a register whose value is zero, so clearing the last
// output it happens to be standing on leaves that bit where it was. Ours always
// writes, which is one byte more on a bus that runs at 400 kHz.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_BOARD_CH422G_H
#define RCT_BOARD_CH422G_H

#include <Arduino.h>

#include "BoardPins.h"

#if BOARD_CH422G_PRESENT

// The I2C bus, the probe at 0x20 and all twelve pins as push-pull outputs. The
// chip's power-on state is all twelve high, so a board that never calls this
// still has its backlight on - which is what makes a missing call visible as a
// lit panel rather than as a dark one.
bool ch422gBegin();

// One output. Returns false when the chip was not found, so a caller that needs
// the line can say so instead of driving a pin that does nothing. Writing the
// level a pin already has is free: no bus traffic at all, which matters for the
// SD card's chip select, which sits low for the life of the panel.
bool ch422gWrite(uint8_t pin, bool level);

// One output, as a pulse: inactive for lowMs, active for highMs. The display and
// the touch both come up with a reset of exactly this shape, and the vendor's
// timings are the ones written down here (10/100 ms for the panel, 100/200 ms
// for the touch) - a reset that is too short leaves the controller half awake,
// which looks like a panel that lights up but never draws.
bool ch422gPulse(uint8_t pin, bool activeLow, uint32_t lowMs, uint32_t highMs);

// Whether the chip answered. True only after a successful ch422gBegin().
bool ch422gFound();

#else

// A board without an expander gets the same three calls and does nothing, so
// that the places which need a line do not have to know which board they are on.
inline bool ch422gBegin() { return false; }
inline bool ch422gWrite(uint8_t, bool) { return false; }
inline bool ch422gPulse(uint8_t, bool, uint32_t, uint32_t) { return false; }
inline bool ch422gFound() { return false; }

#endif // BOARD_CH422G_PRESENT

#endif // RCT_BOARD_CH422G_H