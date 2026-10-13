// GT911 capacitive touch controller on I2C.
//
// The 4848S040 has no reset/interrupt pins wired to the GT911, so this driver
// polls: it probes the controller addresses (0x5D primary, 0x14 fallback),
// reads the "buffer ready" bit in the status register and pulls the first
// touch point. Writing 0 to the status register acknowledges the buffer.
//
// SPDX-License-Identifier: MIT
#include "Touch.h"

#include "board/BoardPins.h"
#include "board/Ch422G.h"

#include <Wire.h>
#include <lvgl.h>

#include "Backlight.h"
#include "board/BoardPins.h"

static uint8_t s_addr = 0;
static int16_t s_x = 0;
static int16_t s_y = 0;
static bool s_pressed = false;
static bool s_logPress = true;

// The range the controller reports in. It is told this at configuration time and
// keeps it in its own registers (X_MAX / Y_MAX), so it is read rather than
// assumed. On the 7-inch board it reads 480 x 0 - a range nobody configured it
// with, Y zero - so a range is only used when it can be a range at all: both
// sides non-zero and within a factor of two of the panel. Without one the
// numbers are the panel's own, which is how the 480 px board has always worked.
//
// What the 480 that used to stand here cost: it was the Guition's resolution, so
// on the 7-inch board every touch right of x = 480 was thrown away as out of
// range - the right half of the screen, and with it the right navigation button.
static uint16_t s_rawMaxX = 0;
static uint16_t s_rawMaxY = 0;
static bool s_rangeBrauchbar = false;

#define GT911_REG_STATUS 0x814E
#define GT911_REG_POINT1 0x814F
#define GT911_REG_RANGE 0x8148

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

#if BOARD_CH422G_PRESENT
  // The GT911 on this board comes out of a reset: EXIO1 holds it in reset, and
  // GPIO 4 - the interrupt line - is driven low for the duration, because the
  // controller needs its INT low to accept a reset. The sequence and its timings
  // are the vendor's (TP_INT low, EXIO1 low 100 ms, EXIO1 high 200 ms, INT
  // released), taken from the TOUCH_PRE_BEGIN hook in their board file. Without
  // it the controller answers its product-ID register now and then, which is a
  // touch screen that is there and not there.
  pinMode(PIN_TOUCH_INT, OUTPUT);
  digitalWrite(PIN_TOUCH_INT, LOW);
  delay(10);
  ch422gWrite(BOARD_CH422G_PIN_TOUCH_RST, false);
  delay(100);
  ch422gWrite(BOARD_CH422G_PIN_TOUCH_RST, true);
  delay(200);
  pinMode(PIN_TOUCH_INT, INPUT); // released; the driver polls it never
#endif

  const uint8_t candidates[] = {TOUCH_I2C_ADDR, TOUCH_I2C_ADDR_ALT};
  for (uint8_t i = 0; i < sizeof(candidates); i++) {
    s_addr = candidates[i];
    uint8_t pid[4] = {0};
    if (gt911Read(0x8140, pid, 4) && pid[0] == '9' && pid[1] == '1' && pid[2] == '1') {
      Serial.printf("Touch: GT911 found at 0x%02X\n", s_addr);
      uint8_t bereich[4] = {0};
      if (gt911Read(GT911_REG_RANGE, bereich, 4)) {
        s_rawMaxX = (uint16_t)(bereich[0] | (bereich[1] << 8));
        s_rawMaxY = (uint16_t)(bereich[2] | (bereich[3] << 8));
        s_rangeBrauchbar = s_rawMaxX > 0 && s_rawMaxY > 0 &&
                           s_rawMaxX <= BOARD_SCREEN_W * 2 &&
                           s_rawMaxX >= BOARD_SCREEN_W / 2 &&
                           s_rawMaxY <= BOARD_SCREEN_H * 2 &&
                           s_rawMaxY >= BOARD_SCREEN_H / 2;
        Serial.printf("Touch: Bereich %u x %u, Bild %d x %d (%s)\n",
                      (unsigned)s_rawMaxX, (unsigned)s_rawMaxY,
                      BOARD_SCREEN_W, BOARD_SCREEN_H,
                      s_rangeBrauchbar ? "skaliert" : "unbrauchbar, roh");
      } else {
        Serial.println(F("Touch: Bereich unbekannt, rohe Koordinaten"));
      }
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
        const int rx = pt[1] | (pt[2] << 8);
        const int ry = pt[3] | (pt[4] << 8);
        // A finger that has just been lifted can report one more point past the
        // range; those are not touches.
        const int maxX = s_rangeBrauchbar ? s_rawMaxX : BOARD_SCREEN_W - 1;
        const int maxY = s_rangeBrauchbar ? s_rawMaxY : BOARD_SCREEN_H - 1;
        const bool imBereich = rx <= maxX + 16 && ry <= maxY + 16;
        int px = rx;
        int py = ry;
        if (s_rangeBrauchbar) {
          px = (int)((int32_t)rx * BOARD_SCREEN_W / s_rawMaxX);
          py = (int)((int32_t)ry * BOARD_SCREEN_H / s_rawMaxY);
        }
        if (px < 0) px = 0;
        if (py < 0) py = 0;
        if (px >= BOARD_SCREEN_W) px = BOARD_SCREEN_W - 1;
        if (py >= BOARD_SCREEN_H) py = BOARD_SCREEN_H - 1;
        s_x = (int16_t)px;
        s_y = (int16_t)py;
        if (s_logPress) { // log press transitions only (no serial flood)
          Serial.printf("Touch: press x=%d y=%d (roh %d/%d) id=%u size=%u%s\n",
                        s_x, s_y, rx, ry, pt[0], pt[5],
                        imBereich ? "" : " OUT-OF-RANGE");
          s_logPress = false;
        }
        if (imBereich) {
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

  data->point.x = s_x;
  data->point.y = s_y;
  data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}