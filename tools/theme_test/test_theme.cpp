// Host test for the theme walk's decision (src/gui/Theme.h).
//
// Header-only and free of LVGL, so this needs nothing but a host compiler and
// compiles the shipped header.
//
// Why it exists: the decision was two if-statements inside the walk, untestable
// without a display, and one of its comparisons was wrong for as long as the
// light theme existed. The node fill was pure white - also the light page
// background - so a white node read as something sitting on the page, and
// switching to dark repainted it. The panel was right after every boot and
// wrong after every theme switch, and only a person tapping the button noticed.
// The first case below is that bug, kept as a test.
//
// SPDX-License-Identifier: MIT
#include <cmath>
#include <cstdio>

#include "gui/Theme.h"

static int g_checks = 0;
static int g_failed = 0;

static void check(bool ok, const char *what) {
  g_checks++;
  if (!ok) {
    g_failed++;
    printf("FAIL  %s\n", what);
  }
}

// The project's own colours, as the 24-bit values the header works with.
static const int32_t kBgDark = 0x101418;
static const int32_t kBgLight = 0xFFFFFF;
static const int32_t kTextDark = 0xE8ECF1;
static const int32_t kTextLight = 0x101418;
static const int32_t kOk = 0x3EC97A;
static const int32_t kCard = 0x1C222A;
static const int32_t kBar = 0x3A4550;
static const int32_t kNodeFill = 0xF2F4F7; // GuiApp.cpp: FLOW_WHITE
static const int32_t kMuted = 0x8A94A0;

// Dark -> light, the switch the button does first.
static ThemeDecision toLight(bool bgOwn, int32_t bg, int32_t text,
                             bool aufBg = true) {
  return themeDecision(bgOwn, bg, kBgDark, kBgLight, text, kTextDark, kTextLight,
                       kOk, kOk, aufBg);
}

// Light -> dark.
static ThemeDecision toDark(bool bgOwn, int32_t bg, int32_t text,
                            bool aufBg = true) {
  return themeDecision(bgOwn, bg, kBgLight, kBgDark, text, kTextLight, kTextDark,
                       kOk, kOk, aufBg);
}

// THE BUG. A node whose fill was pure white, switching to dark: the fill has to
// stay light, because a node is a light disc with a ring in both themes.
// With the fill equal to the light page background the comparison could not tell
// a node from the page, and the node came out dark.
static void testNodeStaysLight() {
  const ThemeDecision d = toDark(true, kNodeFill, kTextDark);
  check(d.bg == -1, "Knoten: Fuellung bleibt unberuehrt (heller Kreis)");
  check(d.childAufBg == false,
        "Knoten: gilt als eigene Flaeche, Kinder nicht auf der Seite");

  // And the same shape in the other direction, where it worked before: the fill
  // is not the dark page either, so nothing happens going to light as well.
  const ThemeDecision h = toLight(true, kNodeFill, kTextDark);
  check(h.bg == -1, "Knoten: auch zum hellen Theme hin unberuehrt");
}

// The collision itself, kept as a test so the colour is never used again: white
// is the light page, so a white panel cannot be recognised as a panel.
static void testWhiteIsNotAPanelColour() {
  check(kNodeFill != kBgLight, "Knotenfuellung ist nicht die helle Seite");
  check(kNodeFill != kBgDark, "Knotenfuellung ist auch nicht die dunkle Seite");
  check(kBgLight != kBgDark, "die beiden Seiten unterscheiden sich");
}

// Panels of their own keep their colour: the cards, the bars, the code row.
static void testPanelsKeepTheirColour() {
  check(toLight(true, kCard, kTextDark).bg == -1, "Karte behaelt ihre Farbe");
  check(toDark(true, kCard, kTextDark).bg == -1, "Karte behaelt sie auch zurueck");
  check(toLight(true, kBar, kTextDark).bg == -1, "Balken behaelt seine Farbe");
  check(toDark(true, kBar, kMuted).bg == -1, "Balken behaelt sie auch zurueck");
}

// The page itself and what sits directly on it.
static void testBackgroundFollows() {
  const ThemeDecision l = toLight(true, kBgDark, kTextDark);
  check(l.bg == kBgLight, "Seite wird hell");
  check(l.text == kTextLight, "Text auf der Seite wird dunkel");
  check(l.childAufBg == true, "Kinder der Seite liegen weiter auf der Seite");

  const ThemeDecision d = toDark(true, kBgLight, kTextLight);
  check(d.bg == kBgDark, "Seite wird dunkel");
  check(d.text == kTextDark, "Text auf der Seite wird hell");
}

// A label has no background of its own. It must not end the chain, or the first
// plain label on a page would leave everything below it unthemed - which is why
// opacity is asked as well as the colour: an object without a background reports
// the default colour, white, which in the light theme even collides with the
// page.
static void testLabelsOnTheBackground() {
  const ThemeDecision d = toLight(false, 0xFFFFFF, kTextDark);
  check(d.bg == -1, "Label ohne Flaeche behaelt sie");
  check(d.text == kTextLight, "Label auf der Seite wechselt mit");
  check(d.childAufBg == true, "Label beendet die Kette nicht");

  // White text on a card is not text on the page: the card is above, so the
  // walk must leave it alone. That is what childAufBg == false means.
  const ThemeDecision c = toLight(true, kCard, 0xFFFFFF, false);
  check(c.text == -1, "weisser Text auf einer Karte bleibt weiss");
}

