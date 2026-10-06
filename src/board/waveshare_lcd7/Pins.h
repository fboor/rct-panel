// Pin map of the Waveshare ESP32-S3-Touch-LCD-7 (with touch), 800 x 480.
//
// SOURCE. Every number below is from the vendor, not estimated:
//   - the pin table in docs.waveshare.com/ESP32-S3-Touch-LCD-7
//   - esp_panel_board_custom_conf.h out of the vendor's own demo
//     (github.com/waveshareteam/ESP32-S3-Touch-LCD-7, examples/Arduino/examples/
//      10_lvgl_v9_demo), which is the file their build actually compiles
// The wiki numbers the panel's own data bits R3..R7; the library maps the same wires
// for RGB565 as DATA0..DATA15. Both describe one wiring, and the library's is the
// one esp_lcd wants.
//
// NOT A RUNNING BOARD YET. This profile exists so the numbers are in one place and
// the host test can check them, and the build below stops with a #error until the
// three pieces it still lacks are written. Marking it "done" instead would be the
// worse mistake: a firmware that builds and shows nothing is harder to diagnose than
// one that refuses to build.
//
//   1. the ST7262 vendor init sequence (the panel here is an ST7262, ours an ST7701S)
//   2. a CH422G driver - the backlight and the SD chip select are on it, not on GPIOs
//   3. the ST7262 RGB timings instead of the ST7701 ones in Display.cpp
//
// SPDX-License-Identifier: MIT
#ifndef RCT_BOARD_WAVESHARE_PINS_H
#define RCT_BOARD_WAVESHARE_PINS_H

// ---- LCD, ST7262, RGB565 over the RGB peripheral ---------------------------------
#define PIN_LCD_HSYNC 46
#define PIN_LCD_VSYNC 3
#define PIN_LCD_DE 5
#define PIN_LCD_PCLK 7

// RGB565 data lines in esp_lcd data_gpio_nums order (B0..B4, G0..G5, R0..R4).
#define PIN_LCD_B0 14
#define PIN_LCD_B1 38
#define PIN_LCD_B2 18
#define PIN_LCD_B3 17
#define PIN_LCD_B4 10
#define PIN_LCD_G0 39
#define PIN_LCD_G1 0
#define PIN_LCD_G2 45
#define PIN_LCD_G3 48
#define PIN_LCD_G4 47
#define PIN_LCD_G5 21
#define PIN_LCD_R0 1
#define PIN_LCD_R1 2
#define PIN_LCD_R2 42
#define PIN_LCD_R3 41
#define PIN_LCD_R4 40

// The vendor's timings. Ours are 8/50/10 and 8/20/10 at 12 MHz with the clock on
// the rising edge; these are 4/8/8 and 4/8/8 at 16 MHz with the clock on the
// falling edge. The polarity is the one that would be missed if it were left out -
// PCLK_ACTIVE_NEG 1 in the vendor config.
#define BOARD_LCD_PCLK_HZ (16 * 1000 * 1000)
#define BOARD_LCD_PCLK_ACTIVE_NEG 1
#define BOARD_LCD_HSYNC_PULSE_WIDTH 4
#define BOARD_LCD_HSYNC_BACK_PORCH 8
#define BOARD_LCD_HSYNC_FRONT_PORCH 8
#define BOARD_LCD_VSYNC_PULSE_WIDTH 4
#define BOARD_LCD_VSYNC_BACK_PORCH 8
#define BOARD_LCD_VSYNC_FRONT_PORCH 8

// ---- I2C bus: the GT911 and the CH422G expander share it -----------------------
// The vendor's demo opens it with Wire.begin(8, 9), so these are the bus, not just
// the touch. The touch is the 4th device on it.
#define PIN_TOUCH_SDA 8
#define PIN_TOUCH_SCL 9
#define TOUCH_I2C_ADDR 0x5D      // the same GT911 address ours uses
#define TOUCH_I2C_ADDR_ALT 0x14
#define BOARD_TOUCH_HAS_INT 1   // GPIO 4; the vendor config uses it, we still poll
#define PIN_TOUCH_INT 4

// No separate SPI link for the vendor init: an RGB panel is configured over the RGB
// bus itself, so there are no CS/SCK/MOSI wires on this board. -1 rather than
// undefined, because the board test and Display.cpp both name them.
#define PIN_LCD_CS -1
#define PIN_LCD_SCK -1
#define PIN_LCD_MOSI -1

// ---- backlight over the CH422G: on or off, and that is all ----------------------
#define PIN_CH422G_SDA 8
#define PIN_CH422G_SCL 9
#define BOARD_CH422G_PRESENT 1
// EXIOn is CH422G pin n - confirmed against the vendor config, which puts the
// backlight on pin 2 while the wiki calls that same wire EXIO2.
#define BOARD_CH422G_PIN_BACKLIGHT 2
#define BOARD_CH422G_PIN_SD_CS 4
#define BOARD_CH422G_PIN_TOUCH_RST 1
#define BOARD_CH422G_PIN_USB_SEL 5
#define BOARD_BACKLIGHT_ON_LEVEL 1
// The expander has one bit per output. There is no PWM behind it, so this board
// cannot dim - and Backlight.cpp asks before it offers a slider that does nothing.
#define BOARD_BACKLIGHT_DIMMABLE 0

// ---- TF slot: SPI on real pins, chip select on the expander --------------------
#define PIN_SD_SCK 12
#define PIN_SD_MISO 13
#define PIN_SD_MOSI 11
#define PIN_SD_CS -1            // not a GPIO here; see BOARD_CH422G_PIN_SD_CS

// ---- no switched output on this board -------------------------------------------
#define RELAY_PIN -1
#define BOARD_RELAY_PRESENT 0

// The resolution this board's own layout answers for. The board test uses it
// to pick that layout out of uiLayoutForSize() - the firmware's uiLayout()
// returns whichever board was compiled in, which in a test build is always
// the first one.
#define BOARD_SCREEN_W 800
#define BOARD_SCREEN_H 480

// ---- and the three pieces it still lacks ----------------------------------------
// BOARD_TEST_IGNORE_INCOMPLETE lets tools/board_test compile the profile anyway, to
// check the numbers above today rather than on the day the hardware arrives. It is
// set by that one test and by nothing else - a real build of this board stops here.
#ifndef BOARD_TEST_IGNORE_INCOMPLETE
#error "ESP32-S3-Touch-LCD-7: pins are mapped, the board does not run yet. Missing: the ST7262 vendor init, the CH422G driver, and the ST7262 RGB timings in Display.cpp. See the head of this file."
#endif

#endif // RCT_BOARD_WAVESHARE_PINS_H