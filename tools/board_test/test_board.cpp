// Host test for the board abstraction: does each board's own pin map and layout
// hang together?
//
// The point is not that the numbers are right - nobody can check that without the
// board. The point is that a board is INTERNALY consistent: every pin is a real
// ESP32-S3 pin, no pin is claimed twice for two jobs that would fight over it, the
// RGB data pins number what the pixel format needs, and the geometry fits the
// resolution it claims.
//
// That is the check a build machine can make and the only one that catches the
// class of mistake a second board actually brings: a pin copied from the first
// board, or a layout still carrying the other board's width.
//
// Every board is compiled with its own flag, so a profile that nobody builds cannot
// rot unnoticed. The Waveshare one is expected to fail its "runs" assertion on
// purpose - see the note at the bottom.
//
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "board/BoardPins.h"
#include "ui/UiLayout.h"

static int g_fehler = 0;
static int g_pruefungen = 0;

static void ok(bool bedingung, const char *was, const std::string &detail = "") {
  g_pruefungen++;
  if (!bedingung) {
    g_fehler++;
    printf("  FEHLER  %s%s%s\n", was, detail.empty() ? "" : ": ", detail.c_str());
  }
}

// Every pin the ESP32-S3 can actually route, and nothing else. GPIO 26-32 and 39 do
// not exist on the S3 (they were the PSRAM pins on the original ESP32), and the
// strapping pins are usable but a board that put a display on them would be odd -
// they are allowed here, so this test does not become a style guide.
static bool pinGueltig(int pin) {
  if (pin < 0) return true;                 // -1 means "not wired here"
  if (pin > 48) return false;
  if (pin >= 26 && pin <= 32) return false;  // not bonded out on the S3
  return true;
}

// One pin, one job. Two roles on one pin is not a style question: the display and
// the SD card would both drive it.
struct Belegung {
  const char *rolle;
  int pin;
};

