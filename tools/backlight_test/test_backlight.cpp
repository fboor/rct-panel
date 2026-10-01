// Host test for the backlight: src/display/Backlight.cpp is included as-is,
// with Arduino replaced by the stub next to this file (millis() from a global,
// the LEDC calls recorded in a variable).
//
// What cannot be checked on the bench without sitting in front of the panel for
// ten minutes: that the three-minute dimming and the five-minute switch-off
// arrive when they should, that a touch brings the light back within the short
// wake ramp and restarts both timers, that the millis() wrap after 49 days is
// harmless, and that a panel without a touch controller never switches itself
// off - which would be a dead panel, not a dimming one.
//
// The numbers here (1024 full, 307 = 30 %, 3 min / 5 min, 400 ms wake, 1500 ms
// fade) are written out by hand on purpose: they are the values the header
// comment promises, so a change in Backlight.cpp that breaks a promise fails
// here instead of on the wall.
//
// Build and run:
//   g++ -std=c++17 -Itools/backlight_test/stubs -Isrc -o /tmp/backlight_test tools/backlight_test/test_backlight.cpp && /tmp/backlight_test

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "Arduino.h"

uint32_t g_millis = 0;
StubSerial Serial;

// --- recorded LEDC / pin state ---------------------------------------------

static const uint32_t kFull = 1024; // 2^10, see ledcWrite() in the core
static const uint32_t kDim = 307;   // 30 %

static int g_pin = -1;
static int g_pinModeSeen = -1;
static int g_pinLevel = -1;
static int g_ledcChannel = -1;
static int g_ledcFreq = -1;
static int g_ledcBits = -1;
static int g_ledcPin = -1;
static uint32_t g_ledcDuty = 0xFFFFFFFF; // "never written"
static int g_ledcFail = 0;
static int g_writes = 0; // how often ledcWrite() was called at all

void pinMode(int pin, int mode) {
  g_pin = pin;
  g_pinModeSeen = mode;
}
void digitalWrite(int pin, int level) {
  (void)pin;
  g_pinLevel = level;
}
uint32_t ledcSetup(uint8_t channel, uint32_t freq, uint8_t resolution) {
  if (g_ledcFail) {
    return 0; // what the driver answers when it cannot do it
  }
  g_ledcChannel = channel;
  g_ledcFreq = (int)freq;
  g_ledcBits = (int)resolution;
  return freq;
}
void ledcAttachPin(uint8_t pin, uint8_t chan) {
  g_ledcPin = pin;
  g_ledcChannel = chan;
}
void ledcWrite(uint8_t chan, uint32_t duty) {
  (void)chan;
  g_ledcDuty = duty;
  g_writes++;
}
void StubSerial::printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

// The module under test.
#include "display/Backlight.cpp"

// ---------------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------------

static int g_fail = 0;
static int g_checks = 0;

static void check(bool ok, const char *what) {
  g_checks++;
  if (!ok) {
    g_fail++;
    printf("  FAIL  %s\n", what);
  }
}

static uint32_t duty() { return g_ledcDuty; }

// Back to the state right after boot: light on, nothing touched.
static void bootPanel() {
  g_millis = 0;
  g_pinLevel = -1;
  g_ledcDuty = 0xFFFFFFFF;
  g_ledcFail = 0;
  g_writes = 0;
  backlightInit();
  backlightSetWakeable(true);
}

// Let time pass in 100 ms steps, calling the housekeeping every step - what the
// LVGL loop does (many times over, but the state machine must not care how
// often it is called). Counted in elapsed time rather than as "until the clock
// reaches X", so it also works across the millis() wrap.
static void runFor(uint32_t ms, uint32_t stepMs = 100) {
  uint32_t elapsed = 0;
  while (elapsed < ms) {
    const uint32_t step = (ms - elapsed < stepMs) ? (ms - elapsed) : stepMs;
    g_millis += step;
    elapsed += step;
    backlightUpdate();
  }
}

// --- Cases ------------------------------------------------------------------

