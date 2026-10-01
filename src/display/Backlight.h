// Backlight of the 4848S040 (GPIO38, active high) - see Backlight.cpp for the
// timing and the reasons behind it.
//
// The panel hangs on a wall in a room where nobody stands in front of it all
// day. The light therefore follows the rule a screen lock follows: no touch for
// three minutes and it dims, no touch for five and it is off, the first touch
// brings it back. Nothing else is configurable - see the comments there.
//
// SPDX-License-Identifier: MIT
#ifndef BACKLIGHT_H
#define BACKLIGHT_H

#include <Arduino.h>

// Drive the light with LEDC PWM at 1 kHz and turn it on at full brightness.
void backlightInit();

// Someone is there: full brightness again and the idle timers start from zero.
// This is the *only* thing that wakes the panel, so the call site is the touch
// read callback; every press counts, including one that starts with a finger
// already resting on the glass.
void backlightActivity();

// Whether a touch can reach the backlight at all. Without the touch controller
// there is no way back out of "off", so the light then stays on for good. Pass
// touchInit()'s result.
void backlightSetWakeable(bool wakeable);

// Evaluate the idle timers and ramp the duty towards the level they ask for.
// Idempotent and cheap (a subtraction, a few comparisons, one division), so it
// may be called as often as one likes - from displayLooper(), i.e. once per
// LVGL cycle, which also keeps the timers running while something else holds
// the main task (inverter poll, web download).
void backlightUpdate();

// Brightness in per cent (rounded), for the log line.
uint8_t backlightPercent();

#endif // BACKLIGHT_H