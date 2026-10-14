// 480x480 ST7701 display driver for the Guition ESP32-S3 4848S040.
//
// The panel is wired as: configuration via 3-wire 9-bit SPI (CS/SCK/MOSI, no
// D/C line), pixels via the ESP32-S3 LCD_CAM parallel RGB interface driven by
// esp_lcd (IDF component shipped inside the Arduino-ESP32 core, Apache-2.0).
// LVGL renders into two partial frame buffers and flushes them through
// esp_lcd_panel_draw_bitmap().
//
// The ST7701 init sequence is the widely used sequence from the Arduino_GFX
// project (MIT license) as transcribed in the Tasmota discussion #20527.
// See NOTICE.
//
// SPDX-License-Identifier: MIT
#include "Display.h"

#include "../Diag.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <esp_lcd_panel_ops.h>
// esp_lcd_panel_rgb.h from tools/sdk, which is the revision the linked library was
// built from - see the comment above panel_cfg for what the newer revision in the
// -libs package can and this one cannot.
#include <esp_lcd_panel_rgb.h>
#include <hal/lcd_types.h>

#include <math.h>

#include "Backlight.h"
#include "board/BoardPins.h"
#include "board/Ch422G.h"

// The resolution, the pixel clock and the porches are the board's, not this
// file's. They were three constants here until a second panel made the profile
// the only place where a number about a display belongs; on the Guition they are
// the same values as before, to the byte.
#define LCD_H_RES BOARD_SCREEN_W
#define LCD_V_RES BOARD_SCREEN_H

// ---------------------------------------------------------------------------
// Driver state
// ---------------------------------------------------------------------------
static esp_lcd_panel_handle_t s_lcd_panel = nullptr;
static lv_display_t *s_lv_disp = nullptr;

// ---------------------------------------------------------------------------
// ST7701 init sequence (command, payload). delay_ms runs after the command.
// Only this controller has one: the ST7262 on the Waveshare is configured over
// the RGB bus and takes no commands at all, so everything below - and the 3-wire
// link that carries it - belongs to the Guition alone.
// ---------------------------------------------------------------------------
#if BOARD_LCD_CTRL_ST7701

// Bring-up: the ST7701 loads VCOM (and factory settings) from OTP at reset.
// Our init then overwrites VCOM with the generic reference value (0xB1 0x32
// in BK1). Tested using the panel's OTP VCOM: the dark-end red/violet cast
// persisted, so VCOM is not the cause (and the OTP may be unprogrammed).
// Keeping the reference value; unit 0 = 0x32 baseline.
#define LCD_USE_OTP_VCOM 0

typedef struct {
  uint8_t cmd;
  const uint8_t *data;
  size_t len;
  uint32_t delay_ms;
} st7701_cmd_t;

static const uint8_t d_ff_bk0[] = {0x77, 0x01, 0x00, 0x00, 0x10};
static const uint8_t d_c0[] = {0x3B, 0x00};
static const uint8_t d_c1[] = {0x0D, 0x02};
static const uint8_t d_c2[] = {0x31, 0x05};
static const uint8_t d_cd[] = {0x00}; // COLCTRL: MDT=0 (vendor demo value - normal 16-bit RGB bus mapping)
static const uint8_t d_b0_gamma[] = {0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07,
                                     0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA,
                                     0x31, 0x18};
static const uint8_t d_b1_gamma[] = {0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08,
                                     0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9,
                                     0x32, 0x18};
static const uint8_t d_ff_bk1[] = {0x77, 0x01, 0x00, 0x00, 0x11};
static const uint8_t d_b0_vop[] = {0x60};
#if !LCD_USE_OTP_VCOM
static const uint8_t d_b1_vcom[] = {0x32};
#endif
static const uint8_t d_b2_vgh[] = {0x07};
static const uint8_t d_b3[] = {0x80};
static const uint8_t d_b5_vgl[] = {0x49};
static const uint8_t d_b7[] = {0x85};
static const uint8_t d_b8[] = {0x21};
static const uint8_t d_c1_1[] = {0x78};
static const uint8_t d_c2_1[] = {0x78};
static const uint8_t d_e0[] = {0x00, 0x1B, 0x02};
static const uint8_t d_e1[] = {0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0,
                               0x00, 0x00, 0x00, 0x44, 0x44};
static const uint8_t d_e2[] = {0x11, 0x11, 0x44, 0x44, 0xED, 0xA0,
                               0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00};
