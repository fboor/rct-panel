// Pin map of the Waveshare ESP32-S3-Touch-LCD-7 **with touch**, 800 x 480.
//
// NOT THE 7B. Waveshare sells four boards under that one name with "LCD-7" in
// it, and they are not interchangeable:
//
//   ESP32-S3-Touch-LCD-7   800 x 480   this file
//   ESP32-S3-LCD-7         800 x 480   no touch, no GT911 on the bus
//   ESP32-S3-Touch-LCD-7B  1024 x 600  and EXIO6 = LCD_VDD_EN, which does not
//                                    exist here
//   ESP32-S3-LCD-7B        1024 x 600
//
// The 7B shares this board's RGB data pins to the number, so a profile written
// for the wrong one builds, passes a board test that checks the resolution it
// claims, and puts an 800-line picture on a 1024-line panel. The revision is in
// the profile's name for that reason: a 7B is a second profile, not a variant
// of this one.
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
// WHAT THE FIRST VERSION OF THIS FILE GOT WRONG, and what the hardware needs
// beyond the pins. The list used to name an "ST7262 vendor init sequence" as the
// first missing piece. There is none: the ST7262 sits on the RGB bus like any
// other RGB panel, and the vendor's driver hands the timings to esp_lcd and stops
// - configVendorCommands() is opt-in and their demo does not call it. What the
// panel does need is a reset, and that reset is not on a GPIO: it is EXIO3 on the
// expander, pulsed low for 10 ms and high for 100 ms, which their LCD_PRE_BEGIN
// hook does before the panel object is created. Without it the panel lights and
// draws nothing.
//
// What is written now: the CH422G driver (src/board/Ch422G.cpp), the per-board
// reset and timings in Display.cpp, and the chip select of the TF card (EXIO4).
//
// SPDX-License-Identifier: MIT
#ifndef RCT_BOARD_WAVESHARE_TOUCH_LCD7_PINS_H
#define RCT_BOARD_WAVESHARE_TOUCH_LCD7_PINS_H

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
#define BOARD_LCD_CTRL_ST7262 1
// The order of the sixteen data lines as the panel's wires carry them: blue
// first. The vendor's own table numbers them DATA0..DATA15 = B0..B4, G0..G5,
// R0..R4, and esp_lcd wants index 0 to be the pin for bit 0 of the pixel value -
// so on this board index 0 is blue. The Guition is the other way round and its
// ST7701 is told about it in a register instead.
// The bounce buffer, in pixels: ten lines of the panel, which is what the
#define BOARD_LCD_DATA_ORDER_BGR 1
// Pixel clock: the vendor's 16 MHz, with its inverted edge, and that is also the
// panel's floor - at 12 MHz, with the porches below, the picture came on with
// colours that wander. The porches are 4/8/8 horizontally and 4/16/16 vertically;
// the vertical ones were 8/8 until the panel's own colours said so.
//
// What is left at 16 MHz is the scan's share of the PSRAM: 800 * 480 * 2 * 37.8 =
// 29 MB/s of continuous read, out of the same PSRAM LVGL renders into and the
// network stack works in. A line that arrives late is what the panel shows as the
// picture jumping - the colours stay right, which is how a late line is told from
// a panel out of sync. Neither of the two escapes in this file helps: the clock
// cannot go lower (see above) and the frame buffer cannot leave the PSRAM (768 kB
// out of 512 kB of SRAM).
#define BOARD_LCD_PCLK_HZ (16 * 1000 * 1000)
#define BOARD_LCD_PCLK_ACTIVE_NEG 1
#define BOARD_LCD_HSYNC_PULSE_WIDTH 4
#define BOARD_LCD_HSYNC_BACK_PORCH 8
#define BOARD_LCD_HSYNC_FRONT_PORCH 8
#define BOARD_LCD_VSYNC_PULSE_WIDTH 4
#define BOARD_LCD_VSYNC_BACK_PORCH 16
#define BOARD_LCD_VSYNC_FRONT_PORCH 16
// LVGL's draw buffer, in lines of the panel. 24 lines are 38 kB on this board at
// RGB565, and that buffer is the one piece of the redraw that does not have to
// live in the PSRAM: the driver copies every strip into the frame buffer itself,
// so LVGL rendering into SRAM saves one of the three passes a page switch makes
// over the memory the scan reads 29 MB/s out of. 24 and not more because the
// internal heap has to keep room for the network stack - 40 lines left 11 kB.
#define BOARD_LCD_BUF_ROWS 24
// No bounce buffer. The vendor's demo asks for one (ten lines of the panel, in
// PSRAM) and the field it goes into does not exist in the esp_lcd this project
// builds against: bounce_buffer_size_px came with a later esp_lcd than the
// Arduino-ESP32 core 3.x carries. So a tearing RGB image is what this board gets,
// and the fix is a core upgrade rather than a line of configuration.

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

