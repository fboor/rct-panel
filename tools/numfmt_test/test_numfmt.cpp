// Host test for the number formatting of the display (src/NumFmt.h).
//
// The rule is small but it is the kind of small thing that is only ever checked
// by looking at a panel: a value that rounds to zero must not keep a minus. The
// display never prints a number alone though - it prints "%.1f kW" or
// "%+.3f kW  %.1f A" - so the sign has to be found inside the finished text, and
// the cases below are the ones that would break if that search were wrong.
//
// The header is compiled as shipped, with no Arduino and no hardware.
//
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include <string>

#include "NumFmt.h"

static int g_failed = 0;
static int g_checks = 0;

static void expect(const std::string &got, const std::string &want,
                   const char *what) {
  g_checks++;
  if (got != want) {
    g_failed++;
    std::printf("  FEHLER %-34s ist \"%s\", sollte \"%s\" sein\n", what,
                got.c_str(), want.c_str());
  }
}

static std::string dot(double v) {
  char b[64];
  fmtNum(b, sizeof(b), "%.1f kW", v);
  return b;
}

static std::string comma(double v) {
  char b[64];
  fmtNumComma(b, sizeof(b), "%.2f kW", v);
  return b;
}

// The variant that follows the build: a comma in the German one, a point in the
// English one. run.sh compiles this file twice, so both are checked.
static std::string lang(double v) {
  char b[64];
  fmtNumLang(b, sizeof(b), "%.2f kW", v);
  return b;
}

// Two pieces of a fmtPower() answer: the part that is the same in both languages
// (whole watts, with the sign), and the part that carries the separator (kW).
static std::string punkt(const char *want) { return want; }
static std::string langK(const char *mitKomma) {
  char b[64];
  // The expected string arrives with a comma; the build's separator replaces it.
  std::string s(mitKomma);
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == ',') {
      s[i] = langDecPoint();
    }
  }
  return s;
}

