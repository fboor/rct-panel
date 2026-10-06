// What the simulator fakes: everything the GUI reaches for that is not LVGL.
//
// The GUI is almost pure. Across its 3356 lines it calls twelve functions that
// live outside LVGL, plus Serial and the clock. This file provides those, and
// nothing else - a stub that is not needed is a second thing to keep in step.
//
// What is faked and how:
//
//   clock          millis/delay/micros from std::chrono, so LVGL timers fire at
//                  the panel's rate and a layout that needs a second gets a
//                  second, not a hurry
//   Serial         stdout, so the panel's own diagnostics are readable
//   device state   from a JSON file (sim_data.cpp) - the same DeviceState the
//                  driver fills, so Rules.h sees no difference
//   SD             no card: the status line says so, and a screenshot is the
//                  simulator's own job rather than the card's
//   network        up, because a page that never connects shows the wrong page
//   NVS            in memory, so the settings page has values in it
//
// SPDX-License-Identifier: MIT
#ifndef RCT_PANEL_SIM_STUBS_H
#define RCT_PANEL_SIM_STUBS_H

#include <cstdint>
#include <cstdarg>
#include <cstdio>

// Bring the fake up: the resolution the window was opened with, and the clock
// base that millis() counts from.
//
// `speed` scales the panel's clock against the wall clock. It exists because the
// GUI keeps the Wi-Fi overlay up for a ten-second boot test window
// (GuiApp.cpp: s_apTestUntil = millis() + 10000), and a screenshot taken inside
// that window shows the QR page rather than the one that was asked for. Advancing
// the clock twenty times faster reaches the same state in half a second - the
// panel's own timer logic runs unchanged, and only the wall-clock cost differs.
// At speed 1 nothing is scaled at all.
void simBegin(int breite, int hoehe, int speed);

// The board to draw. Set before simBegin(), because the GUI asks for it from the
// first object it creates.
struct UiLayout;
void simSetBoard(const UiLayout &board);

// One collection run out of the JSON file, at the rate the firmware polls.
void simPoll();

// Close the window and stop the web server. Called before a restart, so that the
// next process finds the port free instead of waiting for the kernel to release it.
void simQuit();

// Set the panel's clock to an exact millisecond value. Called immediately before a
// screenshot.
//
// Without this a picture cannot be compared with another picture: the clock counts
// real time, so two runs of the same build differ wherever the display shows an
// age - the uptime on the device page, the seconds since the last value, the
// connection age - and that is true of the panel too, but it makes "did this
// refactor move anything" unanswerable. Setting the clock to a fixed value before
// the snapshot makes the picture a function of the code and not of the wall clock.
void simSetClockMs(uint32_t ms);

// Remember the arguments, so that a restart can be execv()ed with the same ones.
// See sim_restart.cpp for why they are the same - and why the device ones are not.
void simMerkeArgumente(int argc, char **argv);

// The state as it was last read, for the log line the simulator prints.
const char *simDatenQuelle();

// --- the clock, which LVGL and the GUI both read ----------------------------
uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void yield();

// --- Serial ------------------------------------------------------------------
// A subset of the Arduino object, and only the subset the firmware calls: begin,
// print, println, printf, available, read, write, flush. Not a full HardwareSerial
// - a full one would be a second Arduino to keep working.
struct SimSerial {
  void begin(unsigned long baud);
  int available();
  int read();
  size_t write(const uint8_t *buf, size_t n);
  size_t print(const char *s);
  size_t print(char c);
  size_t println(const char *s);
  size_t println();
  size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  void flush();
  operator bool() const { return true; }
};
extern SimSerial Serial;

#endif // RCT_PANEL_SIM_STUBS_H