static const uint8_t d_e3[] = {0x00, 0x00, 0x11, 0x11};
static const uint8_t d_e4[] = {0x44, 0x44};
static const uint8_t d_e5[] = {0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0,
                               0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0};
static const uint8_t d_e6[] = {0x00, 0x00, 0x11, 0x11};
static const uint8_t d_e7[] = {0x44, 0x44};
static const uint8_t d_e8[] = {0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0,
                               0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0};
static const uint8_t d_eb[] = {0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40};
static const uint8_t d_ec[] = {0x3C, 0x00};
static const uint8_t d_ed[] = {0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF,
                               0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA};
static const uint8_t d_ff_bk3[] = {0x77, 0x01, 0x00, 0x00, 0x13};
static const uint8_t d_e5_1[] = {0xE4};
static const uint8_t d_ff_bk0a[] = {0x77, 0x01, 0x00, 0x00, 0x00};
static const uint8_t d_madctl[] = {0x08}; // BGR: un-swap R/B on this panel's bus
static const uint8_t d_3a[] = {0x60}; // RGB666, matching the vendor demo (MDT=0)

static const st7701_cmd_t st7701_init[] = {
    {0x01, nullptr, 0, 6}, // Software Reset + settle (ESPHome does this first)
    {0xFF, d_ff_bk0, sizeof(d_ff_bk0), 0}, {0xC0, d_c0, sizeof(d_c0), 0},
    {0xC1, d_c1, sizeof(d_c1), 0},         {0xC2, d_c2, sizeof(d_c2), 0},
    {0xCD, d_cd, sizeof(d_cd), 0},
    {0xB0, d_b0_gamma, sizeof(d_b0_gamma), 0},
    {0xB1, d_b1_gamma, sizeof(d_b1_gamma), 0},
    {0xFF, d_ff_bk1, sizeof(d_ff_bk1), 0}, {0xB0, d_b0_vop, sizeof(d_b0_vop), 0},
#if !LCD_USE_OTP_VCOM
    {0xB1, d_b1_vcom, sizeof(d_b1_vcom), 0},
#endif
    {0xB2, d_b2_vgh, sizeof(d_b2_vgh), 0}, {0xB3, d_b3, sizeof(d_b3), 0},
    {0xB5, d_b5_vgl, sizeof(d_b5_vgl), 0}, {0xB7, d_b7, sizeof(d_b7), 0},
    {0xB8, d_b8, sizeof(d_b8), 0},         {0xC1, d_c1_1, sizeof(d_c1_1), 0},
    {0xC2, d_c2_1, sizeof(d_c2_1), 0},     {0xE0, d_e0, sizeof(d_e0), 0},
    {0xE1, d_e1, sizeof(d_e1), 0},         {0xE2, d_e2, sizeof(d_e2), 0},
    {0xE3, d_e3, sizeof(d_e3), 0},         {0xE4, d_e4, sizeof(d_e4), 0},
    {0xE5, d_e5, sizeof(d_e5), 0},         {0xE6, d_e6, sizeof(d_e6), 0},
    {0xE7, d_e7, sizeof(d_e7), 0},         {0xE8, d_e8, sizeof(d_e8), 0},
    {0xEB, d_eb, sizeof(d_eb), 0},         {0xEC, d_ec, sizeof(d_ec), 0},
    {0xED, d_ed, sizeof(d_ed), 0},         {0xFF, d_ff_bk3, sizeof(d_ff_bk3), 0},
    {0xE5, d_e5_1, sizeof(d_e5_1), 0},     {0xFF, d_ff_bk0a, sizeof(d_ff_bk0a), 0},
    {0x3A, d_3a, sizeof(d_3a), 0},
    {0x36, d_madctl, sizeof(d_madctl), 0}, // MADCTL BGR: R/B un-swap (must be sent!)
    {0x11, nullptr, 0, 120}, // Sleep Out
    {0x29, nullptr, 0, 120}, // Display On
};

