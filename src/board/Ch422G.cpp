// SPDX-License-Identifier: MIT
//
// The implementation behind src/board/Ch422G.h. Compiled on every board and
// empty on the ones without an expander, so that a call site is the same line of
// code either way and the board profile stays the only place that decides.
#include "Ch422G.h"

#if BOARD_CH422G_PRESENT

#include <Wire.h>

// The register map, from the vendor's esp_io_expander_ch422g.c. The values in
// that file are written as (0x48 >> 1) and the like; the shifts are already done
// here, because at the Wire level these are the addresses that go on the bus.
#define CH422G_REG_WR_OC 0x23  // EXIO8..11
#define CH422G_REG_WR_SET 0x24 // bit0 IO_OE, bit2 OD_EN
#define CH422G_REG_RD_IO 0x26  // the pins, read back
#define CH422G_REG_WR_IO 0x38  // EXIO0..7

// WR_SET bit 0 set means the pins are OUTPUTS. The vendor's driver decides it the
// other way round in code: write_direction_reg() sets IO_OE for any non-zero
// direction mask, and read_direction_reg() answers DIR_OUT_VALUE (all twelve) when
// IO_OE is set and DIR_IN_VALUE (only EXIO8..11) when it is clear. So with 0x00
// written here the chip had EXIO0..7 as inputs - and there sit the LCD reset,
// the USB/CAN selector, the card's chip select and the touch reset. None of them
// was ever driven, which is what the panel showed: a picture that needs a power
// cycle to appear, and no card.
#define CH422G_ALL_OUTPUTS 0x01

#define CH422G_MAX_PIN 11

static bool s_found = false;
static uint8_t s_oc = 0x0F;
static uint8_t s_io = 0xFF;

// All twelve as push-pull outputs, then the pin levels the board needs. Declared
// before ch422gBegin(), which writes them.
static bool konfigurieren();

// The Wire error of the last failed write, for the log. 2 is "no acknowledge from
// the address", 3 is "no acknowledge from the data", 1 is anything else - the
// numbers are the Arduino ones, and they are the only thing that says whether the
// chip is not there or is not listening.
static int s_wireFehler = 0;

// Which devices are on the bus, asked by reading: a transmission without a data
// byte returns ESP_OK in this core without ever touching the bus, so it "finds"
// every address there is. Reading also leaves the devices' registers alone - the
// touch controller shares this bus, and writing a zero into its control register
// is not a probe.
//
// Only called when the expander did not answer: what else is on the bus tells a
// wrong address from a chip that is not there at all.
static void busFragen() {
  uint8_t gefunden = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission(false) != 0) {
      continue;
    }
    if (Wire.requestFrom((int)a, (int)1) > 0) {
      gefunden++;
      Serial.printf("CH422G: Busgeraet 0x%02X\n", a);
    }
  }
  if (gefunden == 0) {
    Serial.println(F("CH422G: niemand antwortet auf dem Bus"));
  }
}

static bool schreibe(uint8_t reg, uint8_t wert) {
  s_wireFehler = 0;
  Wire.beginTransmission(BOARD_CH422G_I2C_ADDR);
  Wire.write(reg);
  Wire.write(wert);
  const int rc = Wire.endTransmission(true);
  if (rc != 0) {
    s_wireFehler = rc;
    return false;
  }
  return true;
}

bool ch422gBegin() {
  s_found = false;
  // The touch is on this bus and calls Wire.begin() itself later; doing it here
  // first is the same call with the same two pins, and it has to be before the
  // display's reset pulse - which is what displayInit() asks for first.
  //
  // Two clocks, because the first one that fails is not always the right answer:
  // the vendor's config runs this bus at 400 kHz, and if the chip does not
  // acknowledge there, the slower clock the Arduino default uses is tried before
  // the expander is given up on. What is not tried is a chip that stays silent:
  // four of the board's functions hang on it, and the panel needs its reset.
  // No address probe before this: a transmission without a data byte returns
  // ESP_OK in this core without ever touching the bus, so a probe that way says
  // "answering" for every address there is. Writing the first register is the
  // honest test, and it is what has to succeed anyway.
  static const uint32_t takte[] = {BOARD_CH422G_I2C_HZ, 100000};
  for (size_t t = 0; t < sizeof(takte) / sizeof(takte[0]); t++) {
    Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, takte[t]);
    if (konfigurieren()) {
      s_found = true;
      return true;
    }
    Serial.printf("CH422G: 0x%02X nicht schreibbar bei %lu Hz\n",
                  BOARD_CH422G_I2C_ADDR, (unsigned long)takte[t]);
  }
  busFragen();
  return false;
}

static bool konfigurieren() {
  // All twelve as push-pull outputs: IO_OE set, open-drain enable off. The pin
  // values are written afterwards, so a chip that starts in its default (all
  // high) is put into the same state here as one that was already configured.
  //
  // The values are the board's, not the chip's: BOARD_CH422G_START_IO says what
  // each pin this firmware uses has to be after the bring-up. The chip's own
  // default (all high) is wrong for two of them - the TF card's chip select is
  // active low, and USB_SEL high puts the board in CAN mode - so a blanket 0xFF
  // here would be a silent fault rather than a default.
  s_oc = 0x0F;
  s_io = BOARD_CH422G_START_IO;
  struct {
    uint8_t reg;
    uint8_t wert;
    const char *name;
  } schritte[] = {
      {CH422G_REG_WR_SET, CH422G_ALL_OUTPUTS, "WR_SET"},
      {CH422G_REG_WR_OC, s_oc, "WR_OC"},
      {CH422G_REG_WR_IO, s_io, "WR_IO"},
  };
  for (size_t i = 0; i < sizeof(schritte) / sizeof(schritte[0]); i++) {
    if (!schreibe(schritte[i].reg, schritte[i].wert)) {
      Serial.printf("CH422G: %s (0x%02X = 0x%02X) nicht schreibbar, Wire %d\n",
                    schritte[i].name, schritte[i].reg, schritte[i].wert,
                    s_wireFehler);
      return false;
    }
  }
  Serial.printf("CH422G: %d Ausgaenge auf GPIO %d/%d\n", CH422G_MAX_PIN + 1,
                PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  return true;
}

bool ch422gWrite(uint8_t pin, bool level) {
  if (!s_found || pin > CH422G_MAX_PIN) {
    return false;
  }
  if (pin < 8) {
    const uint8_t bit = (uint8_t)(1u << pin);
    if (((s_io & bit) != 0) == level) {
      return true;
    }
    s_io = level ? (uint8_t)(s_io | bit) : (uint8_t)(s_io & ~bit);
    return schreibe(CH422G_REG_WR_IO, s_io);
  }
  const uint8_t bit = (uint8_t)(1u << (pin - 8));
  if (((s_oc & bit) != 0) == level) {
    return true;
  }
  s_oc = level ? (uint8_t)(s_oc | bit) : (uint8_t)(s_oc & ~bit);
  return schreibe(CH422G_REG_WR_OC, s_oc);
}

bool ch422gPulse(uint8_t pin, bool activeLow, uint32_t lowMs, uint32_t highMs) {
  if (!ch422gWrite(pin, activeLow)) {
    return false;
  }
  delay(lowMs);
  if (!ch422gWrite(pin, !activeLow)) {
    return false;
  }
  delay(highMs);
  return true;
}

bool ch422gFound() { return s_found; }

#endif // BOARD_CH422G_PRESENT