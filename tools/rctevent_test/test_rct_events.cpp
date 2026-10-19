// Host test for the event log decoder (src/rct/RctEvents.h).
//
// The payload format is not a guess: it was read out of the official app
// (org.rctpower.heiphossil, version 2.500, decompiled - MainActivity fills the
// error list out of a long response) and the rctclient documentation describes
// the same frame. What the test adds is what the format makes easy to get wrong
// in a way the device will not complain about:
//
//   - an entry ends the table when its start timestamp is 0, so a payload whose
//     FIRST entry is 0 is empty and not an error, and a trailing partial entry
//     is not an entry;
//   - the same window arrives again every time a client asks for it, so the
//     same entries must not pile up - a list that grows while nothing happened
//     is a bug that looks like data;
//   - the values are raw 32-bit words and a float bit pattern or a negative
//     duration has to survive them unchanged.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device/EventTime.h"
#include "rct/RctEvents.h"

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

static void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

#define PL_MAX 1024

// A payload: the query timestamp, then entries. Add a terminator with
// terminate = true, which is what the device appends.
static size_t buildPayload(uint8_t *pl, uint32_t query,
                           const rctevents::Entry *e, size_t n,
                           bool terminate, size_t truncate = 0) {
  size_t at = 0;
  put32(pl + at, query);
  at += 4;
  for (size_t i = 0; i < n; i++) {
    put32(pl + at, e[i].type);
    put32(pl + at + 4, e[i].startUnix);
    put32(pl + at + 8, e[i].endUnix);
    put32(pl + at + 12, e[i].value1);
    put32(pl + at + 16, e[i].value2);
    at += 20;
  }
  if (terminate) {
    put32(pl + at, 0);
    put32(pl + at + 4, 0);
    put32(pl + at + 8, 0);
    put32(pl + at + 12, 0);
    put32(pl + at + 16, 0);
    at += 20;
  }
  return truncate ? (at > truncate ? truncate : at) : at;
}

// ---------------------------------------------------------------------- tests

// One page, three entries: they come out newest first, with every word intact.
static void testEinBlatt() {
  rctevents::Table t;
  uint8_t pl[PL_MAX];
  rctevents::Entry e[3];
  e[0].type = 0x76; // RESET / "System start"
  e[0].startUnix = 1789012345;
  e[0].endUnix = 1789012367;
  e[0].value1 = 1;
  e[0].value2 = 0;
  e[1].type = 0x73; // PRM_CHANGE / "Parameter changed"
  e[1].startUnix = 1789020000;
  e[1].endUnix = 0x9AB0C1D2;
  e[1].value1 = 0;
  e[1].value2 = 1;
  e[2].type = 0x64; // BAT_OVERVOLTAGE
  e[2].startUnix = 1789025000;
  e[2].endUnix = 1789025022;
  e[2].value1 = 0x41A00000; // 20.0 V as a float bit pattern
  e[2].value2 = 0;

  const size_t len = buildPayload(pl, 1789030000, e, 3, true);
  checkEq(t.decode(pl, len), 3, "ein Blatt: drei Eintraege");
  checkEq(t.size(), 3, "ein Blatt: Groesse");
  checkEq(t.queryUnix(), 1789030000, "ein Blatt: Abfragezeitstempel");
  checkEq(t.pages(), 1, "ein Blatt: eine Seite");
  checkEq(t.newestUnix(), 1789025000, "ein Blatt: neuester Zeitstempel");
  checkEq(t.oldestUnix(), 1789012345, "ein Blatt: aeltester Zeitstempel");

  checkEq(t.at(0).type, 0x64, "ein Blatt: neuester Typ");
  checkEq(t.at(0).startUnix, 1789025000, "ein Blatt: neuester Start");
  checkEq(t.at(0).endUnix, 1789025022, "ein Blatt: neuestes Ende");
  checkEq(t.at(0).value1, 0x41A00000, "ein Blatt: Wert bleibt Bitmuster");
  checkEq(t.at(1).type, 0x73, "ein Blatt: zweiter Typ");
  checkEq(t.at(1).endUnix, 0x9AB0C1D2, "ein Blatt: Objekt-ID bleibt unveraendert");
  checkEq(t.at(2).type, 0x76, "ein Blatt: dritter Typ");
}

