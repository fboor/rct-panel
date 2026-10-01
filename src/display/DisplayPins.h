// Pin mapping for the Guition ESP32-S3 4848S040 (verified against the vendor
// board diagram; see README "Hardware bring-up notes" for cross-references).
//
// LCD: ST7701S, 480x480, RGB-16bit interface, config via 3-wire 9-bit SPI.
// Touch: GT911 on I2C (address 0x5D, no RST/INT wiring).
#ifndef DISPLAY_PINS_H
#define DISPLAY_PINS_H

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

// GT911 touch controller.
#define PIN_TOUCH_SDA 19
#define PIN_TOUCH_SCL 45
#define TOUCH_I2C_ADDR 0x5D      // primary (0x14 is the alternative)
#define TOUCH_I2C_ADDR_ALT 0x14

#endif // DISPLAY_PINS_H