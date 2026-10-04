// Host test for the JSON scanner (src/device/Json.h).
//
// The scanner is what the second device family reads its values with, so the
// cases that matter are the ones the OpenInverterGateway actually produces: a
// flat object whose keys are register names, whose fields depend on the model,
// and whose strings carry colons and colons-free names. Everything is checked
// here rather than by watching a panel, and the answers come from the real
// answers of a simulator run captured in the file below.
//
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include <string>

#include "device/Json.h"

static int g_checks = 0;
static int g_failed = 0;

static void check(bool ok, const char *what) {
  g_checks++;
  if (!ok) {
    g_failed++;
    printf("FAIL  %s\n", what);
  }
}

static void checkNear(double got, double want, const char *what) {
  g_checks++;
  if (got - want > 1e-6 || want - got > 1e-6) {
    g_failed++;
    printf("FAIL  %s: got %f, want %f\n", what, got, want);
  }
}

static void checkStr(const char *got, const char *want, const char *what) {
  g_checks++;
  if (strcmp(got, want) != 0) {
    g_failed++;
    printf("FAIL  %s: got \"%s\", want \"%s\"\n", what, got, want);
  }
}

// A complete answer of the OpenInverterGateway simulator, byte for byte as it
// came off the wire. Fields: the simulated inverter's eleven values, then what
// the stick adds about itself.
static const char kSimStatus[] =
    "{\"Hostname\":\"OIG-SIM\",\"Status\":1,\"DcPower\":230,\"DcVoltage\":70.5,"
    "\"DcInputCurrent\":8.5,\"AcFreq\":50,\"AcVoltage\":230,\"AcPower\":0,"
    "\"EnergyToday\":0.3,\"EnergyTotal\":49.1,\"OperatingTime\":123456,"
    "\"Temperature\":21.12,\"AccumulatedEnergy\":320,\"Mac\":\"2C:F4:32:36:BA:0C\","
    "\"Cnt\":0,\"Uptime\":1278,\"WifiRSSI\":-62,\"HeapFree\":23864,"
    "\"HeapMaxAlloc\":23344,\"HeapMinFree\":23640,\"HeapFragmentation\":3}";

static size_t lenOf(const char *s) { return strlen(s); }

static void testNumbersFromTheWire() {
  double v = 0;
  check(json::getNumber(kSimStatus, lenOf(kSimStatus), "DcPower", &v),
        "DcPower vorhanden");
  checkNear(v, 230.0, "DcPower = 230");
  check(json::getNumber(kSimStatus, lenOf(kSimStatus), "DcVoltage", &v),
        "DcVoltage vorhanden");
  checkNear(v, 70.5, "DcVoltage = 70.5");
  check(json::getNumber(kSimStatus, lenOf(kSimStatus), "Temperature", &v),
        "Temperature vorhanden");
  checkNear(v, 21.12, "Temperature = 21.12");
  check(json::getNumber(kSimStatus, lenOf(kSimStatus), "EnergyToday", &v),
        "EnergyToday vorhanden");
  checkNear(v, 0.3, "EnergyToday = 0.3");
  check(json::getNumber(kSimStatus, lenOf(kSimStatus), "WifiRSSI", &v),
        "WifiRSSI vorhanden");
  checkNear(v, -62.0, "WifiRSSI = -62 (negative Zahl)");
  check(json::getNumber(kSimStatus, lenOf(kSimStatus), "AcPower", &v),
        "AcPower vorhanden");
  checkNear(v, 0.0, "AcPower = 0 (Nullwert, kein fehlender Wert)");
}

static void testStrings() {
  char buf[48] = {0};
  check(json::getString(kSimStatus, lenOf(kSimStatus), "Hostname", buf, sizeof(buf)),
        "Hostname vorhanden");
  checkStr(buf, "OIG-SIM", "Hostname = OIG-SIM");
  check(json::getString(kSimStatus, lenOf(kSimStatus), "Mac", buf, sizeof(buf)),
        "Mac vorhanden");
  checkStr(buf, "2C:F4:32:36:BA:0C", "Mac mit Doppelpunkten");
}

static void testAbsentKeys() {
  double v = 123.0;
  check(!json::getNumber(kSimStatus, lenOf(kSimStatus), "BatteryPercentage", &v),
        "BatteryPercentage fehlt");
  checkNear(v, 123.0, "der Rueckgabewert bleibt unberuehrt");
  check(!json::has(kSimStatus, lenOf(kSimStatus), "BatteryPercentage"),
        "has() false fuer fehlenden Schluessel");
  check(json::has(kSimStatus, lenOf(kSimStatus), "DcPower"),
        "has() true fuer vorhandenen Schluessel");
  // A prefix of an existing key is not a hit - otherwise "Ac" would match
  // "AcPower" and a driver would read the wrong register.
  check(!json::has(kSimStatus, lenOf(kSimStatus), "Ac"),
        "Praefix ist kein Treffer");
  check(!json::has(kSimStatus, lenOf(kSimStatus), "DcPowe"),
        "abgeschnittener Schluessel ist kein Treffer");
  check(!json::has(kSimStatus, lenOf(kSimStatus), "DcPowerX"),
        "verlaengerter Schluessel ist kein Treffer");
}

