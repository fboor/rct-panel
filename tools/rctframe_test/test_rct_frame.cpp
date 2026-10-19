// Host test for the receive state machine (src/rct/RctFrame.h).
//
// The failure this guards against is silent. A frame that ends at the wrong
// length is dropped by the device's own checksum, the panel then says "keine
// Daten" and everything looks like a broken inverter. Nothing crashes, no
// warning is printed, and the bug sits in a file that cannot be run on this
// machine - which is why it was written into a header.
//
// The frames below are built here rather than captured, because the thing that
// has to be right is the arithmetic: where the length field sits, how many
// bytes a frame occupies, and which span the checksum covers. A captured frame
// would carry the same arithmetic into the test and prove nothing.
//
// The one shape that no test can prove against a device is the plant frame
// (command 0x41..0x46), which this receiver drops like the official app drops
// it. That is a decision, and a decision is written down here so that it can be
// contradicted by something better than a guess.

#include <stdio.h>
#include <string.h>

#include "rct/RctCrc.h"
#include "rct/RctFrame.h"

static int g_checks = 0;
static int g_failed = 0;

static void checkEq(unsigned long got, unsigned long want, const char *what) {
  g_checks++;
  if (got != want) {
    g_failed++;
    printf("  FEHLER %-46s ist %lu, soll %lu\n", what, got, want);
  }
}

static void check(bool ok, const char *what) {
  g_checks++;
  if (!ok) {
    g_failed++;
    printf("  FEHLER %s\n", what);
  }
}

// ---------------------------------------------------------------- frame builder

#define WIRE_MAX 256

static size_t appendEscaped(uint8_t *dst, size_t at, uint8_t b) {
  if (b == 0x2b || b == 0x2d) {
    dst[at++] = 0x2d;
  }
  dst[at++] = b;
  return at;
}

// Build the wire form of a frame. longForm picks the two-byte length field
// (command 0x03/0x04/0x06) over the one-byte one. badCrc flips the last
// checksum byte, which is what a line error looks like.
static size_t buildFrame(uint8_t *wire, uint8_t cmd, uint32_t oid,
                         const uint8_t *payload, size_t payloadLen,
                         bool longForm, bool badCrc) {
  uint8_t log[64];
  size_t at = 0;
  const size_t len = 4 + payloadLen;
  log[at++] = 0x2b;
  log[at++] = cmd;
  if (longForm) {
    log[at++] = (uint8_t)(len >> 8);
  }
  log[at++] = (uint8_t)(len & 0xff);
  log[at++] = (uint8_t)(oid >> 24);
  log[at++] = (uint8_t)(oid >> 16);
  log[at++] = (uint8_t)(oid >> 8);
  log[at++] = (uint8_t)oid;
  memcpy(log + at, payload, payloadLen);
  at += payloadLen;
  const uint16_t crc = rctcrc::compute(log + 1, at - 1);
  log[at++] = (uint8_t)(crc >> 8);
  log[at++] = badCrc ? (uint8_t)(crc ^ 0x01) : (uint8_t)(crc & 0xff);

  size_t w = 0;
  // The start token is the delimiter and is sent as it is; everything behind it
  // is escaped.
  wire[w++] = log[0];
  for (size_t i = 1; i < at; i++) {
    w = appendEscaped(wire, w, log[i]);
  }
  return w;
}

// Feed a whole wire frame into a receiver and report the final status.
static rctframe::Status feedAll(rctframe::Receiver &rx, const uint8_t *wire,
                                size_t n) {
  rctframe::Status st = rctframe::kPartial;
  for (size_t i = 0; i < n; i++) {
    st = rx.feed(wire[i]);
  }
  return st;
}

// ---------------------------------------------------------------------- tests

// A small response, the shape the panel sees forty times a poll. Total bytes
// on the wire: 2b + cmd + len(1) + oid(4) + payload(4) + crc(2) = 13.
static void testKleineAntwort() {
  uint8_t buf[128];
  rctframe::Receiver rx(buf, sizeof(buf));
  uint8_t wire[WIRE_MAX];
  const uint8_t pl[4] = {0xde, 0xad, 0xbe, 0xef};
  const size_t n = buildFrame(wire, rctframe::kCmdResponse, 0x37F9D5CA, pl, 4,
                             false, false);

  checkEq(n, 13, "kleine Antwort: 13 Byte auf der Leitung");
  checkEq(feedAll(rx, wire, n), rctframe::kComplete, "kleine Antwort: fertig");
  check(rx.complete(), "kleine Antwort: complete()");
  checkEq(rx.command(), rctframe::kCmdResponse, "kleine Antwort: Befehl");
  checkEq(rx.oid(), 0x37F9D5CA, "kleine Antwort: Objekt-ID");
  checkEq(rx.payloadLen(), 4, "kleine Antwort: Nutzlastlaenge");
  checkEq(rx.frameLen(), 13, "kleine Antwort: Rahmenlaenge");
  check(rx.crcOk(), "kleine Antwort: Pruefsumme");
  checkEq(rx.payload()[0], 0xde, "kleine Antwort: erstes Nutzlastbyte");
  checkEq(rx.payload()[3], 0xef, "kleine Antwort: letztes Nutzlastbyte");
  rx.reset();
}