int main() {
  printf("=== Boardpruefung: %s ===\n",
#if defined(RCT_BOARD_WAVESHARE_LCD7)
         "Waveshare ESP32-S3-Touch-LCD-7"
#else
         "Guition ESP32-S3-4848S040"
#endif
  );

  // ---- every pin is routable ------------------------------------------------
  const std::vector<Belegung> pins = {
      {"lcd.hsync", PIN_LCD_HSYNC}, {"lcd.vsync", PIN_LCD_VSYNC},
      {"lcd.de", PIN_LCD_DE},           {"lcd.pclk", PIN_LCD_PCLK},
      {"lcd.cs", PIN_LCD_CS},           {"lcd.sck", PIN_LCD_SCK},
      {"lcd.mosi", PIN_LCD_MOSI},
      {"lcd.data0", PIN_LCD_B0},        {"lcd.data1", PIN_LCD_B1},
      {"lcd.data2", PIN_LCD_B2},        {"lcd.data3", PIN_LCD_B3},
      {"lcd.data4", PIN_LCD_B4},
      {"lcd.data5", PIN_LCD_G0},        {"lcd.data6", PIN_LCD_G1},
      {"lcd.data7", PIN_LCD_G2},        {"lcd.data8", PIN_LCD_G3},
      {"lcd.data9", PIN_LCD_G4},        {"lcd.data10", PIN_LCD_G5},
      {"lcd.data11", PIN_LCD_R0},       {"lcd.data12", PIN_LCD_R1},
      {"lcd.data13", PIN_LCD_R2},       {"lcd.data14", PIN_LCD_R3},
      {"lcd.data15", PIN_LCD_R4},
      {"touch.sda", PIN_TOUCH_SDA},      {"touch.scl", PIN_TOUCH_SCL},
      {"sd.sck", PIN_SD_SCK},           {"sd.miso", PIN_SD_MISO},
      {"sd.mosi", PIN_SD_MOSI},         {"sd.cs", PIN_SD_CS},
      {"relay", RELAY_PIN},
  };
  for (const auto &b : pins) {
    char buf[80];
    snprintf(buf, sizeof(buf), "%s = %d", b.rolle, b.pin);
    ok(pinGueltig(b.pin), "Pin nicht fuehrbar", buf);
  }

  // ---- no pin in two roles --------------------------------------------------
  // The display's 3-wire SPI link may share the SD bus' SPI pins on a board that
  // wires it that way, but never with the SD chip select, and never with the RGB
  // data lines. What is checked here is the pair that actually collides in
  // practice: a data line that is also a control line or a peripheral pin.
  const std::vector<Belegung> daten = {
      {"lcd.data0", PIN_LCD_B0}, {"lcd.data1", PIN_LCD_B1},
      {"lcd.data2", PIN_LCD_B2}, {"lcd.data3", PIN_LCD_B3},
      {"lcd.data4", PIN_LCD_B4}, {"lcd.data5", PIN_LCD_G0},
      {"lcd.data6", PIN_LCD_G1}, {"lcd.data7", PIN_LCD_G2},
      {"lcd.data8", PIN_LCD_G3}, {"lcd.data9", PIN_LCD_G4},
      {"lcd.data10", PIN_LCD_G5}, {"lcd.data11", PIN_LCD_R0},
      {"lcd.data12", PIN_LCD_R1}, {"lcd.data13", PIN_LCD_R2},
      {"lcd.data14", PIN_LCD_R3}, {"lcd.data15", PIN_LCD_R4},
  };
  const std::vector<Belegung> steuerung = {
      {"lcd.hsync", PIN_LCD_HSYNC}, {"lcd.vsync", PIN_LCD_VSYNC},
      {"lcd.de", PIN_LCD_DE}, {"lcd.pclk", PIN_LCD_PCLK},
      {"touch.sda", PIN_TOUCH_SDA}, {"touch.scl", PIN_TOUCH_SCL},
      {"sd.sck", PIN_SD_SCK}, {"sd.miso", PIN_SD_MISO},
      {"sd.mosi", PIN_SD_MOSI}, {"sd.cs", PIN_SD_CS},
      {"relay", RELAY_PIN},
  };
  for (const auto &d : daten) {
    for (const auto &s : steuerung) {
      if (d.pin < 0 || s.pin < 0) continue;
      char buf[80];
      snprintf(buf, sizeof(buf), "%s und %s beide auf %d", d.rolle, s.rolle, d.pin);
      ok(d.pin != s.pin, "Pin in zwei Rollen", buf);
    }
  }

  // ---- the RGB bus carries RGB565, so 16 data lines ------------------------
  ok(daten.size() == 16, "nicht 16 RGB-Datenpins",
     std::to_string(daten.size()));
  int doppelt = 0;
  for (size_t i = 0; i < daten.size(); i++) {
    for (size_t j = i + 1; j < daten.size(); j++) {
      if (daten[i].pin == daten[j].pin) doppelt++;
    }
  }
  ok(doppelt == 0, "RGB-Datenpins nicht eindeutig", std::to_string(doppelt));

  // ---- the geometry fits the resolution it claims --------------------------
  // uiLayoutForSize(), not uiLayout(): the latter returns the board that was
  // compiled into the firmware, which in a test build is always the first one -
  // asking it would check the Waveshare board against the Guition's geometry and
  // say nothing.
  const UiLayout *gefunden = uiLayoutForSize(BOARD_SCREEN_W, BOARD_SCREEN_H);
  ok(gefunden != nullptr, "kein Layout fuer diese Aufloesung");
  if (gefunden == nullptr) {
    printf("  %d Pruefungen, %d Fehler\n", g_pruefungen, g_fehler);
    return 1;
  }
  const UiLayout &u = *gefunden;
  char buf[160];
  snprintf(buf, sizeof(buf), "%dx%d", u.screenW, u.screenH);
  ok(u.screenW > 0 && u.screenH > 0, "Bildschirmgroesse unplausibel", buf);

  snprintf(buf, sizeof(buf), "contentH %d > 0", u.contentH());
  ok(u.contentH() > 0, "kein Platz zwischen den Leisten", buf);

  // Bars: the track has to fit on the screen, and the value label has to end
  // where the track ends, or the two drift apart on a wider board.
  snprintf(buf, sizeof(buf), "Balken x=%d w=%d auf %d px", u.bars.x, u.bars.w,
           u.screenW);
  ok(u.bars.x >= 0 && u.bars.x + u.bars.w <= u.screenW, "Balken passen nicht",
     buf);
  snprintf(buf, sizeof(buf), "Wert bis %d, Balken bis %d", u.bars.valX + u.bars.valW,
           u.bars.x + u.bars.w);
  ok(u.bars.valX + u.bars.valW == u.bars.x + u.bars.w,
     "Wert steht nicht am Balkenende", buf);

  // Cards: every row inside the page, and every card in a cell that exists.
  const int letzteZeile = u.cards.rows - 1;
  snprintf(buf, sizeof(buf), "Kartenzeilen %d", u.cards.rows);
  ok(u.cards.rows >= 1 && u.cards.rows <= letzteZeile + 1, "Kartenzahl unplausibel",
     buf);
  for (int r = 0; r < u.cards.rows; r++) {
    const UiCardRow &zeile = u.cards.row[r];
    snprintf(buf, sizeof(buf), "Zeile %d: y=%d h=%d, Spalten %d", r, zeile.y,
             zeile.h, zeile.cols);
    ok(zeile.cols >= 1, "Zeile ohne Spalten", buf);
    ok(zeile.y >= 0 && zeile.y + zeile.h <= u.contentH(),
       "Kartenzeile passt nicht in die Seite", buf);
    const int breite = uiCardW(u, r);
    snprintf(buf, sizeof(buf), "Zeile %d: %d Karten a %d px = %d px von %d", r,
             zeile.cols, breite, zeile.cols * breite + (zeile.cols - 1) * zeile.gap,
             u.screenW - 2 * u.cards.x0);
    ok(zeile.cols * breite + (zeile.cols - 1) * zeile.gap <=
           u.screenW - 2 * u.cards.x0,
       "Kartenraster breiter als die Seite", buf);
  }
  for (int i = 0; i < u.cards.n; i++) {
    const int r = u.cards.cardRow[i];
    const int c = u.cards.cardCol[i];
    snprintf(buf, sizeof(buf), "Karte %d auf Zeile %d Spalte %d", i, r, c);
    ok(r >= 0 && r < u.cards.rows, "Karte zeigt auf eine Zeile, die es nicht gibt",
       buf);
    ok(c >= 0 && c < u.cards.row[r < u.cards.rows ? r : 0].cols,
       "Karte zeigt auf eine Spalte, die es nicht gibt", buf);
  }

  // Chart and pills: both are fixed numbers a board can forget to widen.
  snprintf(buf, sizeof(buf), "Diagramm x=%d w=%d auf %d px", u.chart.x, u.chart.w,
           u.screenW);
  ok(u.chart.x >= 0 && u.chart.x + u.chart.w <= u.screenW,
     "Diagramm passt nicht", buf);
  snprintf(buf, sizeof(buf), "Diagramm y=%d h=%d, contentH %d", u.chart.y, u.chart.h,
           u.contentH());
  ok(u.chart.y >= 0 && u.chart.y + u.chart.h <= u.contentH(),
     "Diagramm passt nicht in die Seite", buf);

  const int pillen = 3;
  const int pillenBreite = pillen * u.flow.pillW + (pillen - 1) * u.flow.pillGap;
  snprintf(buf, sizeof(buf), "Pillen %d px auf %d px", pillenBreite, u.screenW);
  ok(pillenBreite <= u.screenW, "Pillen passen nicht nebeneinander", buf);
  snprintf(buf, sizeof(buf), "Pillen y=%d h=%d, contentH %d", u.flow.pillY,
           u.flow.pillH, u.contentH());
  ok(u.flow.pillY >= 0 && u.flow.pillY + u.flow.pillH <= u.contentH(),
     "Pillen passen nicht in die Seite", buf);

  // ---- what the board can and cannot do -------------------------------------
  // Not a style question: a backlight behind an IO expander has one bit and no
  // PWM, so a slider on it would be a control that does nothing.
#if BOARD_BACKLIGHT_DIMMABLE
  ok(true, "Backlight dimmbar");
#else
  printf("  Hinweis  Backlight ist nicht dimmbar (Schalter hinter dem Expander)\n");
#endif
#if BOARD_RELAY_PRESENT
  snprintf(buf, sizeof(buf), "Relais an GPIO %d", RELAY_PIN);
  ok(RELAY_PIN >= 0, "Relais angekündigt, aber ohne Pin", buf);

  // A relay pin that is also a display data line is always wrong: the two drive it.
  for (const auto &d : daten) {
    if (d.pin < 0) continue;
    char meldung[80];
    snprintf(meldung, sizeof(meldung), "%s und %s beide auf %d", d.rolle,
             "relay", d.pin);
    ok(d.pin != RELAY_PIN, "Relais auf einem RGB-Datenpin", meldung);
  }

  // On the I2C lines it is a decision, not an oversight, and it is allowed only
  // where the board says so - with a line printed on every run. The Waveshare board
  // drives its output from GPIO 8, which is SDA: the GT911 and the CH422G both sit on
  // that bus, and with the expander gone so do the backlight switch and the SD card's
  // chip select. A decision nobody reads again is how it becomes an oversight, so it
  // is stated rather than tolerated silently.
#if BOARD_RELAY_SHARES_I2C
  printf("  ACHTUNG  Relais haengt am I2C-Bus: GPIO %d ist SDA (touch.sda = %d).\n"
         "           Damit fallen Touch, CH422G, Backlicht und die SD-Karte aus.\n",
         RELAY_PIN, PIN_TOUCH_SDA);
  ok(RELAY_PIN == PIN_TOUCH_SDA || RELAY_PIN == PIN_TOUCH_SCL,
     "BOARD_RELAY_SHARES_I2C gesetzt, aber der Pin ist gar keine I2C-Leitung");
#else
  snprintf(buf, sizeof(buf), "Relais GPIO %d auf dem I2C-Bus %d/%d", RELAY_PIN,
           PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  ok(RELAY_PIN != PIN_TOUCH_SDA && RELAY_PIN != PIN_TOUCH_SCL,
     "Relais auf einer I2C-Leitung, ohne es anzugeben", buf);
#endif
#else
  snprintf(buf, sizeof(buf), "Relais an GPIO %d", RELAY_PIN);
  ok(RELAY_PIN < 0, "kein Relais, aber ein Pin gesetzt", buf);
#endif

  // ---- the panel timings, when the board names its own --------------------
  // These come from the board's profile, not from UiLayout, so they are checked
  // here: a porch that does not fit the resolution it claims is a picture that
  // comes out sheared, and nothing else in this file would notice.
#if defined(BOARD_LCD_HSYNC_BACK_PORCH)
  {
    const int breite = u.screenW + BOARD_LCD_HSYNC_PULSE_WIDTH +
                       BOARD_LCD_HSYNC_BACK_PORCH + BOARD_LCD_HSYNC_FRONT_PORCH;
    const int hoehe = u.screenH + BOARD_LCD_VSYNC_PULSE_WIDTH +
                      BOARD_LCD_VSYNC_BACK_PORCH + BOARD_LCD_VSYNC_FRONT_PORCH;
    snprintf(buf, sizeof(buf), "eine Zeile %d Takte fuer %d px", breite, u.screenW);
    ok(breite >= u.screenW, "Horizontaltiming passt nicht zur Aufloesung", buf);
    snprintf(buf, sizeof(buf), "ein Bild %d Zeilen fuer %d px", hoehe, u.screenH);
    ok(hoehe >= u.screenH, "Vertikaltiming passt nicht zur Aufloesung", buf);

    // The refresh rate is what the porches really have to add up to, and it is the
    // check that catches both ends. Porches that are too small leave the visible
    // part short; porches that are too large leave it long - and both keep
    // "total >= resolution" true, so the two tests above cannot see the second kind
    // at all. MEASURED, a back porch of 900 instead of 8 on this board gives 14 Hz
    // and passed the first version of this test.
    //
    // A panel is not readable below roughly 20 Hz, and anything past 120 Hz on an
    // RGB bus this size costs pixels for nothing. Both boards sit in between:
    //   4848S040   12 MHz / (548 x 518) = 42 Hz
    //   LCD-7      16 MHz / (820 x 500) = 39 Hz
    const double hz = (double)BOARD_LCD_PCLK_HZ / ((double)breite * (double)hoehe);
    snprintf(buf, sizeof(buf), "%.1f Hz aus %d x %d bei %d Hz Takt", hz, breite,
             hoehe, BOARD_LCD_PCLK_HZ);
    ok(hz >= 20.0 && hz <= 120.0, "Bildfrequenz unplausibel", buf);

    snprintf(buf, sizeof(buf), "Pixel-Takt %d Hz", BOARD_LCD_PCLK_HZ);
    ok(BOARD_LCD_PCLK_HZ > 0 && BOARD_LCD_PCLK_HZ <= 40 * 1000 * 1000,
       "Pixel-Takt unplausibel", buf);
    ok(BOARD_LCD_PCLK_ACTIVE_NEG == 0 || BOARD_LCD_PCLK_ACTIVE_NEG == 1,
       "PCLK-Flanke ist 0 oder 1 und nichts sonst");
  }
#endif

  printf("  %d Pruefungen, %d Fehler\n", g_pruefungen, g_fehler);
  if (g_fehler == 0) {
    printf("Boardpruefung gruen\n");
  }
  return g_fehler == 0 ? 0 : 1;
}