// 480x480 ST7701 display: config via 3-wire SPI, pixels via the ESP32-S3
// LCD_CAM RGB interface (esp_lcd), rendered by LVGL.
#ifndef DISPLAY_H
#define DISPLAY_H

#include <lvgl.h>

// Power up the panel and create the LVGL display. Must be called once after
// the WiFi config portal (so the panel can show connection status) and before
// any LVGL object creation.
bool displayInit();

// LVGL heartbeat; call from the main loop.
void displayLooper();

// Handle used internally; keeps the compiler honest in gui/.
lv_display_t *dispGetHandle();

#endif // DISPLAY_H