// The same, but with a payload byte that has to be escaped twice on the wire
// and comes back once in the buffer. 0x2b is the start token, so a receiver
// that forgets the escape loses the frame here.
static void testEscaping() {
  uint8_t buf[128];
  rctframe::Receiver rx(buf, sizeof(buf));
  uint8_t wire[WIRE_MAX];
  const uint8_t pl[3] = {0x2b, 0x2d, 0x41};
  const size_t n = buildFrame(wire, rctframe::kCmdResponse, 0x91617C58, pl, 3,
                             false, false);

  checkEq(n, 14, "Escaping: zwei Bytes mehr auf der Leitung");
  checkEq(feedAll(rx, wire, n), rctframe::kComplete, "Escaping: fertig");
  check(rx.crcOk(), "Escaping: Pruefsumme");
  checkEq(rx.payloadLen(), 3, "Escaping: Nutzlastlaenge");
  checkEq(rx.payload()[0], 0x2b, "Escaping: Starttoken in der Nutzlast");
  checkEq(rx.payload()[1], 0x2d, "Escaping: Escape in der Nutzlast");
  rx.reset();
}

// The shape the whole file exists for: a long response. The length field is two
// bytes here, so the length byte at index 2 is 0x00 and the real length is at
// index 3. Read as one byte the frame would end after nine bytes.
static void testLangeAntwort() {
  uint8_t buf[128];
  rctframe::Receiver rx(buf, sizeof(buf));
  uint8_t wire[WIRE_MAX];
  uint8_t pl[24];
  for (size_t i = 0; i < sizeof(pl); i++) {
    pl[i] = (uint8_t)(0xa0 + i);
  }
  const size_t n = buildFrame(wire, rctframe::kCmdLongResponse, 0x6F3876BC, pl,
                             sizeof(pl), true, false);

  checkEq(n, 34, "lange Antwort: 34 Byte auf der Leitung");
  checkEq(feedAll(rx, wire, n), rctframe::kComplete, "lange Antwort: fertig");
  checkEq(rx.command(), rctframe::kCmdLongResponse, "lange Antwort: Befehl");
  checkEq(rx.oid(), 0x6F3876BC, "lange Antwort: Objekt-ID");
  checkEq(rx.payloadLen(), 24, "lange Antwort: Nutzlastlaenge");
  checkEq(rx.frameLen(), 34, "lange Antwort: Rahmenlaenge");
  check(rx.crcOk(), "lange Antwort: Pruefsumme");
  // The payload starts behind the four length and id bytes, not three - the
  // offset difference between the two shapes.
  checkEq(rx.payload()[0], 0xa0, "lange Antwort: erstes Nutzlastbyte");
  checkEq(rx.payload()[23], 0xb7, "lange Antwort: letztes Nutzlastbyte");
  rx.reset();
}

// A long response that is not complete yet: after every byte but the last it
// must still say "coming".
static void testLangeAntwortUnvollstaendig() {
  uint8_t buf[128];
  rctframe::Receiver rx(buf, sizeof(buf));
  uint8_t wire[WIRE_MAX];
  uint8_t pl[24];
  memset(pl, 0x11, sizeof(pl));
  const size_t n = buildFrame(wire, rctframe::kCmdLongResponse, 0x6F3876BC, pl,
                             sizeof(pl), true, false);
  for (size_t i = 0; i + 1 < n; i++) {
    if (rx.feed(wire[i]) == rctframe::kComplete) {
      g_checks++;
      g_failed++;
      printf("  FEHLER lange Antwort unvollstaendig: %u Bytes zu frueh fertig\n",
             (unsigned)i);
      return;
    }
  }
  g_checks++;
  checkEq(rx.feed(wire[n - 1]), rctframe::kComplete,
          "lange Antwort: erst am letzten Byte fertig");
  check(rx.crcOk(), "lange Antwort unvollstaendig: Pruefsumme");
  rx.reset();
}

