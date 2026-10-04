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

int main() {
  testNodeStaysLight();
  testWhiteIsNotAPanelColour();
  testPanelsKeepTheirColour();
  testBackgroundFollows();
  testLabelsOnTheBackground();
  testGreenFollows();
  testUntouched();

  printf("%s: %d Prüfungen, %d fehlgeschlagen\n",
         g_failed == 0 ? "OK" : "FEHLER", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}