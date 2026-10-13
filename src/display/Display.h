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

// Frames the panel has shown since boot, and the rate over the last call. The
// timings say what that rate must be - the scan reads the frame buffer from PSRAM
// once per frame, and PSRAM is also where the network stack and LVGL keep their
// buffers. A rate below the theoretical one means the scan did not get its data,
// which looks on glass like flicker and has nothing to do with what is drawn.
uint32_t displayFrameCount();
float displayFrameRate();

// The longest time a single draw took since the last call, in milliseconds, plus
// the number of draws in that time and their total. The total is the measure
// that matters here: it is the time the PSRAM spends being written to while the
// scan is reading it, and it is what a change in where LVGL's buffer lives shows
// up in. The driver copies the area into the frame buffer and then writes the
// whole buffer back out of the CPU cache, so this is not the size of the area but
// the size of the frame buffer - against a frame that is 26 ms long. Returns 0
// before the first call.
uint32_t displaySlowestDrawMs(uint32_t *draws, uint32_t *totalMs);

// Handle used internally; keeps the compiler honest in gui/.
lv_display_t *dispGetHandle();

// The panel's per-channel colour correction, as applied by the flush callback.
// A screenshot renders LVGL's own buffer, i.e. *before* this correction, so
// running the values through the same function is what makes the file show what
// the panel actually shows rather than what LVGL asked for.
uint16_t dispCorrectPixel(uint16_t rgb565);

#endif // DISPLAY_H