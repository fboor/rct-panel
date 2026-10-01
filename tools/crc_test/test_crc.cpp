// Host test for the frame checksum (src/rct/RctCrc.h).
//
// A wrong checksum does not crash: the device and the simulator simply drop the
// frame, and the panel shows "no data" against a perfectly healthy inverter. The
// values below were therefore computed from the polynomial with a table-driven
// implementation, independent of the code under test, and two of them are
// cross-checks against a second implementation that we did not write: the READ
// frame for prim_sm.state (01 04 5f 33 28 4e -> 0xd9d3) is the frame the
// simulator logged as "2b01045f33284ed9d3" when the panel asked it for that
// value, and the same value is accepted by the real device.
//
// The one thing the host cannot decide is the padding rule for odd lengths: it
// is a property of the device, and it is why the three-byte extension frame
// works at all.

#include <stdio.h>
#include <string.h>

#include "rct/RctCrc.h"

static int g_checks = 0;
static int g_failed = 0;

static void checkEq(unsigned long got, unsigned long want, const char *what) {
  g_checks++;
  const bool ok = got == want;
  if (!ok) {
    g_failed++;
  }
  printf("%s %-52s 0x%04lX (erwartet 0x%04lX)\n", ok ? "OK  " : "FEHLER", what, got,
         want);
}

static void checkStr(const char *s, unsigned long want, const char *what) {
  checkEq(rctcrc::compute((const uint8_t *)s, strlen(s)), want, what);
}

static void checkBytes(const uint8_t *d, size_t n, unsigned long want,
                       const char *what) {
  checkEq(rctcrc::compute(d, n), want, what);
}

int main() {
  // Even length: the padding rule is not involved, so this is plain
  // CRC-16/CCITT-FALSE.
  checkStr("12345678", 0xA12B, "gerade Laenge: '12345678'");

  // Odd length: padded with one 0x00, which is what the device checks. Plain
  // CCITT-FALSE over the same nine bytes is 0x29B1 - the difference between the
  // two is the padding rule and nothing else.
  checkStr("123456789", 0x044B, "ungerade Laenge: '123456789' mit Polsterbyte");
  checkStr("A", 0x23F2, "ungerade Laenge: 'A' mit Polsterbyte");

  checkEq(rctcrc::compute(nullptr, 0), 0xFFFF, "leer: der Preset");

  // The frames the panel actually sends.
  static const uint8_t kRead[] = {0x01, 0x04, 0x5f, 0x33, 0x28, 0x4e};
  checkBytes(kRead, sizeof(kRead), 0xD9D3,
             "READ prim_sm.state - gegen den Simulator geprueft");
  static const uint8_t kExt[] = {0x2b, 0x3c, 0xe1};
  checkBytes(kExt, sizeof(kExt), 0x3ED6,
             "Verlaengerungsframe (3 Byte, gepolstert)");

  // One single bit difference must move the result: a checksum that ignores
  // bits would pass every vector above and still break the link.
  uint8_t kBit[] = {0x01, 0x04, 0x5f, 0x33, 0x28, 0x4e};
  const uint16_t a = rctcrc::compute(kRead, sizeof(kRead));
  kBit[5] ^= 0x01;
  checkEq(a != rctcrc::compute(kBit, sizeof(kBit)), 1,
          "ein gekipptes Bit aendert die Pruefsumme");

  // The padding is a trailing zero byte, and it is not optional: for every odd
  // length the result must equal the computation over n+1 bytes ending in
  // zero, and must differ from the same with a one in that place.
  uint8_t kurz[24], lang[24];
  int paddingOk = 1;
  for (size_t i = 0; i < sizeof(kurz); i++) {
    kurz[i] = (uint8_t)(i * 7 + 1);
  }
  for (size_t n = 1; n + 1 <= sizeof(kurz); n += 2) {
    memcpy(lang, kurz, n);
    lang[n] = 0x00;
    const uint16_t ohne = rctcrc::compute(kurz, n);
    if (ohne != rctcrc::compute(lang, n + 1)) {
      paddingOk = 0;
      break;
    }
    lang[n] = 0x01;
    if (ohne == rctcrc::compute(lang, n + 1)) {
      paddingOk = 0;
      break;
    }
  }
  checkEq((unsigned long)paddingOk, 1,
          "alle ungeraden Laengen: Polsterbyte ist eine Null am Ende");

  printf("%s: %d Prüfungen, %d fehlgeschlagen\n",
         g_failed == 0 ? "OK" : "FEHLER", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}