// The same question asked twice - which is what happens every time a user
// opens the log again - has to leave the list alone.
static void testDieselbeSeiteZweimal() {
  rctevents::Table t;
  uint8_t pl[PL_MAX];
  rctevents::Entry e[2];
  e[0].type = 0x76;
  e[0].startUnix = 1000;
  e[0].endUnix = 1060;
  e[0].value1 = 1;
  e[0].value2 = 0;
  e[1].type = 0x64;
  e[1].startUnix = 2000;
  e[1].endUnix = 2022;
  e[1].value1 = 0;
  e[1].value2 = 0;

  const size_t len = buildPayload(pl, 9999, e, 2, true);
  checkEq(t.decode(pl, len), 2, "zweimal: erster Durchgang");
  checkEq(t.decode(pl, len), 0, "zweimal: zweiter Durchgang bringt nichts");
  checkEq(t.size(), 2, "zweimal: Groesse bleibt");
  checkEq(t.pages(), 2, "zweimal: zwei Seiten gesehen");
}

// A second page of older entries: they are added, and the order stays by
// arrival, which is newest first - the device answers a window the client asked
// for, so every page carries entries older than the one before.
static void testZweiteSeite() {
  rctevents::Table t;
  uint8_t pl[PL_MAX];
  rctevents::Entry neu[1];
  neu[0].type = 0x76;
  neu[0].startUnix = 5000;
  neu[0].endUnix = 5060;
  neu[0].value1 = 0;
  neu[0].value2 = 0;
  rctevents::Entry alt[1];
  alt[0].type = 0x63; // PHASE_POS - the entry the documentation puts at 0x61
  alt[0].startUnix = 100;
  alt[0].endUnix = 160;
  alt[0].value1 = 3;
  alt[0].value2 = 0;

  size_t len = buildPayload(pl, 5000, neu, 1, true);
  checkEq(t.decode(pl, len), 1, "zweite Seite: erster Durchgang");
  len = buildPayload(pl, 100, alt, 1, true);
  checkEq(t.decode(pl, len), 1, "zweite Seite: alterer Eintrag kommt dazu");
  checkEq(t.size(), 2, "zweite Seite: Groesse");
  checkEq(t.at(0).type, 0x76, "zweite Seite: der neuere Eintrag steht vorn");
  checkEq(t.at(1).type, 0x63, "zweite Seite: der aeltere dahinter");
  checkEq(t.queryUnix(), 100, "zweite Seite: Abfragezeitstempel ist der neue");
}

// Nothing older than the timestamp: a table with a zero start right away. That
// is an empty answer, not a damaged one.
static void testLeereTabelle() {
  rctevents::Table t;
  uint8_t pl[PL_MAX];
  rctevents::Entry e[1];
  e[0].type = 0x76;
  e[0].startUnix = 0; // end of table
  e[0].endUnix = 0;
  e[0].value1 = 0;
  e[0].value2 = 0;
  const size_t len = buildPayload(pl, 12345, e, 1, true);
  checkEq(t.decode(pl, len), 0, "leere Tabelle: nichts aufgenommen");
  checkEq(t.size(), 0, "leere Tabelle: bleibt leer");
  checkEq(t.queryUnix(), 12345, "leere Tabelle: Abfrage trotzdem gelesen");
  checkEq(t.pages(), 1, "leere Tabelle: die Seite wurde gesehen");
}

// A payload that stops in the middle of an entry. Half an entry is not an
// entry - the five words are the unit, and the last one before the break is
// complete enough to keep.
static void testAbgeschnitteneNutzlast() {
  rctevents::Table t;
  uint8_t pl[PL_MAX];
  rctevents::Entry e[3];
  for (int i = 0; i < 3; i++) {
    e[i].type = 0x76;
    e[i].startUnix = (uint32_t)(1000 + i);
    e[i].endUnix = 0;
    e[i].value1 = 0;
    e[i].value2 = 0;
  }
  const size_t voll = buildPayload(pl, 7777, e, 3, true);
  const size_t halb = buildPayload(pl, 7777, e, 3, true, voll - 28);
  checkEq(t.decode(pl, halb), 2, "abgeschnitten: zwei ganze Eintraege");
  checkEq(t.size(), 2, "abgeschnitten: Groesse");
  checkEq(t.at(0).startUnix, 1001, "abgeschnitten: der dritte ist nicht da");
}

