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

#include <Arduino.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <hal/lcd_types.h>

#include "DisplayPins.h"

#define LCD_H_RES 480
#define LCD_V_RES 480
#define LCD_PIXEL_CLOCK_HZ (16 * 1000 * 1000) // vendor demo clock range
#define LCD_INIT_SPI_CLK_HZ (2 * 1000 * 1000)

// ---------------------------------------------------------------------------
// ST7701 init sequence (command, payload). delay_ms runs after the command.
// ---------------------------------------------------------------------------
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
static const uint8_t d_cd[] = {0x00};
static const uint8_t d_b0_gamma[] = {0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07,
                                     0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA,
                                     0x31, 0x18};
static const uint8_t d_b1_gamma[] = {0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08,
                                     0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9,
                                     0x32, 0x18};
static const uint8_t d_ff_bk1[] = {0x77, 0x01, 0x00, 0x00, 0x11};
static const uint8_t d_b0_vop[] = {0x60};
static const uint8_t d_b1_vcom[] = {0x32};
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
static const uint8_t d_3a[] = {0x60}; // RGB666 (vendor demo value)

static const st7701_cmd_t st7701_init[] = {
    {0xFF, d_ff_bk0, sizeof(d_ff_bk0), 0}, {0xC0, d_c0, sizeof(d_c0), 0},
    {0xC1, d_c1, sizeof(d_c1), 0},         {0xC2, d_c2, sizeof(d_c2), 0},
    {0xCD, d_cd, sizeof(d_cd), 0},
    {0xB0, d_b0_gamma, sizeof(d_b0_gamma), 0},
    {0xB1, d_b1_gamma, sizeof(d_b1_gamma), 0},
    {0xFF, d_ff_bk1, sizeof(d_ff_bk1), 0}, {0xB0, d_b0_vop, sizeof(d_b0_vop), 0},
    {0xB1, d_b1_vcom, sizeof(d_b1_vcom), 0},
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
    {0x11, nullptr, 0, 120}, // Sleep Out
    {0x29, nullptr, 0, 120}, // Display On
};

// ---------------------------------------------------------------------------
// Driver state
// ---------------------------------------------------------------------------
static esp_lcd_panel_io_handle_t s_lcd_io = nullptr;
static esp_lcd_panel_handle_t s_lcd_panel = nullptr;
static lv_display_t *s_lv_disp = nullptr;

static void lcdWriteInitCmd(const st7701_cmd_t *c) {
  esp_lcd_panel_io_tx_param(s_lcd_io, c->cmd, c->data, c->len);
  if (c->delay_ms) {
    delay(c->delay_ms);
  }
}

static void lcdFlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  esp_lcd_panel_draw_bitmap(s_lcd_panel, area->x1, area->y1, area->x2 + 1,
                            area->y2 + 1, px_map);
  lv_display_flush_ready(disp);
}

bool displayInit() {
  // Backlight on (active high).
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  // 1) SPI link for ST7701 configuration (3-wire 9-bit mode, no D/C line).
  spi_bus_config_t buscfg = {};
  buscfg.sclk_io_num = PIN_LCD_SCK;
  buscfg.mosi_io_num = PIN_LCD_MOSI;
  buscfg.miso_io_num = -1;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = 64;
  if (spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
    Serial.println("LCD: SPI bus init failed");
    return false;
  }

  esp_lcd_panel_io_spi_config_t io_cfg = {};
  io_cfg.cs_gpio_num = PIN_LCD_CS;
  io_cfg.dc_gpio_num = -1; // 3-wire SPI (9-bit), command/data in 9th bit
  io_cfg.spi_mode = 0;
  io_cfg.pclk_hz = LCD_INIT_SPI_CLK_HZ;
  io_cfg.lcd_cmd_bits = 8;
  io_cfg.lcd_param_bits = 8;
  io_cfg.trans_queue_depth = 4;
  // This SDK's esp_lcd expects the SPI host id passed as the opaque bus
  // handle (the panel-io driver calls spi_bus_add_device() with it).
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg,
                               &s_lcd_io) != ESP_OK) {
    Serial.println("LCD: panel io init failed");
    return false;
  }

  // 2) ST7701 controller configuration.
  for (size_t i = 0; i < sizeof(st7701_init) / sizeof(st7701_init[0]); i++) {
    lcdWriteInitCmd(&st7701_init[i]);
  }

  // 3) Parallel RGB panel (LCD_CAM / esp_lcd).
  static const int data_gpios[] = {
      PIN_LCD_R0, PIN_LCD_R1, PIN_LCD_R2, PIN_LCD_R3, PIN_LCD_R4,
      PIN_LCD_G0, PIN_LCD_G1, PIN_LCD_G2, PIN_LCD_G3, PIN_LCD_G4, PIN_LCD_G5,
      PIN_LCD_B0, PIN_LCD_B1, PIN_LCD_B2, PIN_LCD_B3, PIN_LCD_B4,
  };

  esp_lcd_rgb_panel_config_t panel_cfg = {};
  panel_cfg.clk_src = LCD_CLK_SRC_PLL160M;
  panel_cfg.timings.pclk_hz = LCD_PIXEL_CLOCK_HZ;
  panel_cfg.timings.h_res = LCD_H_RES;
  panel_cfg.timings.v_res = LCD_V_RES;
  panel_cfg.timings.hsync_pulse_width = 8;
  panel_cfg.timings.hsync_back_porch = 50;
  panel_cfg.timings.hsync_front_porch = 10;
  panel_cfg.timings.vsync_pulse_width = 8;
  panel_cfg.timings.vsync_back_porch = 20;
  panel_cfg.timings.vsync_front_porch = 10;
  panel_cfg.timings.flags.hsync_idle_low = 1;
  panel_cfg.timings.flags.vsync_idle_low = 1;
  panel_cfg.timings.flags.de_idle_high = 0;
  panel_cfg.timings.flags.pclk_active_neg = 0; // clock data on rising edge
  panel_cfg.timings.flags.pclk_idle_high = 0;

  panel_cfg.data_width = 16; // RGB565 on 16 data lines
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
  static const uint32_t buf_rows = 80;
  lv_color_t *buf1 =
      (lv_color_t *)heap_caps_malloc(LCD_H_RES * buf_rows * sizeof(lv_color_t),
                                     MALLOC_CAP_SPIRAM);
  lv_color_t *buf2 =
      (lv_color_t *)heap_caps_malloc(LCD_H_RES * buf_rows * sizeof(lv_color_t),
                                     MALLOC_CAP_SPIRAM);
  if (!buf1 || !buf2) {
    Serial.println("LCD: LVGL buffer allocation failed");
    return false;
  }

  s_lv_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
  lv_display_set_buffers(s_lv_disp, buf1, buf2,
                         LCD_H_RES * buf_rows * sizeof(lv_color_t),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(s_lv_disp, lcdFlushCb);

  Serial.println("LCD: display ready");
  return true;
}

void displayLooper() { lv_timer_handler(); }

lv_display_t *dispGetHandle() { return s_lv_disp; }