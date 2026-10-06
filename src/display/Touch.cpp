// GT911 capacitive touch controller on I2C.
//
// The 4848S040 has no reset/interrupt pins wired to the GT911, so this driver
// polls: it probes the controller addresses (0x5D primary, 0x14 fallback),
// reads the "buffer ready" bit in the status register and pulls the first
// touch point. Writing 0 to the status register acknowledges the buffer.
//
// SPDX-License-Identifier: MIT
#include "Touch.h"

#include <Wire.h>
#include <lvgl.h>

#include "Backlight.h"
#include "board/BoardPins.h"

static uint8_t s_addr = 0;
static int16_t s_x = 0;
static int16_t s_y = 0;
static bool s_pressed = false;
static bool s_logPress = true;

#define GT911_REG_STATUS 0x814E
#define GT911_REG_POINT1 0x814F

static bool gt911Read(uint16_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  size_t got = Wire.requestFrom(s_addr, len);
  if (got != len) {
    while (Wire.available()) {
      Wire.read(); // drain leftovers so the next transaction starts clean
    }
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    buf[i] = Wire.read();
  }
  return true;
}

static bool gt911Write16(uint16_t reg, uint16_t value) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(value >> 8);
  Wire.write(value & 0xFF);
  // endTransmission(false) leaves the Wire core waiting for a requestFrom();
  // the next command would then log "Unfinished Repeated Start ..." and be
  // cleared. A write-only transaction must always end with a STOP.
  return Wire.endTransmission() == 0;
}

bool touchInit() {
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  Wire.setClock(100000);

  const uint8_t candidates[] = {TOUCH_I2C_ADDR, TOUCH_I2C_ADDR_ALT};
  for (uint8_t i = 0; i < sizeof(candidates); i++) {
    s_addr = candidates[i];
    uint8_t pid[4] = {0};
    if (gt911Read(0x8140, pid, 4) && pid[0] == '9' && pid[1] == '1' && pid[2] == '1') {
      Serial.printf("Touch: GT911 found at 0x%02X\n", s_addr);
      return true;
    }
  }
  Serial.println("Touch: GT911 not found (check I2C wiring)");
  return false;
}

void touchReadCb(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  if (!s_addr) { // controller not found / Wire not up: report released
    data->point.x = 0;
    data->point.y = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  uint8_t status = 0;
  if (gt911Read(GT911_REG_STATUS, &status, 1)) {
    // GT911 buffer-status register: bit7 = data ready, bits2:0 = touch count
    // (ESPhome gt911 driver uses 0x80/count; bit0 must NOT be used).
    uint8_t num = status & 0x07;
    gt911Write16(GT911_REG_STATUS, 0); // acknowledge/clear (before reading points)
    if ((status & 0x80) && num > 0) {
      uint8_t pt[6] = {0}; // track id + x(lo,hi) + y(lo,hi) + size
      if (gt911Read(GT911_REG_POINT1, pt, sizeof(pt))) {
        s_x = pt[1] | (pt[2] << 8);
        s_y = pt[3] | (pt[4] << 8);
        if (s_logPress) { // log press transitions only (no serial flood)
          Serial.printf("Touch: press x=%d y=%d id=%u size=%u status=0x%02X%s\n",
                        s_x, s_y, pt[0], pt[5], status,
                        (s_x < 480 && s_y < 480) ? " ok" : " OUT-OF-RANGE");
          s_logPress = false;
        }
        if (s_x < 480 && s_y < 480) {
          s_pressed = true;
          // A touch is the one thing that wakes the panel, and the only thing
          // that should: it arrives repeatedly while the finger stays down, so
          // a long press holds the light at full brightness. See Backlight.h.
          backlightActivity();
        }
      }
    } else {
      if (s_pressed) {
        Serial.printf("Touch: release (%d,%d)\n", s_x, s_y);
      }
      s_pressed = false;
      s_logPress = true;
    }
  }

  if (s_x < 0) s_x = 0;
  if (s_y < 0) s_y = 0;
  data->point.x = s_x;
  data->point.y = s_y;
  data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}