// Shorter than the echoed query timestamp: not a table at all.
static void testZuKurz() {
  rctevents::Table t;
  uint8_t pl[8] = {0x2b, 0x2b, 0x2b, 0};
  checkEq(t.decode(pl, 3), 0, "zu kurz: nichts aufgenommen");
  checkEq(t.decode(pl, 0), 0, "leer: nichts aufgenommen");
  checkEq(t.size(), 0, "zu kurz: bleibt leer");
}

// More entries than the panel keeps: the oldest go, the newest stay, and the
// order is still newest first.
static void testUeberlauf() {
  rctevents::Table t;
  uint8_t pl[PL_MAX];
  rctevents::Entry e[50];
  for (int i = 0; i < 50; i++) {
    e[i].type = 0x76;
    e[i].startUnix = (uint32_t)(10000 + i);
    e[i].endUnix = 0;
    e[i].value1 = 0;
    e[i].value2 = 0;
  }
  const size_t len = buildPayload(pl, 99999, e, 50, true);
  checkEq(t.decode(pl, len), 50, "Ueberlauf: alle 50 sind neu");
  checkEq(t.size(), 40, "Ueberlauf: Groesse");
  checkEq(t.newestUnix(), 10049, "Ueberlauf: neuester bleibt");
  checkEq(t.oldestUnix(), 10010, "Ueberlauf: die zehn aeltesten sind weg");
  // The order must not have turned over where the ring wrapped.
  bool absteigend = true;
  for (size_t i = 1; i < t.size(); i++) {
    if (t.at(i).startUnix >= t.at(i - 1).startUnix) {
      absteigend = false;
    }
  }
  check(absteigend, "Ueberlauf: Reihenfolge bleibt neueste zuerst");
}

// A type outside 0..127: the fault texts stop there, but the entry is real and
// the table does not get to decide that it is not worth keeping.
static void testUnbekannterTyp() {
  rctevents::Table t;
  uint8_t pl[PL_MAX];
  rctevents::Entry e[2];
  e[0].type = 200;
  e[0].startUnix = 100;
  e[0].endUnix = 0;
  e[0].value1 = 0;
  e[0].value2 = 0;
  e[1].type = 0x7f;
  e[1].startUnix = 200;
  e[1].endUnix = 0;
  e[1].value1 = 0;
  e[1].value2 = 0;
  const size_t len = buildPayload(pl, 999, e, 2, true);
  checkEq(t.decode(pl, len), 2, "unbekannter Typ: trotzdem aufgenommen");
  // Sortiert wird nach Zeit, nicht nach Typ: der zweite Eintrag ist der neuere.
  checkEq(t.at(0).type, 0x7f, "unbekannter Typ: der neuere steht vorn");
  checkEq(t.at(1).type, 200, "unbekannter Typ: 200 bleibt 200");
}

// Two entries with the same type and start but different values are two
// events, not one - the device logs the same kind of thing twice in a minute.
static void testGleicheArtZweimal() {
  rctevents::Table t;
  uint8_t pl[PL_MAX];
  rctevents::Entry e[2];
  e[0].type = 0x64;
  e[0].startUnix = 500;
  e[0].endUnix = 522;
  e[0].value1 = 0x41A00000;
  e[0].value2 = 0;
  e[1] = e[0];
  e[1].value1 = 0x41B00000; // same second, a different voltage
  const size_t len = buildPayload(pl, 999, e, 2, true);
  checkEq(t.decode(pl, len), 2, "gleiche Art: zwei Eintraege");
  checkEq(t.size(), 2, "gleiche Art: Groesse");
  // Gleiche Sekunde, gleiche Art: da gibt es keine Reihenfolge zu messen. Die
  // Liste haelt die, die das Geraet zuerst geschickt hat - und das ist die
  // einzige Aussage, die hier etwas waere.
  checkEq(t.at(0).value1, 0x41A00000, "gleiche Art: die zuerst gesendete vorn");
  checkEq(t.at(1).value1, 0x41B00000, "gleiche Art: die zweite dahinter");
}