static void testInitAndIdle() {
  printf("test: init, and nothing happens for the first three minutes\n");
  bootPanel();

  check(g_pin == 38, "GPIO 38 is the backlight");
  check(g_pinModeSeen == OUTPUT, "pin is an output");
  check(g_pinLevel == HIGH, "pin driven high before the PWM takes over");
  check(g_ledcPin == 38, "GPIO 38 attached to the LEDC channel");
  check(g_ledcChannel == 0, "channel 0 (unused elsewhere in the firmware)");
  check(g_ledcFreq == 1000, "1 kHz");
  check(g_ledcBits == 10, "10 bits");
  check(duty() == kFull, "full brightness right after init");
  check(backlightPercent() == 100, "100 % right after init");

  runFor(179000);
  check(duty() == kFull, "still full one second before the dimming");
  check(backlightPercent() == 100, "still 100 %");

  // At the three-minute mark the light starts to come down, and it takes its
  // 1.5 s - it must not jump.
  runFor(1100);
  check(duty() < kFull, "dimming has started at 3 min");
  check(duty() > 0, "the light is not off yet");
  runFor(300);
  const uint32_t midway = duty();
  check(midway < kFull && midway > kDim, "between full and dim on the way down");
  runFor(4000);
  check(duty() == kDim, "dim level reached after the fade");
  check(backlightPercent() == 30, "30 % after the fade");

  runFor(114600);
  check(duty() == kDim, "still dimmed just before the five minutes");
  runFor(1100);
  check(duty() < kDim, "switching off at 5 min");
  check(duty() > 0, "not off in the same instant - it fades");
  runFor(5000);
  check(duty() == 0, "off after the fade");
  check(backlightPercent() == 0, "0 %");

  // Nothing further should be written: the panel is quiet now.
  const int writes = g_writes;
  runFor(60000);
  check(g_writes == writes, "no LEDC writes while the panel is off");
}

static void testTouchWakesAndRestartsTimers() {
  printf("test: a touch wakes the panel and restarts both timers\n");
  bootPanel();
  runFor(5 * 60 * 1000 + 2000); // off
  check(duty() == 0, "off before the touch");

  g_millis += 10;
  backlightActivity();
  backlightUpdate();
  runFor(100);
  check(duty() > 0, "light comes on one LVGL cycle after the press");
  check(duty() < kFull, "and does not jump straight to full");
  runFor(150);
  check(duty() < kFull && duty() > kDim, "still on its way up after 260 ms");
  runFor(200);
  check(duty() == kFull, "full brightness after the 400 ms wake ramp");
  check(backlightPercent() == 100, "100 % again");

  // Both timers now run from the touch, not from the dimming at 3 min.
  runFor(170000);
  check(duty() == kFull, "still full 2:50 after the touch");
  runFor(15000);
  check(duty() < kFull, "dimming starts 3 min after the touch, not before");
  runFor(140000);
  check(duty() == 0, "off 5 min after the touch");
}

static void testTouchDuringFade() {
  printf("test: a touch in the middle of a fade goes up from there\n");
  bootPanel();
  runFor(3 * 60 * 1000);
  runFor(700); // half way down to 30 %
  const uint32_t before = duty();
  check(before < kFull && before > kDim, "mid-fade");

  g_millis += 10;
  backlightActivity();
  backlightUpdate();
  check(duty() >= before, "the fade up starts where the light actually is");
  runFor(600);
  check(duty() == kFull, "full again, without a dip through dimmed");
}

static void testMillisWrap() {
  printf("test: millis() wrap after 49 days stays harmless\n");
  bootPanel();
  g_millis = 0xFFFFF000u - 20000; // ~20 s before the wrap
  backlightActivity();            // touched there, so the timers start there
  // Two minutes across the wrap; the idle time must count 120 s, not ~6 weeks.
  runFor(120000);
  check(duty() == kFull, "no dimming across the wrap");

  // ... and the panel does still dim and switch off when it is due.
  runFor(3 * 60 * 1000 - 120000 + 2000);
  check(duty() == kDim, "dims after the wrap, as it should");
  runFor(2 * 60 * 1000 + 2000);
  check(duty() == 0, "and switches off after the wrap");
}

static void testWithoutTouchController() {
  printf("test: without a touch controller the light stays on\n");
  bootPanel();
  backlightSetWakeable(false);
  runFor(30 * 60 * 1000);
  check(duty() == kFull, "still full after half an hour");
  check(backlightPercent() == 100, "still 100 %");
}

static void testLedcSetupFails() {
  printf("test: if the PWM cannot be set up, the light stays on\n");
  g_millis = 0;
  g_pinLevel = -1;
  g_ledcDuty = 0xFFFFFFFF;
  g_ledcFail = 1;
  backlightInit();
  check(g_pinLevel == HIGH, "pin is high - the panel is visible");
  check(g_ledcDuty == 0xFFFFFFFF, "nothing was written to the LEDC");
  backlightSetWakeable(true);

  // No crash, no writes, whatever happens afterwards.
  runFor(20 * 60 * 1000);
  backlightActivity();
  backlightUpdate();
  check(g_ledcDuty == 0xFFFFFFFF, "still nothing written to the LEDC");
  check(g_pinLevel == HIGH, "pin still high");
}

int main() {
  printf("== backlight state machine (host test) ==\n");
  testInitAndIdle();
  testTouchWakesAndRestartsTimers();
  testTouchDuringFade();
  testMillisWrap();
  testWithoutTouchController();
  testLedcSetupFails();
  printf("== %d checks, %d failed ==\n", g_checks, g_fail);
  return g_fail == 0 ? 0 : 1;
}