// One long frame does not fit the buffer. It has to be dropped rather than
// written over, and the frame after it has to arrive anyway.
static void testZuLang() {
  uint8_t buf[16]; // far too small for a 24-byte payload
  rctframe::Receiver rx(buf, sizeof(buf));
  uint8_t wire[WIRE_MAX];
  uint8_t pl[24];
  memset(pl, 0x22, sizeof(pl));
  const size_t n = buildFrame(wire, rctframe::kCmdLongResponse, 0x6F3876BC, pl,
                             sizeof(pl), true, false);

  checkEq(feedAll(rx, wire, n), rctframe::kTooLong, "zu lang: wird verworfen");
  check(!rx.complete(), "zu lang: nichts vollstaendig");
  checkEq(rx.payloadLen(), 0, "zu lang: keine Nutzlast");
  // Was verworfen wurde, steht noch da - sonst sagt die Meldung nur "passte
  // nicht", und die Groesse ist das einzige, was man dann wissen will.
  checkEq(rx.droppedCommand(), 0x06, "zu lang: Befehl bleibt lesbar");
  checkEq(rx.droppedLength(), 28, "zu lang: Laenge bleibt lesbar");

  // The bytes that were still coming belong to the dropped frame; the next
  // start token has to start a new one.
  const uint8_t pl2[4] = {1, 2, 3, 4};
  const size_t n2 = buildFrame(wire, rctframe::kCmdResponse, 0x37F9D5CA, pl2, 4,
                              false, false);
  checkEq(feedAll(rx, wire, n2), rctframe::kComplete,
          "zu lang: die naechste Antwort kommt trotzdem an");
  checkEq(rx.oid(), 0x37F9D5CA, "zu lang: Objekt-ID der naechsten Antwort");
  check(rx.crcOk(), "zu lang: Pruefsumme der naechsten Antwort");
  rx.reset();
}

// A frame for another component on the bus: command bit 6 set, and a four-byte
// address between the length and the object id. Dropped, like the app drops it.
static void testBusFremder() {
  uint8_t buf[128];
  rctframe::Receiver rx(buf, sizeof(buf));
  uint8_t wire[WIRE_MAX];
  const uint8_t pl[4] = {0, 0, 0, 0};
  const size_t n = buildFrame(wire, 0x46, 0x6F3876BC, pl, 4, true, false);
  checkEq(feedAll(rx, wire, n), rctframe::kIgnored,
          "Buss fremd: wird ignoriert");
  check(!rx.complete(), "Buss fremd: nichts vollstaendig");
  rx.reset();
}

// Damage in the frame: the checksum is the only thing that catches it, and the
// receiver has to report it rather than hand out a payload.
static void testPruefsummeFalsch() {
  uint8_t buf[128];
  rctframe::Receiver rx(buf, sizeof(buf));
  uint8_t wire[WIRE_MAX];
  const uint8_t pl[4] = {0xde, 0xad, 0xbe, 0xef};
  const size_t n = buildFrame(wire, rctframe::kCmdResponse, 0x37F9D5CA, pl, 4,
                             false, true);
  checkEq(feedAll(rx, wire, n), rctframe::kComplete,
          "Pruefsumme falsch: Rahmen ist trotzdem vollstaendig");
  check(!rx.crcOk(), "Pruefsumme falsch: wird erkannt");
  rx.reset();
}

// Bytes between the frames are another client's chatter on the shared bus and
// must be ignored without disturbing what follows.
static void testMuellVorDerAntwort() {
  uint8_t buf[128];
  rctframe::Receiver rx(buf, sizeof(buf));
  uint8_t wire[WIRE_MAX];
  const uint8_t pl[4] = {0x0a, 0x0b, 0x0c, 0x0d};
  const size_t n = buildFrame(wire, rctframe::kCmdResponse, 0x37F9D5CA, pl, 4,
                             false, false);
  // What a line can carry between two frames: bytes that start nothing. Note
  // that none of them is 0x2b - that would be a frame, and the next test is
  // about exactly that one.
  const uint8_t muell[6] = {0x13, 0x17, 0x2d, 0x2d, 0x42, 0x00};

  rctframe::Status st = rctframe::kPartial;
  for (size_t i = 0; i < sizeof(muell); i++) {
    st = rx.feed(muell[i]);
  }
  check(st != rctframe::kComplete, "Muell: liefert keinen Rahmen");
  checkEq(feedAll(rx, wire, n), rctframe::kComplete,
          "Muell: die Antwort danach kommt an");
  check(rx.crcOk(), "Muell: Pruefsumme stimmt");
  checkEq(rx.oid(), 0x37F9D5CA, "Muell: Objekt-ID stimmt");
  rx.reset();
}