// --- die Zeitregeln des Eintrags (src/device/EventTime.h) --------------------
//
// Sie stehen in einem eigenen Kopf, weil zwei Stellen sie brauchen - der
// Serienlog und die Seite /ereignisse - und sie sind die einzige Stelle, an der
// aus einer Zahl des Eintrags etwas wird, das plausibel aussieht und falsch
// ist: aus einem Objektbezeichner wird "00:12:34", aus einer Uhr, die noch
// nicht gestellt war, wird "1970-01-01".
static void testDauer() {
  uint32_t d = 12345;
  check(eventtime::durationSeconds(1700000000u, 1700000060u, d),
        "Dauer: 60 s erkannt");
  checkEq(d, 60, "Dauer: 60 s");
  // Gleiche Zeit ist eine Dauer von null Sekunden und keine fehlende: das
  // Geraet schreibt ein einmaliges Ereignis mit Ende 0, nicht mit Ende gleich
  // Anfang.
  check(eventtime::durationSeconds(1700000000u, 1700000000u, d),
        "Dauer: gleiche Zeit ist 0 s");
  checkEq(d, 0, "Dauer: gleiche Zeit ist 0 s lang");
  check(!eventtime::durationSeconds(1700000000u, 1699999999u, d),
        "Dauer: rueckwaerts ist keine");
  check(!eventtime::durationSeconds(1700000000u, 0u, d),
        "Dauer: Ende 0 ist keine");
  check(eventtime::durationSeconds(1700000000u, 1700086399u, d),
        "Dauer: 23:59:59 geht noch");
  checkEq(d, 86399, "Dauer: 23:59:59");
  check(!eventtime::durationSeconds(1700000000u, 1700086400u, d),
        "Dauer: ein ganzer Tag ist keine");
  // Ein Objektbezeichner, wie ihn die Parameteraenderung an dieser Stelle
  // traegt: gross und positiv, die Differenz ist deshalb negativ.
  check(!eventtime::durationSeconds(1700000000u, 939222474u, d),
        "Dauer: Objektbezeichner ist keine");
  // Und die ersten Sekunden nach einem Neustart, bevor die Uhr steht.
  check(!eventtime::durationSeconds(3600u, 3660u, d),
        "Dauer: ohne gestellte Uhr keine");
}

static void testZeit() {
  char out[32];
  check(!eventtime::formatUnix(0u, "%Y", out, sizeof(out)),
        "Zeit: 0 ist keine Zeit");
  check(!eventtime::formatUnix(3600u, "%Y", out, sizeof(out)),
        "Zeit: Betriebssekunden sind keine Zeit");
  check(eventtime::formatUnix(1700000000u, "%Y", out, sizeof(out)),
        "Zeit: eine gestellte Uhr geht");
  checkEq(strtoul(out, nullptr, 10), 2023, "Zeit: Jahr aus dem Zeitstempel");
  check(!eventtime::formatUnix(1700000000u, "%Y", out, 0),
        "Zeit: ohne Platz kein Text");
  eventtime::formatDuration(1700000000u, 1700003661u, out, sizeof(out));
  check(strcmp(out, "01:01:01") == 0, "Dauer als Text");
  eventtime::formatDuration(1700000000u, 0u, out, sizeof(out));
  check(strcmp(out, "-") == 0, "Dauer ohne Ende als Strich");
}

int main() {
  testEinBlatt();
  testDieselbeSeiteZweimal();
  testZweiteSeite();
  testLeereTabelle();
  testAbgeschnitteneNutzlast();
  testZuKurz();
  testUeberlauf();
  testUnbekannterTyp();
  testGleicheArtZweimal();
  testDauer();
  testZeit();

  printf("%s: %d Prüfungen, %d fehlgeschlagen\n", g_failed ? "FEHLER" : "OK",
         g_checks, g_failed);
  return g_failed ? 1 : 0;
}