// ---- CH422G: four of the board's functions hang on it --------------------------
#define PIN_CH422G_SDA 8
#define PIN_CH422G_SCL 9
#define BOARD_CH422G_PRESENT 1
// The chip answers at 0x20; the wiki's address map shows the CH422G occupying
// 0x20..0x27 and the GT911 at 0x5D. The bus runs at 400 kHz in the vendor config.
#define BOARD_CH422G_I2C_ADDR 0x20 // unbenutzt: siehe Ch422G.cpp
#define BOARD_CH422G_I2C_HZ 400000
// EXIOn is CH422G pin n. The backlight (pin 2) is in the board's own backlight
// block below, which says which of the two worlds this board is in; the four
// here are what the firmware drives.
#define BOARD_CH422G_PIN_TOUCH_RST 1 // TP_RST; their demo sets it high before the LCD
#define BOARD_CH422G_PIN_LCD_RST 3   // the ST7262's reset: 10 ms low, 100 ms high
#define BOARD_CH422G_PIN_SD_CS 4     // SD_CS, active low
#define BOARD_CH422G_PIN_USB_SEL 5   // low = USB on GPIO 19/20, high = CAN
// ---- the one line that says how this board is built --------------------------
// The boost chip's enable net is an input while the board comes up - the board
// holds it there - and what happens to that net afterwards is the driver's
// business. With the wire from GPIO 6 to it, the light takes the same PWM and the
// same three timers as every other board.
//
// Which of the two worlds this board is in cannot be decided at runtime and
// cannot be decided by the firmware, because the two cannot be used at once:
//
//   1 = the mod is done and the trace between EXIO2 and the enable input is cut.
//       EXIO2 then drives nothing that is connected, the expander keeps EXIO1, 3,
//       4 and 5 for the touch reset, the panel's reset, the TF card's chip select
//       and the USB selector, and GPIO 6 dims the light. Everything works.
//   0 = no cut. EXIO2 is still on that net, the light belongs to it, and a PWM
//       from GPIO 6 would put two push-pull outputs on one net for as long as they
//       disagree. The light is switched there instead: on and off, without the
//       three-minute step.
//
// The direction of the expander's IO0..7 is one direction for all eight - "the
// input/output mode of CH422G's IO0-7 must remain consistent" in the vendor's own
// driver header, "Input is supported only on pins 0-7" in ESPHome's component -
// and open-drain exists only for pins 8..11. So EXIO2 is an output whenever any of
// the other four is, and there is no software way to leave it alone.
//
// 1 is right for the board in the flat layer's hand. It is wrong for anyone who
// flashes a factory board, and the difference is a short, not a colour.
#define BOARD_CH422G_LCD_DISP_CUT 1

// The level every EXIO0..7 pin drives after the expander is brought up, and with
// it the board's own start-up line for each of the five pins this firmware uses:
// touch reset high (out of reset), panel reset high (out of reset), chip select
// low (the card is held selected) and USB low (USB, not CAN). The last two are
// the ones that would be wrong by default - the chip comes up with all twelve
// high - so they are written here rather than left to the default.
//
// EXIO2 is the fifth and follows the board: with the cut it goes nowhere, so it
// takes the off level and a reader of the log can tell from the two values that
// the light is GPIO 6's; without the cut it is the light, and it is high.
#if BOARD_CH422G_LCD_DISP_CUT
#define BOARD_CH422G_START_IO 0x17    // EXIO0,1,3,4 high, EXIO2 und EXIO5 low
#else
#define BOARD_CH422G_START_IO 0x1B    // dazu EXIO2 high: das Licht haengt daran
#endif