// ---------------------------------------------------------------------------
// ST7701 configuration link: bit-banged 3-wire 9-bit SPI (CS/SCK/MOSI, no D/C
// line). Each frame is one D/C bit (0 = command, 1 = parameter) followed by
// eight data bits, MSB first, SPI MODE3. This mirrors the field-tested ESPHome
// configuration for this exact board (spi_mode: MODE3) and the software-SPI
// used by the vendor reference (Arduino_GFX), except the clock idles HIGH and
// data is sampled on the rising edge of SCK. The esp_lcd SPI panel-io driver
// only emits plain 8-bit words, so the ST7701 would never see a valid frame
// through it.
// ---------------------------------------------------------------------------
static void lcdBitBangFrame(uint16_t frame) {
  for (int bit = 8; bit >= 0; bit--) {
    digitalWrite(PIN_LCD_SCK, LOW);  // falling edge of MODE3 clock
    digitalWrite(PIN_LCD_MOSI, (frame >> bit) & 0x1 ? HIGH : LOW);
    digitalWrite(PIN_LCD_SCK, HIGH); // rising edge: ST7701 samples
  }
}

static void lcdInitSendCmd(uint8_t cmd) { lcdBitBangFrame(cmd); }
static void lcdInitSendData(uint8_t data) { lcdBitBangFrame(0x100 | data); }

// ESPHome keeps CS low for the duration of each single 9-bit frame (it wraps
// write_command_/write_data_ in enable()/disable()); do the same so the
// ST7701 sees the exact same bus behaviour as the field-tested port.
static void lcdWriteInitCmd(const st7701_cmd_t *c) {
  digitalWrite(PIN_LCD_CS, LOW);
  lcdInitSendCmd(c->cmd);
  digitalWrite(PIN_LCD_CS, HIGH);
  for (size_t i = 0; i < c->len; i++) {
    digitalWrite(PIN_LCD_CS, LOW);
    lcdInitSendData(c->data[i]);
    digitalWrite(PIN_LCD_CS, HIGH);
  }
  if (c->delay_ms) {
    delay(c->delay_ms);
  }
}

#endif // BOARD_LCD_CTRL_ST7701

// ---------------------------------------------------------------------------
// Software per-channel colour correction (currently identity / unused).
//
// The panel's transfer was per-channel skewed (darks purple = R/B above G,
// light greys green = G above R/B) because the RGB bus mapping registers were
// wrong: we used COLCTRL/MDT=1 + 0x3A RGB565, while this 4848S040 panel needs
// MDT=0 + RGB666 (the vendor demo's values; verified by on-panel ramp sweep
// - mode 0 is the only neutral grey mapping of the four combinations). The
// fixed bus mapping removes the skew, so these LUTs stay identity. If a tint
// ever shows up again, darkBell() cuts R/B (and lifts G) in the dark end and
// highBell() cuts G in the light greys; g=0 and g=255 stay anchored.
// ---------------------------------------------------------------------------
static uint8_t s_corr_r5[32];
static uint8_t s_corr_g6[64];
static uint8_t s_corr_b5[32];
// Whether the tables do anything at all. They are the identity unless they are
// tuned, and running the flush callback's pixel loop over a large area for
// nothing measured 18 ms of a 26 ms frame - more than the frame, so a page switch
// tore on every pass even with the scan kept in step.
static bool s_corrAktiv = false;

static int corrDarkBell(int g) {
  if (g >= 80) {
    return 0;
  }
  return (int)(255.0f * sinf((float)M_PI * g / 80.0f) + 0.5f);
}

static int corrHighBell(int g) {
  if (g <= 112 || g >= 248) {
    return 0;
  }
  return (int)(255.0f * sinf((float)M_PI * (g - 112) / 136.0f) + 0.5f);
}

