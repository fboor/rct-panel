// Pin map of the Guition ESP32-S3 4848S040 - the panel this project started on.
//
// These are the numbers that were in src/display/DisplayPins.h, plus the SD card's
// four and the relay's one, which were elsewhere and are recorded here now so that
// a board is described in one file. Nothing is altered: every value is the one the
// firmware has been running with, and 480 x 480 renders pixel-identical after the
// move (the check is in the commit, not promised).
//
// LCD: ST7701S, 480x480, RGB-16bit, configured over a 3-wire 9-bit SPI.
// Touch: GT911 on I2C, address 0x5D, neither INT nor RST wired.
// SPDX-License-Identifier: MIT
#ifndef RCT_BOARD_GUITION_PINS_H
#define RCT_BOARD_GUITION_PINS_H

#define PIN_LCD_DE 18
#define PIN_LCD_VSYNC 17
#define PIN_LCD_HSYNC 16
#define PIN_LCD_PCLK 21

// RGB565 data lines, order = esp_lcd data_gpio_nums index order
// (R0..R4, G0..G5, B0..B4)
#define PIN_LCD_R0 11
#define PIN_LCD_R1 12
#define PIN_LCD_R2 13
#define PIN_LCD_R3 14
#define PIN_LCD_R4 0
#define PIN_LCD_G0 8
#define PIN_LCD_G1 20
#define PIN_LCD_G2 3
#define PIN_LCD_G3 46
#define PIN_LCD_G4 9
#define PIN_LCD_G5 10
#define PIN_LCD_B0 4
#define PIN_LCD_B1 5
#define PIN_LCD_B2 6
#define PIN_LCD_B3 7
#define PIN_LCD_B4 15

// SPI link used to configure the ST7701 (3-wire 9-bit, no D/C line).
#define PIN_LCD_CS 39
#define PIN_LCD_SCK 48
#define PIN_LCD_MOSI 47

// Backlight (active high). Driven as LEDC PWM, 1 kHz / 10 bits, since the panel
// dims itself after a few minutes without a touch - see Backlight.h.
#define PIN_LCD_BL 38
// Whether the backlight can be dimmed at all. The Waveshare board switches it over
// an IO expander and can only say on or off, and Backlight.cpp asks before it
// promises a dimming that would not happen.
#define BOARD_BACKLIGHT_DIMMABLE 1

// GT911 touch controller. Neither INT nor RST is wired on this board, which is why
// Touch.cpp polls instead of waiting.
#define PIN_TOUCH_SDA 19
#define PIN_TOUCH_SCL 45
#define TOUCH_I2C_ADDR 0x5D      // primary (0x14 is the alternative)
#define TOUCH_I2C_ADDR_ALT 0x14
#define BOARD_TOUCH_HAS_INT 0

// TF slot (see docs/sd-history.md section 1). The chip select is a real GPIO here.
#define PIN_SD_SCK 48
#define PIN_SD_MISO 41
#define PIN_SD_MOSI 47
#define PIN_SD_CS 42

// The 1-way relay port of the 4848S040 (silkscreen "1Way/3WayRelayPort"). Measured
// on the wall on 2026-09-30: the pin sits HIGH while idle and the output follows a
// HIGH, so the module is high-level triggered.
#define RELAY_PIN 40
#define RELAY_ACTIVE_LOW 0
#define BOARD_RELAY_PRESENT 1

// The resolution this board's own layout answers for. The board test uses it
// to pick that layout out of uiLayoutForSize() - the firmware's uiLayout()
// returns whichever board was compiled in, which in a test build is always
// the first one.
#define BOARD_SCREEN_W 480
#define BOARD_SCREEN_H 480

#if RELAY_ACTIVE_LOW
#define RELAY_LEVEL_ON LOW
#define RELAY_LEVEL_OFF HIGH
#else
#define RELAY_LEVEL_ON HIGH
#define RELAY_LEVEL_OFF LOW
#endif

#endif // RCT_BOARD_GUITION_PINS_H