// The receiver lands inside a frame of another client: a start token, a command
// byte, and then the connection ends or the client stops sending. The next
// frame's start token is the way out - without that rule the receiver would keep
// reading foreign bytes as a length and swallow every frame behind it.
static void testNachFremdemFrame() {
  uint8_t buf[128];
  rctframe::Receiver rx(buf, sizeof(buf));
  uint8_t wire[WIRE_MAX];
  const uint8_t pl[4] = {0x0a, 0x0b, 0x0c, 0x0d};
  const size_t n = buildFrame(wire, rctframe::kCmdResponse, 0x37F9D5CA, pl, 4,
                             false, false);

  // Half a foreign frame: start token, a long response command, a length that
  // says 200 bytes - and then the next real frame instead of those 200 bytes.
  const uint8_t fremd[3] = {0x2b, 0x06, 0x00};
  for (size_t i = 0; i < sizeof(fremd); i++) {
    rx.feed(fremd[i]);
  }
  checkEq(feedAll(rx, wire, n), rctframe::kComplete,
          "fremdes Frame: die naechste Antwort kommt an");
  check(rx.crcOk(), "fremdes Frame: Pruefsumme stimmt");
  checkEq(rx.oid(), 0x37F9D5CA, "fremdes Frame: Objekt-ID stimmt");
  checkEq(rx.payloadLen(), 4, "fremdes Frame: Nutzlastlaenge stimmt");
  rx.reset();
}

// A length field that is smaller than an object id is not a frame. Reading an
// object id out of it would read past the end of the buffer.
static void testLaengeZuKlein() {
  uint8_t buf[128];
  rctframe::Receiver rx(buf, sizeof(buf));
  uint8_t wire[WIRE_MAX];
  const size_t n = buildFrame(wire, rctframe::kCmdResponse, 0x37F9D5CA,
                             (const uint8_t *)"", 0, false, false);
  // Hand-built: length 3, which cannot hold an object id.
  uint8_t log[16];
  size_t at = 0;
  log[at++] = 0x2b;
  log[at++] = rctframe::kCmdResponse;
  log[at++] = 3;
  log[at++] = 0x11;
  log[at++] = 0x22;
  log[at++] = 0x33;
  const uint16_t crc = rctcrc::compute(log + 1, at - 1);
  log[at++] = (uint8_t)(crc >> 8);
  log[at++] = (uint8_t)(crc & 0xff);
  size_t w = 0;
  for (size_t i = 0; i < at; i++) {
    w = appendEscaped(wire, w, log[i]);
  }
  (void)n;
  checkEq(feedAll(rx, wire, w), rctframe::kTooLong,
          "Laenge zu klein: wird verworfen");
  rx.reset();
}

// Which commands carry the two-byte length. Getting this wrong is the whole
// bug, so it is stated once and checked.
static void testWelcheBefehleSindLang() {
  check(rctframe::Receiver::isLong(rctframe::kCmdLongWrite),
            "0x03 ist lang");
  check(rctframe::Receiver::isLong(rctframe::kCmdReadPeriodically),
            "0x04 ist lang");
  check(rctframe::Receiver::isLong(rctframe::kCmdLongResponse),
            "0x06 ist lang");
  check(!rctframe::Receiver::isLong(rctframe::kCmdRead), "0x01 ist kurz");
  check(!rctframe::Receiver::isLong(rctframe::kCmdWrite), "0x02 ist kurz");
  check(!rctframe::Receiver::isLong(rctframe::kCmdResponse),
            "0x05 ist kurz");
}

int main() {
  testKleineAntwort();
  testEscaping();
  testLangeAntwort();
  testLangeAntwortUnvollstaendig();
  testZuLang();
  testBusFremder();
  testPruefsummeFalsch();
  testMuellVorDerAntwort();
  testNachFremdemFrame();
  testLaengeZuKlein();
  testWelcheBefehleSindLang();

  printf("%s: %d Prüfungen, %d fehlgeschlagen\n",
         g_failed ? "FEHLER" : "OK", g_checks, g_failed);
  return g_failed ? 1 : 0;
}