// ---- backlight --------------------------------------------------------------
// Lit when the enable is high, in both of the worlds above.
#define BOARD_BACKLIGHT_ON_LEVEL 1
#if BOARD_CH422G_LCD_DISP_CUT
#define PIN_LCD_BL 6
#define BOARD_BACKLIGHT_DIMMABLE 1
#else
#define PIN_LCD_BL -1
#define BOARD_BACKLIGHT_DIMMABLE 0
#define BOARD_CH422G_PIN_BACKLIGHT 2 // DISP, the panel's backlight enable, active high
#endif

// ---- TF slot: SPI on real pins, chip select on the expander --------------------
// The card's chip select is EXIO4 and there is no GPIO behind it. What the vendor's
// own SD demo does with that (SD_SS = -1, CS driven low once at startup) does not
// transfer to the Arduino SD class: it takes its chip select as a uint8_t, so -1
// arrives as 255 and the library asks pinMode() for pin 255 - "Invalid pin
// selected", then "Card Failed", once per command, for as long as the mount is
// retried. There is no pin number to hand it, so the card stays unused until the
// mount goes through the IDF's SD/MMC driver, which takes gpio_cs = -1.
#define BOARD_SD_CS_IS_GPIO 0
#define PIN_SD_SCK 12
#define PIN_SD_MISO 13
#define PIN_SD_MOSI 11
#define PIN_SD_CS -1            // not a GPIO here; see BOARD_CH422G_PIN_SD_CS

// ---- switched output --------------------------------------------------------
// It goes to EXIO2, the pin the backlight used, once the enable net is cut
// (BOARD_CH422G_LCD_DISP_CUT 1) - that is what the cut frees up. Not GPIO 8 or 9:
// those are SDA and SCL, and the GT911 and the CH422G both sit on that bus, so a
// relay there would take the touch, the panel's reset, the TF card's chip select
// and the USB selector with it. The board still labels GPIO 8 as I2C on its
// silkscreen, which is true of the pin and not of what a firmware may do with it.
//
// What is connected to it is never a coil, always a module whose input side is an
// optocoupler LED and whose contact side is separated from that. So the question
// the CH422G's I/O figures would have had to answer - how many milliamperes an IO
// pin can deliver - does not arise: what it drives is a logic input with a
// resistor in front of it, in the same league as the panel's reset line one pin
// over. The coil current never passes through the board; it is drawn from the
// module's own supply on the isolated side.
//
// The output is off for now (BOARD_RELAY_PRESENT 0) because the module is not
// decided yet, not because of a technical doubt - turning it on means the flag,
// RELAY_PIN below, and a relayWrite() that goes through the expander instead of
// pinMode()/digitalWrite(), the same way Backlight.cpp has two paths.
//
// RELAY_PIN is -1 rather than a number, the same "not a GPIO here" the TF card's
// chip select uses: writing 8 into it would say that the firmware drives SDA.
#define RELAY_PIN -1
#define RELAY_ACTIVE_LOW 0
#define BOARD_RELAY_PRESENT 0
// No BOARD_RELAY_SHARES_I2C any more: that flag described a relay on GPIO 8,
// which this board does not have. The output hangs on an optocoupler module's
// input, which is a logic input and belongs on EXIO2 once the cut above is done.

#if RELAY_ACTIVE_LOW
#define RELAY_LEVEL_ON LOW
#define RELAY_LEVEL_OFF HIGH
#else
#define RELAY_LEVEL_ON HIGH
#define RELAY_LEVEL_OFF LOW
#endif

// The resolution this board's own layout answers for. The board test uses it
// to pick that layout out of uiLayoutForSize() - the firmware's uiLayout()
// returns whichever board was compiled in, which in a test build is always
// the first one.
#define BOARD_SCREEN_W 800
#define BOARD_SCREEN_H 480

// ---- and the guard ------------------------------------------------------------
// The profile builds now, which is not the same as the board running: no firmware
// of this project has been on this hardware yet. What is still open is written
// down above - the relay pin, and whether the backlight mod leaves EXIO2 alone or
// puts a second driver on the enable net.
//
// BOARD_TEST_IGNORE_INCOMPLETE no longer exists and with it the host test's
// excuse for compiling this profile anyway: there is nothing left to excuse.

#endif // RCT_BOARD_WAVESHARE_TOUCH_LCD7_PINS_H