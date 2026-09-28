// GT911 capacitive touch controller (polled; no IRQ/RST wired on this board).
#ifndef TOUCH_H
#define TOUCH_H

#include <Arduino.h>
#include <lvgl.h>

// Probe and configure the controller on I2C. Returns true if a GT911 was found.
bool touchInit();

// Read the current touch state into the LVGL indev read callback.
void touchReadCb(lv_indev_t *indev, lv_indev_data_t *data);

#endif // TOUCH_H