// A number that looks like a key: the value after it must not be mistaken for
// one, and a key that looks like a value must be skipped over correctly.
static void testValueShapes() {
  double v = 0;
  const char *j = "{\"a\":1,\"b\":\"x,y\",\"c\":{\"d\":2},\"target\":42}";
  check(json::getNumber(j, lenOf(j), "target", &v), "Ziel hinter Sonderwerten");
  checkNear(v, 42.0, "target = 42");
  check(json::has(j, lenOf(j), "c"), "verschachteltes Objekt als Schluessel");
  check(!json::getNumber(j, lenOf(j), "d", &v),
        "Schluessel eines verschachtelten Objekts ist nicht sichtbar");

  // Arrays mit Klammern in Strings: der Scanner darf sich nicht aus dem
  // Gleichgewicht bringen lassen.
  const char *k = "{\"arr\":[{\"n\":\"{\"},{\"n\":\"}\"}],\"after\":7}";
  check(json::getNumber(k, lenOf(k), "after", &v), "Ziel hinter einem Array");
  checkNear(v, 7.0, "after = 7");

  // Exponent und Vorzeichen.
  const char *e = "{\"small\":1e-3,\"neg\":-0.5,\"big\":2.5E4}";
  check(json::getNumber(e, lenOf(e), "small", &v), "Exponent klein");
  checkNear(v, 0.001, "small = 1e-3");
  check(json::getNumber(e, lenOf(e), "neg", &v), "negative Zahl");
  checkNear(v, -0.5, "neg = -0.5");
  check(json::getNumber(e, lenOf(e), "big", &v), "Exponent gross");
  checkNear(v, 25000.0, "big = 2.5E4");
}

// Was der Treiber braucht, um zu erkennen, dass ein Schluessel keine Zahl ist.
static void testWrongTypes() {
  double v = 0;
  check(!json::getNumber(kSimStatus, lenOf(kSimStatus), "Hostname", &v),
        "String ist keine Zahl");
  char buf[48] = {0};
  check(!json::getString(kSimStatus, lenOf(kSimStatus), "DcPower", buf, sizeof(buf)),
        "Zahl ist kein String");
  const char *j = "{\"n\":null,\"t\":true,\"f\":false}";
  check(!json::getNumber(j, lenOf(j), "n", &v), "null ist keine Zahl");
  check(!json::getNumber(j, lenOf(j), "t", &v), "true ist keine Zahl");
  check(json::has(j, lenOf(j), "n"), "null als Schluessel existiert");
  check(json::has(j, lenOf(j), "t"), "true als Schluessel existiert");
}

// Kaputte Eingaben dürfen den Speicher nicht hinter sich lassen.
static void testTruncated() {
  double v = 0;
  check(!json::getNumber("", 0, "x", &v), "leeres Dokument");
  check(!json::getNumber("{", 1, "x", &v), "nur eine Klammer");
  check(!json::getNumber("{\"a\":", 5, "a", &v), "abgeschnittener Wert");
  check(!json::getNumber("{\"a\":1", 5, "a", &v), "abgeschnittenes Ende");
  check(!json::getNumber("{\"a\"", 4, "a", &v), "abgeschnittener Schluessel");
  check(json::getNumber("{\"a\":12}", 7, "a", &v),
        "genau am Ende ist ein Wert noch lesbar");
  checkNear(v, 12.0, "a = 12 am Ende");
  char buf[4] = {0};
  json::getString("{\"Hostname\":\"ein-sehr-langer-name\"}", 33, "Hostname", buf,
                  sizeof(buf));
  check(strlen(buf) < 4, "String wird abgeschnitten statt ueberlaufen");
}

// Zwei Zeichensätze, die es auf einem Geraet mit 8-Bit-Zeichenfeld gibt.
static void testHighBytes() {
  char buf[48] = {0};
  const char *j = "{\"Name\":\"Wechselrichter \\u00fc\",\"x\":1}";
  check(json::getString(j, lenOf(j), "Name", buf, sizeof(buf)), "Name mit Escape");
  checkStr(buf, "Wechselrichter \xc3\xbc", "Escapes werden uebernommen");
  double v = 0;
  check(json::getNumber(j, lenOf(j), "x", &v), "Zahl hinter einem Escape-String");
  checkNear(v, 1.0, "x = 1");
}

int main() {
  testNumbersFromTheWire();
  testStrings();
  testAbsentKeys();
  testValueShapes();
  testWrongTypes();
  testTruncated();
  testHighBytes();

  printf("%s: %d Prüfungen, %d fehlgeschlagen\n",
         g_failed == 0 ? "OK" : "FEHLER", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}