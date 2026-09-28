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

#include "DisplayPins.h"

static uint8_t s_addr = 0;
static int16_t s_x = 0;
static int16_t s_y = 0;
static bool s_pressed = false;

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
  return Wire.endTransmission(false) == 0;
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
  uint8_t status = 0;
  if (gt911Read(GT911_REG_STATUS, &status, 1) && (status & 0x01)) {
    uint8_t pt[6] = {0}; // track id + x(lo,hi) + y(lo,hi) + size
    if (gt911Read(GT911_REG_POINT1, pt, sizeof(pt))) {
      s_x = pt[1] | (pt[2] << 8);
      s_y = pt[3] | (pt[4] << 8);
      if (s_x < 480 && s_y < 480) {
        s_pressed = true;
      }
    }
    gt911Write16(GT911_REG_STATUS, 0); // acknowledge
  } else {
    s_pressed = false;
  }

  if (s_x < 0) s_x = 0;
  if (s_y < 0) s_y = 0;
  data->point.x = s_x;
  data->point.y = s_y;
  data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}