static uint8_t corrClamp8(int v) {
  return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static void corrBuild(int rD, int bD, int gD, int gH) {
  for (int i = 0; i < 32; i++) {
    int g = (i << 3) | (i >> 2); // 5-bit -> 8-bit expand
    int d = corrDarkBell(g);
    s_corr_r5[i] = (uint8_t)((corrClamp8(g - (rD * d * g) / (255 * 255)) * 31 + 127) / 255);
  }
  for (int i = 0; i < 64; i++) {
    int g = (i << 2) | (i >> 4); // 6-bit -> 8-bit expand
    int d = corrDarkBell(g);
    int h = corrHighBell(g);
    int cv = g + (gD * d * (255 - g)) / (255 * 255) - (gH * h * g) / (255 * 255);
    s_corr_g6[i] = (uint8_t)((corrClamp8(cv) * 63 + 127) / 255);
  }
  for (int i = 0; i < 32; i++) {
    int g = (i << 3) | (i >> 2);
    int d = corrDarkBell(g);
    s_corr_b5[i] = (uint8_t)((corrClamp8(g - (bD * d * g) / (255 * 255)) * 31 + 127) / 255);
  }
  s_corrAktiv = (rD != 0) || (bD != 0) || (gD != 0) || (gH != 0);
}

static inline uint16_t corrPixel(uint16_t p) {
  return (uint16_t)((s_corr_r5[(p >> 11) & 0x1F] << 11) |
                    (s_corr_g6[(p >> 5) & 0x3F] << 5) | s_corr_b5[p & 0x1F]);
}

uint16_t dispCorrectPixel(uint16_t rgb565) { return corrPixel(rgb565); }

static void corrBitmap(uint16_t *fb, size_t n) {
  for (size_t i = 0; i < n; i++) {
    fb[i] = corrPixel(fb[i]);
  }
}

static void corrIdentity() {
  for (int i = 0; i < 32; i++) {
    s_corr_r5[i] = (uint8_t)i;
    s_corr_b5[i] = (uint8_t)i;
  }
  for (int i = 0; i < 64; i++) {
    s_corr_g6[i] = (uint8_t)i;
  }
  s_corrAktiv = false;
}

// ---- WRITING EACH STRIP WHERE THE SCAN HAS ALREADY BEEN --------------------
//
// The driver scans the frame buffer it owns, and it owns exactly one: a strip
// written while the scan is somewhere else reaches the glass in two halves.
// That is the tearing on the 7-inch panel and it has nothing to do with the
// strip size, the refresh rate or the colours.
//
// The one thing the driver does say is when a frame has been scanned to the end
// (on_frame_trans_done, in ISR context). From that timestamp and the timings
// above it, the scan's line number is known at any moment - the driver scans a
// fixed number of pixel clocks per line. Writing a strip only once the scan has
// been past that strip puts the write behind the beam: the old pixels stay on
// glass until the next frame, then the new ones appear whole. Nobody sees a
// frame late by 21 ms; everybody sees a picture that holds still.
//
// The wait is at most one frame, and it is a wait in the LVGL task, not in the
// driver: the scan keeps running, so nothing else stalls behind it.
static volatile uint32_t s_scanFrameEndUs = 0; // esp_timer at the last frame end
static volatile uint32_t s_scanFrames = 0;      // frames the driver has reported
static uint32_t s_lineUs = 0;       // pixel clocks per line, in microseconds
static uint32_t s_frameLines = 0;   // lines per frame, porch included
static uint32_t s_drawMaxUs = 0;    // longest draw_bitmap() so far
static uint32_t s_drawTotalUs = 0;  // all of them, since the last report
static uint32_t s_flushes = 0;
static uint32_t s_flushesAtLastReport = 0;

// How many lines past the strip the write starts, and how long a flush waits for
// that at most. The margin absorbs how far the line estimate can be off - it
// counts from the frame the driver reported, and where in the frame that report
// lands is the driver's business. The bound is three frames on the 7-inch
// timings; anything longer would be a driver that reports nothing, and then the
// picture is written anyway rather than late.
#define BEAM_MARGIN_LINES 8
#define BEAM_MAX_WAIT_US 70000

static bool onScanFrameDone(esp_lcd_panel_handle_t /*panel*/,
                            esp_lcd_rgb_panel_event_data_t * /*edata*/,
                            void * /*ctx*/) {
  s_scanFrameEndUs = (uint32_t)esp_timer_get_time();
  s_scanFrames++;
  return false; // no high-priority task to wake
}

// Line the scan is on right now, counted from the frame it started. A line
// number at or above the height is inside the vertical blanking, which means the
// scan is above the whole screen again. Returns -1 if the driver has not
// reported a frame yet - the panel has none of these knobs working, and then
// writing is the only thing left to do.
static int scanLine() {
  if (s_scanFrames == 0 || s_lineUs == 0) {
    return -1;
  }
  uint32_t since = (uint32_t)esp_timer_get_time() - s_scanFrameEndUs;
  return (int)((since / s_lineUs) % s_frameLines);
}

// Frames shown since boot, and the rate over the window since the last call. The
// scan reads the frame buffer out of PSRAM once per frame and PSRAM also carries
// the network stack and LVGL's own buffers, so a rate below the one the timings
// give is the sign that the scan did not get its data in time. That looks on
// glass like flicker and has nothing to do with what is drawn.
uint32_t displayFrameCount() { return s_scanFrames; }

float displayFrameRate() {
  static uint32_t lastMs = 0;
  static uint32_t lastN = 0;
  uint32_t now = millis();
  uint32_t n = s_scanFrames;
  float fps = 0.0f;
  if (lastMs != 0 && n > lastN) {
    fps = (float)(n - lastN) * 1000.0f / (float)(now - lastMs);
  }
  lastMs = now;
  lastN = n;
  return fps;
}

uint32_t displaySlowestDrawMs(uint32_t *draws, uint32_t *totalMs) {
  if (draws != nullptr) {
    *draws = s_flushes - s_flushesAtLastReport;
  }
  if (totalMs != nullptr) {
    *totalMs = s_drawTotalUs / 1000;
  }
  s_flushesAtLastReport = s_flushes;
  s_drawTotalUs = 0;
  uint32_t ms = s_drawMaxUs / 1000;
  s_drawMaxUs = 0;
  return ms;
}

static void lcdFlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  // Phase names inside the flush callback: the heartbeat reported "lvgl" as
  // stuck for minutes without distinguishing LVGL's draw logic from this, and
  // esp_lcd's draw_bitmap() on an RGB panel blocks until the driver takes the
  // buffer. A 480 px high area against an 80-row draw buffer means this runs six
  // times per frame, so it is also where a per-frame cost would accumulate.
  diagPhase("lcd.flush");
  const uint32_t t0 = (uint32_t)esp_timer_get_time();
  uint16_t *px = (uint16_t *)px_map;
  size_t n = (size_t)(area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
  if (s_corrAktiv) { // nothing to correct: the loop is 18 ms of a 26 ms frame
    for (size_t i = 0; i < n; i++) {
      px[i] = corrPixel(px[i]);
    }
  }
  const uint32_t tCorr = (uint32_t)esp_timer_get_time() - t0;
  // Behind the beam: write only once the scan has passed the last line of this
  // strip, plus a margin for how far the line estimate can be off. Bounded, so a
  // driver that never reports a frame costs a fixed delay instead of a hang -
  // that is the case on a board whose driver has no such callback, and there the
  // wait is simply skipped (scanLine() == -1).
  const uint32_t y1 = (uint32_t)area->y1;
  const uint32_t rows = (uint32_t)(area->y2 - area->y1 + 1);
  // Wait for the scan to be somewhere other than on this strip. Where else it is
  // does not matter: writing behind it makes the change visible a frame later,
  // writing ahead of it makes it visible in this frame, and only writing while it
  // is on the strip shows half old and half new. Waiting for "past the strip"
  // instead - which is what this did at first - costs on average half a frame per
  // strip, and a page switch is twenty strips: that alone was most of its time.
  // The beam is on this strip for 24 of 516 lines, so the wait is about 1.3 ms.
  const int von = (int)y1 - BEAM_MARGIN_LINES;
  const int bis = (int)(y1 + rows) + BEAM_MARGIN_LINES;
  for (uint32_t waited = 0; waited < BEAM_MAX_WAIT_US; waited += 200) {
    int line = scanLine();
    if (line < 0 || line < von || line > bis) {
      break;
    }
    delayMicroseconds(200);
  }
  const uint32_t tWait = (uint32_t)esp_timer_get_time() - t0 - tCorr;
  esp_lcd_panel_draw_bitmap(s_lcd_panel, area->x1, area->y1, area->x2 + 1,
                            area->y2 + 1, px_map);
  const uint32_t tDraw = (uint32_t)esp_timer_get_time() - t0 - tCorr - tWait;
  // What a flush costs, because the question behind all of this is whether a strip
  // can be written faster than the scan reaches it: the driver copies the area
  // into the frame buffer and then writes the whole buffer back out of the CPU
  // cache, so the cost is not the area but the frame buffer - and a frame is 26 ms
  // long at 16 MHz. The heartbeat prints the slowest one (displaySlowestDrawMs).
  if (tDraw > s_drawMaxUs) {
    s_drawMaxUs = tDraw;
  }
  s_drawTotalUs += tDraw;
  s_flushes++;
  lv_display_flush_ready(disp);
  diagPhase("lcd.ready");
}

bool displayInit() {
  // LVGL must be initialized before any lv_* API (first use is
  // lv_display_create() below); lv_init() is safe to call repeatedly.
  lv_init();

  corrIdentity(); // correction LUTs are pass-through until/unless tuned

  // Backlight on, dimmable from here on (LEDC PWM, see Backlight.h).
  backlightInit();

#if BOARD_LCD_CTRL_ST7262
  // The ST7262 has no reset pin on a GPIO (the vendor's config has RST_IO = -1):
  // its reset is EXIO3 on the CH422G, pulsed low for 10 ms and high for 100 ms
  // before the panel object is created - the vendor does the same in the
  // LCD_PRE_BEGIN hook of their own board file. Without it the panel takes the
  // timings and stays blank, which is why the expander comes up first here and
  // not where the touch needs it.
  if (!ch422gBegin()) {
    Serial.println(F("LCD: kein IO-Expander - das Panel bleibt dunkel"));
  } else {
    // EXIO5 decides what GPIO 19/20 are: low is USB, high is CAN. The chip comes
    // up with every output driving high, so without this the board sits in CAN
    // mode and the USB port - the way this firmware gets onto the board in the
    // first place - answers nothing. The vendor's demo writes it low in the same
    // place (USB_SEL, low) and the wiki names the same two modes.
    ch422gWrite(BOARD_CH422G_PIN_USB_SEL, false);
    if (!ch422gPulse(BOARD_CH422G_PIN_LCD_RST, true, 10, 100)) {
      Serial.println(F("LCD: Reset am Expander nicht ausgefuehrt"));
    }
  }
#endif

#if BOARD_LCD_CTRL_ST7701
  // 1) 3-wire 9-bit configuration link: plain GPIOs, driven by software bit
  // banging (see lcdBitBangFrame above). The parallel RGB panel needs no
  // SPI/panel-io handle.
  pinMode(PIN_LCD_CS, OUTPUT);
  pinMode(PIN_LCD_SCK, OUTPUT);
  pinMode(PIN_LCD_MOSI, OUTPUT);
  digitalWrite(PIN_LCD_SCK, HIGH); // MODE3: clock idles high
  digitalWrite(PIN_LCD_CS, HIGH);   // deselected

  // 2) ST7701 controller configuration.
  for (size_t i = 0; i < sizeof(st7701_init) / sizeof(st7701_init[0]); i++) {
    lcdWriteInitCmd(&st7701_init[i]);
  }
#endif

// 3) Parallel RGB panel (LCD_CAM / esp_lcd).
  //
  // The order of these sixteen is the board's wiring, not a constant of the
  // project: esp_lcd puts bit 0 of the pixel value on index 0, and what bit 0
  // *means* on the panel's wires is the board's business.
  //
  // The Guition's wires carry red first, so R,G,B - and its ST7701 is told in a
  // register (MADCTL 0x08 above) that its own wires read BGR, which cancels out.
  // The Waveshare's wires carry blue first (the vendor's own table numbers them
  // DATA0..DATA15 = B0..B4, G0..G5, R0..R4) and its ST7262 has no register to
  // say so, so the order has to be B,G,R here. With R,G,B on that board orange
  // came out light blue while every neutral grey was right - a swapped pair of
  // colour channels, not a gamma curve.
#if BOARD_LCD_DATA_ORDER_BGR
  static const int data_gpios[] = {
      PIN_LCD_B0, PIN_LCD_B1, PIN_LCD_B2, PIN_LCD_B3, PIN_LCD_B4,
      PIN_LCD_G0, PIN_LCD_G1, PIN_LCD_G2, PIN_LCD_G3, PIN_LCD_G4, PIN_LCD_G5,
      PIN_LCD_R0, PIN_LCD_R1, PIN_LCD_R2, PIN_LCD_R3, PIN_LCD_R4,
  };
#else
  static const int data_gpios[] = {
      PIN_LCD_R0, PIN_LCD_R1, PIN_LCD_R2, PIN_LCD_R3, PIN_LCD_R4,
      PIN_LCD_G0, PIN_LCD_G1, PIN_LCD_G2, PIN_LCD_G3, PIN_LCD_G4, PIN_LCD_G5,
      PIN_LCD_B0, PIN_LCD_B1, PIN_LCD_B2, PIN_LCD_B3, PIN_LCD_B4,
  };
#endif

  esp_lcd_rgb_panel_config_t panel_cfg = {};
  panel_cfg.clk_src = LCD_CLK_SRC_PLL160M;
  // The timings are the board's. On the Guition these are the values that were
  // hard-wired here (12 MHz, 8/50/10, 8/20/10, data on the rising edge); the
  // Waveshare runs at 16 MHz with 4/8/8 porches and the clock on the falling
  // edge, and the polarity is the one that is invisible until the panel comes up
  // white.
  panel_cfg.timings.pclk_hz = BOARD_LCD_PCLK_HZ;
  panel_cfg.timings.h_res = LCD_H_RES;
  panel_cfg.timings.v_res = LCD_V_RES;
  panel_cfg.timings.hsync_pulse_width = BOARD_LCD_HSYNC_PULSE_WIDTH;
  panel_cfg.timings.hsync_back_porch = BOARD_LCD_HSYNC_BACK_PORCH;
  panel_cfg.timings.hsync_front_porch = BOARD_LCD_HSYNC_FRONT_PORCH;
  panel_cfg.timings.vsync_pulse_width = BOARD_LCD_VSYNC_PULSE_WIDTH;
  panel_cfg.timings.vsync_back_porch = BOARD_LCD_VSYNC_BACK_PORCH;
  panel_cfg.timings.vsync_front_porch = BOARD_LCD_VSYNC_FRONT_PORCH;
  panel_cfg.timings.flags.hsync_idle_low = 0; // active-low HSYNC (vendor demo)
  panel_cfg.timings.flags.vsync_idle_low = 0; // active-low VSYNC (vendor demo)
  panel_cfg.timings.flags.de_idle_high = 0;
  panel_cfg.timings.flags.pclk_active_neg = BOARD_LCD_PCLK_ACTIVE_NEG;
  panel_cfg.timings.flags.pclk_idle_high = 0;

  // The pixel format follows from the data width: 16 lines is RGB565, and the esp_lcd
  // this core links has no separate field for it (bits_per_pixel arrived with the
  // newer driver, together with the bounce buffer and the second frame buffer). So
  // there is no colour depth here that could be set wrong - 16 lines is RGB565 by
  // construction. What the format does not cover is the channel order, which is the
  // pin table's business and sits above.
  panel_cfg.data_width = 16;
  // Initialize the whole GPIO table to -1, then fill the 16 used pins; the
  // driver's array is sized SOC_LCD_RGB_DATA_WIDTH which may exceed 16.
  for (size_t i = 0; i < SOC_LCD_RGB_DATA_WIDTH; i++) {
    panel_cfg.data_gpio_nums[i] = -1;
  }
  memcpy(panel_cfg.data_gpio_nums, data_gpios, sizeof(data_gpios));

  panel_cfg.hsync_gpio_num = PIN_LCD_HSYNC;
  panel_cfg.vsync_gpio_num = PIN_LCD_VSYNC;
  panel_cfg.de_gpio_num = PIN_LCD_DE;
  panel_cfg.pclk_gpio_num = PIN_LCD_PCLK;
  panel_cfg.disp_gpio_num = -1;

  panel_cfg.flags.fb_in_psram = true;
  panel_cfg.psram_trans_align = 64;

  // The scan's line number, from these same timings - one line is this many
  // pixel clocks, and one frame is this many lines. See the block above
  // lcdFlushCb for why the flush needs to know where the scan is.
  {
    const uint32_t clocksPerLine =
        BOARD_LCD_HSYNC_PULSE_WIDTH + BOARD_LCD_HSYNC_BACK_PORCH +
        (uint32_t)LCD_H_RES + BOARD_LCD_HSYNC_FRONT_PORCH;
    s_lineUs = (uint32_t)(((uint64_t)clocksPerLine * 1000000ULL) /
                          BOARD_LCD_PCLK_HZ);
    s_frameLines = (uint32_t)LCD_V_RES + BOARD_LCD_VSYNC_PULSE_WIDTH +
                   BOARD_LCD_VSYNC_BACK_PORCH + BOARD_LCD_VSYNC_FRONT_PORCH;
  }
  panel_cfg.on_frame_trans_done = onScanFrameDone;

  // WHY THERE IS ONLY ONE FRAME BUFFER, AND WHAT THAT COSTS
  //
  // This esp_lcd revision has no bounce buffer and no second frame buffer: it can
  // only hand out the one buffer it scans from, so a region written while the
  // scan is somewhere else shows on screen in two halves. That is the tearing on
  // the 7-inch panel, and no setting in this file removes it.
  //
  // The newer esp_lcd has all three - bounce_buffer_size_px, num_fbs, and a
  // callback for filling a bounce buffer by hand - and its header is in the -libs
  // package on this machine. Using it changes the struct layout, and the struct
  // has to match the *linked* library: the linker takes its libesp_lcd from
  // tools/sdk, whose version has none of those fields, so a config built against
  // the newer header writes the pins where that driver reads buffer sizes, and the
  // panel goes black with "no mem for frame buffer".
  //
  // So the tearing stays until the libraries are the newer ones, which is a
  // platform version and not a line of configuration.

  if (esp_lcd_new_rgb_panel(&panel_cfg, &s_lcd_panel) != ESP_OK) {
    Serial.println("LCD: rgb panel create failed");
    return false;
  }
  if (esp_lcd_panel_init(s_lcd_panel) != ESP_OK) {
    Serial.println("LCD: rgb panel init failed");
    return false;
  }
  delay(20);

  // 4) LVGL display bound to the panel.
  //
  // ONE buffer, and in internal RAM if there is room. The driver owns the frame
  // buffer and copies every strip into it, so a full-screen redraw costs twice the
  // screen in PSRAM writes whatever LVGL does: once into its own buffer, once into
  // the frame buffer, and the cache write-back behind that. Putting LVGL's buffer
  // in SRAM takes one of the three off the PSRAM - the memory the scan is reading
  // 29 MB/s out of while the network stack works in it.
  //
  // One buffer and not two because a second one buys nothing here: the flush
  // callback is synchronous, so LVGL could not render into it while the first is
  // being copied anyway. Less RAM, and one copy instead of a pipeline.
  s_lv_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
  // The size of a pixel is the display's format's, not sizeof(lv_color_t): LVGL
  // 9's lv_color_t is three bytes whatever the format says (blue, green, red as
  // three uint8_t), and the buffer here is RGB565 because that is the format
  // lv_display_create() gives the display. Asking LVGL instead of assuming keeps
  // the buffer from being half again as large as it needs to be - which in SRAM
  // is 24 kB on this board that nobody would have found missing.
  const size_t pxBytes =
      (LV_COLOR_FORMAT_GET_BPP(lv_display_get_color_format(s_lv_disp)) + 7) / 8;
  const uint32_t buf_rows = BOARD_LCD_BUF_ROWS;
  const size_t bufBytes = (size_t)LCD_H_RES * buf_rows * pxBytes;
  lv_color_t *buf1 =
      (lv_color_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  bool imSram = buf1 != nullptr;
  if (!buf1) {
    buf1 = (lv_color_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
  }
  if (!buf1) {
    Serial.println("LCD: LVGL buffer allocation failed");
    return false;
  }

  lv_display_set_buffers(s_lv_disp, buf1, nullptr, bufBytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(s_lv_disp, lcdFlushCb);
  Serial.printf("LCD: LVGL-Puffer %u Zeilen, %u B (%u B je Pixel), %s\n",
                (unsigned)buf_rows, (unsigned)bufBytes, (unsigned)pxBytes,
                imSram ? "im SRAM" : "im PSRAM");

  Serial.println("LCD: display ready");
  delay(1500);
  Serial.printf("LCD: frames %lu, line %lu us\n", (unsigned long)s_scanFrames,
                (unsigned long)s_lineUs);
  // The memory the chip found decides the pixel clock the panel can be driven at:
  // Espressif's FAQ allows about 22 MHz for octal PSRAM at 80 MHz and about 11 MHz
  // for quad, and the board file asks for octal. A PSRAM that is found at all is
  // also the proof that it is octal, because a quad-mode driver cannot read an
  // octal chip.
  //
  // Its own line, and late, for two reasons that cost the same build: the USB
  // output drops the end of a long line - it cut this value off the end of a
  // 40-character line, where the 20-character one arrives whole - and the first
  // half second of the boot log is lost every time, because resetting the chip
  // re-enumerates the USB port and the earlier lines go with the old handle.
  Serial.printf("PSRAM %u MB\n",
                (unsigned)(ESP.getPsramSize() / (1024 * 1024)));
  return true;
}

// Marked on both sides on purpose. A hang *inside* lv_timer_handler() that does
// not reach the flush callback - LVGL's own draw code, between one callback and
// the next - would otherwise be reported under whatever phase the caller last
// named, which is what made the first two runs ambiguous: the same freeze was
// reported as "rct.poll" and as "lvgl" depending on which path reached it.
void displayLooper() {
  diagPhase("lv.handler");
  lv_timer_handler();
  diagPhase("lv.done");
  // The backlight is driven from here, and after the handler on purpose: the
  // press that counts as activity is the one lv_timer_handler() has just read,
  // so the light starts brightening in the same cycle. Being here also keeps it
  // running while something else holds the main task - the timers are
  // millis()-based and need no other pumping.
  backlightUpdate();
}

lv_display_t *dispGetHandle() { return s_lv_disp; }