// The green follows the theme wherever it stands, by colour and not by place.
static void testGreenFollows() {
  check(toLight(true, kBgDark, kOk).text == kOk, "Gruen auf der Seite bleibt");
  check(toLight(true, kCard, kOk).text == kOk,
        "Gruen auf der Karte wird mitgezogen");
  check(toDark(true, kCard, kOk).text == kOk,
        "Gruen auf der Karte bleibt auch zurueck");
  // A muted caption is neither page text nor green, so it keeps its colour.
  check(toLight(true, kCard, kMuted).text == -1, "gedaempfter Text bleibt");
}

// Nothing to do is a valid answer and must leave both colours alone.
static void testUntouched() {
  const ThemeDecision d = toDark(true, kCard, kMuted);
  check(d.bg == -1 && d.text == -1, "unberuehrt heisst unberuehrt");
  // The card keeps its surface. Text in the page-text colour on that card does
  // change - the rule swaps by colour, and two places that are both "the text
  // colour" cannot be told apart by the walk. That is the documented rule, not
  // an accident: a label in the page-text colour is a label on the page, and the
  // panel has none that would want the other behaviour.
  const ThemeDecision p = toLight(true, kCard, kTextDark);
  check(p.bg == -1, "Karte behaelt ihre Flaeche");
  check(p.text == kTextLight,
        "Text in der Farbe des Seitentexts wird mitgezogen");
}

// A row that keeps its own surface must also keep a text that can be read on
// it. This is the second half of the rule the test above documents, and it is
// the half that bit: the code row and the output row on the Service page were
// given the theme's text colour on a COL_BAR background. COL_BAR is dark in both
// themes, the walk swaps the theme's text colour by value, and so tapping the
// theme button repainted both rows dark on dark - in the light theme only, which
// is why the panel looked right until somebody did it.
//
// The check is contrast and not colour identity on purpose. "The text must not
// be the page-text colour" would pass the moment somebody picked a third shade
// of dark grey that the walk happens not to know, and the row would still be
// unreadable. A ratio cannot be argued with: 3:1 is the level at which a
// graphical object still counts as visible, and this is text on a background.
static double relativeLuminance(int32_t rgb) {
  auto lin = [](double c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * lin((double)((rgb >> 16) & 0xFF) / 255.0) +
         0.7152 * lin((double)((rgb >> 8) & 0xFF) / 255.0) +
         0.0722 * lin((double)(rgb & 0xFF) / 255.0);
}

static double contrast(int32_t a, int32_t b) {
  const double la = relativeLuminance(a), lb = relativeLuminance(b);
  const double hi = la > lb ? la : lb, lo = la > lb ? lb : la;
  return (hi + 0.05) / (lo + 0.05);
}

static void testTextOnItsOwnRow() {
  // What the three buttons in the right column of the Service page and the
  // navigation bar now use, on the bar colour they all sit on.
  check(contrast(kBar, kNodeFill) >= 3.0,
        "weisse Schrift auf der Zeilenflaeche ist lesbar");

  // The pair as it was: the dark theme's text on the same bar. Kept as the case
  // that must not come back, and it is the reason the fix is at the call site -
  // the walk is behaving as documented when it swaps this colour.
  check(contrast(kBar, kTextDark) >= 3.0,
        "heller Text auf der Zeilenflaeche ist lesbar");
  check(contrast(kBar, kTextLight) < 3.0,
        "dunkler Text auf der Zeilenflaeche ist unlesbar - die alte Fassung");

  // The walk does what it is documented to do: it swaps that colour. So the walk
  // cannot be the place where this is caught - only the caller can, by not
  // handing the walk a colour it will swap onto a surface that does not move.
  const ThemeDecision d = toLight(true, kBar, kTextDark);
  check(d.bg == -1, "die Flaeche bleibt, wie sie ist");
  check(d.text == kTextLight,
        "und die Schrift wird trotzdem getauscht - der Aufrufer muss es richtig "
        "machen");

  // A text that the walk does not know is left alone, which is what makes a
  // fixed colour the working answer.
  check(toLight(true, kBar, kNodeFill).text == -1,
        "feste helle Schrift wird nicht angefasst");
  check(toDark(true, kBar, kNodeFill).text == -1,
        "in beide Richtungen nicht");
}

int main() {
  testNodeStaysLight();
  testWhiteIsNotAPanelColour();
  testPanelsKeepTheirColour();
  testBackgroundFollows();
  testLabelsOnTheBackground();
  testGreenFollows();
  testUntouched();
  testTextOnItsOwnRow();

  printf("%s: %d Prüfungen, %d fehlgeschlagen\n",
         g_failed == 0 ? "OK" : "FEHLER", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}