int main() {
  std::printf("== Anzeigewerte, die auf null runden ==\n");

  // The case the rule exists for: a rounding error below zero.
  expect(dot(-0.04), "0.0 kW", "-0.04 mit einer Nachkommastelle");
  expect(dot(-0.001), "0.0 kW", "-0.001 mit einer Nachkommastelle");
  expect(comma(-0.0001), "0,00 kW", "-0.0001 mit zwei Nachkommastellen");

  // Zero has no sign, and a value that rounds to zero from above keeps none
  // either - "0.0" must not become "-0.0" by any path.
  expect(dot(0.0), "0.0 kW", "exakt null");
  expect(dot(0.04), "0.0 kW", "+0.04");
  expect(comma(0.0), "0,00 kW", "exakt null, Komma");
  expect(dot(-0.0), "0.0 kW", "negative Null");

  // A real value keeps its sign - this is the half that must not break.
  expect(dot(-0.4), "-0.4 kW", "-0.4");
  expect(comma(-0.01), "-0,01 kW", "-0.01");
  expect(dot(-1.0), "-1.0 kW", "-1.0");
  expect(dot(-12.34), "-12.3 kW", "-12.34");
  expect(dot(1.5), "1.5 kW", "positiv");

  std::printf("== Dezimaltrennzeichen des Builds %s ==\n", RCT_LANG_NAME);
  {
    const std::string sep(1, langDecPoint());
    expect(lang(-0.0001), "0" + sep + "00 kW", "gerundete Null, Trennzeichen");
    expect(lang(-0.01), "-0" + sep + "01 kW", "echter Wert, Trennzeichen");
    expect(lang(1.5), "1" + sep + "50 kW", "positiver Wert");
    expect(lang(0.25), "0" + sep + "25 kW", "0.25 kW");
  }

  std::printf("== Anzeigewerte ohne Einheit ==\n");
  {
    char b[64];
    // "%.0f" of anything below half a watt prints "-0" - the sign is all that is
    // left of a value that is not there.
    fmtNum(b, sizeof(b), "%.0f W", -0.4);
    expect(b, "0 W", "-0.4 W ohne Nachkommastelle");
    fmtNum(b, sizeof(b), "%d %%", 0);
    expect(b, "0 %", "ganzzahlig, unveraendert");
    fmtNum(b, sizeof(b), "%+.3f kW", -0.0004);
    expect(b, "0.000 kW", "erzwungenes Vorzeichen auf null");
    // Two numbers in one string: both have to be looked at.
    fmtNum(b, sizeof(b), "%+.3f kW  %.1f A  %.1f V", -0.0004, -0.04, 52.3);
    expect(b, "0.000 kW  0.0 A  52.3 V", "zwei gerundete Nullen");
  }

  std::printf("== Text mit Bindestrichen bleibt unberuehrt ==\n");
  {
    char b[64];
    // A date: the dashes sit behind a digit, so they are not signs.
    strcpy(b, "2026-09-30");
    numDropNegZero(b);
    expect(b, "2026-09-30", "Datum");
    strcpy(b, "29.09.2026 20:33");
    numDropNegZero(b);
    expect(b, "29.09.2026 20:33", "Datum mit Zeit");
    // A lone dash in a text row, and a dash followed by a blank: no number, so
    // no sign to remove.
    strcpy(b, "L1 - L2");
    numDropNegZero(b);
    expect(b, "L1 - L2", "Bindestrich zwischen Text");
    strcpy(b, "Punkt - 0 von 3");
    numDropNegZero(b);
    expect(b, "Punkt - 0 von 3", "Bindestrich mit Leerzeichen");
    // A fault name from the panel, unchanged.
    strcpy(b, "IGBT L1 BH defekt");
    numDropNegZero(b);
    expect(b, "IGBT L1 BH defekt", "Stoerungstext");
    // Nothing at all.
    strcpy(b, "");
    numDropNegZero(b);
    expect(b, "", "leerer Puffer");
  }

  std::printf("== Grenzen ==\n");
  {
    char b[8];
    // Truncation must stay visible: the return value is snprintf's, and a
    // caller that checks it still can.
    const int n = fmtNum(b, sizeof(b), "%.1f kW", -0.04);
    (void)n;
    expect(b, "0.0 kW", "gekuerzter Puffer mit der Nullregel");
    // A value that is too long gets cut, and the sign rule must not resurrect
    // the part that did not fit.
    char small[4];
    fmtNum(small, sizeof(small), "%.1f kW", -1234.0);
    expect(small, "-12", "Restsinn nach dem Kuerzen");
  }

  {
    // fmtPower(): whole watts under 1 kW, kilowatts above it, and the separator
    // of the language. The row of figures on the overview is the reason: "0,38
    // kW" next to "380 W" is a row nobody can scan.
    char b[64];
    fmtPower(b, sizeof(b), 380.0f);
    expect(b, punkt("380 W"), "380 W bleiben Watt");
    fmtPower(b, sizeof(b), 0.0f);
    expect(b, punkt("0 W"), "0 W");
    fmtPower(b, sizeof(b), 999.0f);
    expect(b, punkt("999 W"), "999 W sind noch Watt");
    fmtPower(b, sizeof(b), 999.5f);
    expect(b, langK("1,00 kW"), "ab 1000 W wechselt die Einheit, ohne Luecke");
    fmtPower(b, sizeof(b), 1000.0f);
    expect(b, langK("1,00 kW"), "1000 W");
    fmtPower(b, sizeof(b), 5750.0f);
    expect(b, langK("5,75 kW"), "5750 W");
    fmtPower(b, sizeof(b), 12345.0f);
    expect(b, langK("12,35 kW"), "12345 W runden auf zwei Stellen");
    // Das Vorzeichen bleibt oberhalb der Grenze: negativ heisst hier Bezug, und
    // das ist eine Information, keine Rundung.
    fmtPower(b, sizeof(b), -810.0f);
    expect(b, punkt("-810 W"), "negativ bleibt negativ");
    fmtPower(b, sizeof(b), -5750.0f);
    expect(b, langK("-5,75 kW"), "negativ in kW");
    fmtPower(b, sizeof(b), -0.4f);
    expect(b, punkt("0 W"), "unter der Grenze faellt das Vorzeichen wie sonst");
  }

  std::printf("\n== %d Prüfungen, %d fehlgeschlagen ==\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
