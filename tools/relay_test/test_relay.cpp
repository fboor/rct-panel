// Host test for the switched output: src/output/Relay.cpp is included as-is,
// with Arduino and Preferences replaced by the stubs next to this file.
//
// The point is the parts that cannot be checked on the bench: the timing
// (20 s on-delay, 60 s minimum hold), the hysteresis band, the "no data means
// off" rule, the test sequence, and the persistence round-trip. On hardware
// these need a live inverter and a stopwatch; here they need a loop.
//
// Build and run:
//   g++ -std=c++17 -Itools/relay_test/stubs -Isrc -o /tmp/relay_test
//       tools/relay_test/test_relay.cpp && /tmp/relay_test

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "Arduino.h"
#include "Preferences.h"
#include "rct/RctTypes.h"

uint32_t g_millis = 0;
StubSerial Serial;

uint8_t Preferences::kChar = 0;
bool Preferences::kCharSet = false;
int Preferences::kInt = 0;
bool Preferences::kIntSet = false;

RctSnapshot rctState;

// Pin state, so the test can see what the module would do.
int g_pinLevel = -1;
int g_pinModeSeen = -1;
int g_pin = -1;

void pinMode(int p, int mode) {
  g_pin = p;
  g_pinModeSeen = mode;
}
void digitalWrite(int p, int level) { g_pinLevel = level; }

void StubSerial::printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

// The module under test.
#include "output/Relay.cpp"

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

// Reset the world between cases: no stored config, no data, output off.
static void resetWorld() {
  g_millis = 1000;
  g_pinLevel = -1;
  g_pinModeSeen = -1;
  Preferences::kCharSet = false;
  Preferences::kIntSet = false;
  memset(&rctState, 0, sizeof(rctState));
  relayInit();
}

// Feed fresh values: the "device" is reachable and the given powers apply.
static void feed(float gridW, float pvA, float pvB, float load, float s0) {
  rctState.haveData = true;
  rctState.lastUpdateMs = g_millis;
  rctState.gridPowerSum = gridW;
  rctState.pvPower[0] = pvA;
  rctState.pvPower[1] = pvB;
  rctState.loadPower[0] = load;
  rctState.loadPower[1] = 0;
  rctState.loadPower[2] = 0;
  rctState.s0Power = s0;
}

// Time passes in 100 ms steps, as the real loop() would - the module evaluates
// at 1 Hz internally, so a coarser step would only blur the boundaries the test
// is checking. `live` says whether the device keeps answering: false is the
// "link died" case, where lastUpdateMs must not move.
static void step(uint32_t ms, bool live = true) {
  const uint32_t until = g_millis + ms;
  while (g_millis < until) {
    g_millis += 100;
    if (live) {
      rctState.lastUpdateMs = g_millis;
    }
    relayUpdate();
  }
}

static void runFor(uint32_t ms) { step(ms, true); }

static bool on() { return relayIsOn(); }

// The pin is driven at the level that means "on" for the configured polarity.
static int onLevel() { return RELAY_LEVEL_ON; }
static int offLevel() { return RELAY_LEVEL_OFF; }

// ---------------------------------------------------------------------------
// Cases
// ---------------------------------------------------------------------------

// Boot: the pin is driven to the off level, and the mode is the stored one.
static void testBootState() {
  printf("test: boot state\n");
  Preferences::kChar = (uint8_t)RelayMode::PvSurplus;
  Preferences::kCharSet = true;
  Preferences::kInt = 750;
  Preferences::kIntSet = true;

  g_millis = 1000;
  g_pinLevel = -1;
  memset(&rctState, 0, sizeof(rctState));
  relayInit();

  check(g_pinModeSeen == OUTPUT, "pin became an output");
  check(g_pinLevel == offLevel(), "pin driven to the off level");
  check(!relayIsOn(), "output off after init");
  check(relayMode() == RelayMode::PvSurplus, "mode read from NVS");
  check(relayThreshold() == 750, "threshold read from NVS");

  // The internal pull is set before the pin is an output, so a briefly floating
  // pin is not the level that closes the relay.
  // (relayInit does both; re-run to observe the intermediate state.)
  g_pinModeSeen = -1;
  g_pinLevel = -1;
  g_millis = 2000;
  // Nothing to observe here - pinMode is called once for the pull and once for
  // OUTPUT, and only the last is visible. Assert the invariant instead: the
  // off level is the level the pull must hold.
#if RELAY_ACTIVE_LOW
  check(offLevel() == HIGH, "active low: off level is HIGH, so the pull-up is right");
#else
  check(offLevel() == LOW, "active high: off level is LOW, so the pull-down is right");
#endif
}

// Off is the default and never switches, whatever the values say.
static void testModeOff() {
  printf("test: mode Off\n");
  resetWorld();
  check(relayMode() == RelayMode::Off, "default is Off");
  feed(3000, 0, 0, 100, 0); // big grid draw
  runFor(10 * 60 * 1000);
  check(!on(), "stays off with a large grid draw");
  check(g_pinLevel == offLevel(), "pin at the off level");
}

// Grid draw: 20 s over the threshold switches on, the minimum hold keeps it on.
static void testGridDraw() {
  printf("test: grid draw, on-delay and minimum hold\n");
  resetWorld();
  relaySetMode(RelayMode::GridDraw);
  relaySetThreshold(500);

  feed(300, 0, 0, 100, 0); // under the threshold
  runFor(60 * 1000);
  check(!on(), "stays off below the threshold");

  // Over the threshold, but only for 10 s.
  feed(900, 0, 0, 100, 0);
  runFor(10 * 1000);
  check(!on(), "does not switch after 10 s over the threshold");
  check(g_pinLevel == offLevel(), "pin still off");

  // 20 s in: on.
  runFor(10 * 1000);
  check(!on(), "still off at 19 s");
  runFor(2 * 1000);
  check(on(), "switches on after 20 s over the threshold");
  check(g_pinLevel == onLevel(), "pin at the on level");

  // Immediately under the threshold: the minimum hold wins.
  feed(100, 0, 0, 100, 0);
  runFor(30 * 1000);
  check(on(), "stays on during the 60 s minimum hold");
  check(g_pinLevel == onLevel(), "pin still at the on level");

  // After 60 s it may go off, and it must - the value is well under the band.
  runFor(31 * 1000);
  check(!on(), "switches off after the minimum hold");
  check(g_pinLevel == offLevel(), "pin back at the off level");
}

// The band: inside it the state is kept, so a value sitting on the threshold
// does not toggle every poll.
static void testHysteresisBand() {
  printf("test: hysteresis band\n");
  resetWorld();
  relaySetMode(RelayMode::GridDraw);
  relaySetThreshold(500); // band = 100 W -> off below 400

  feed(700, 0, 0, 100, 0);
  runFor(25 * 1000);
  check(on(), "on above the threshold");

  // 450 W: above the band, below the threshold. Must stay on.
  feed(450, 0, 0, 100, 0);
  runFor(5 * 60 * 1000);
  check(on(), "stays on inside the band");

  // 390 W: below the band. Must go off.
  feed(390, 0, 0, 100, 0);
  runFor(2 * 60 * 1000);
  check(!on(), "off below the band");
}

// PV surplus: own generation minus the house, with S0 on the consumption side
// only.
static void testPvSurplus() {
  printf("test: PV surplus\n");
  resetWorld();
  relaySetMode(RelayMode::PvSurplus);
  relaySetThreshold(500);

  // 3000 W PV, house 1000 W -> surplus 2000.
  feed(-2000, 1500, 1500, 1000, 0);
  check(lroundf(relayTriggerValue()) == 2000, "surplus 2000 W");
  runFor(25 * 1000);
  check(on(), "switches on at 2000 W surplus");

  // Now the S0 meter delivers 2000 W into a 1000 W house. Own generation is
  // unchanged, so the surplus must be unchanged - and the relay must not be
  // switched on by somebody else's solar.
  relaySetMode(RelayMode::Off);
  runFor(2 * 60 * 1000);
  check(!on(), "off again after mode change");
  feed(-2000, 1500, 1500, 1000, 2000); // house total 3000 W
  check(lroundf(relayTriggerValue()) == 0, "S0 counts as consumption, surplus 0");
  relaySetMode(RelayMode::PvSurplus);
  runFor(30 * 1000);
  check(!on(), "no switch with a 0 W surplus");

  // And down to a negative surplus.
  feed(2000, 500, 500, 2000, 0);
  runFor(30 * 1000);
  check(!on(), "no switch with a negative surplus");
}

// No data means off - including when the output is already on.
static void testDataLoss() {
  printf("test: no data switches off\n");
  resetWorld();
  relaySetMode(RelayMode::GridDraw);
  relaySetThreshold(500);
  feed(900, 0, 0, 100, 0);
  runFor(25 * 1000);
  check(on(), "on while data arrives");

  // The link dies: lastUpdateMs stays where it was, no new values. The device
  // in question pauses for several minutes as a matter of course, so the rule
  // has to survive that - with the old two minutes it switched off during every
  // pause and back on with the next frame.
  step(2 * 60 * 1000 + 1000, false);
  check(on(), "still on after two minutes: a pause is not a failure");
  step(8 * 60 * 1000, false); // ten minutes in total
  check(!on(), "off after ten minutes without data");
  check(g_pinLevel == offLevel(), "pin back at the off level");

  // Coming back with the same values re-arms the delay rather than switching
  // straight on.
  feed(900, 0, 0, 100, 0);
  runFor(10 * 1000);
  check(!on(), "does not switch immediately after data returns");
  runFor(15 * 1000);
  check(on(), "switches on again after the delay");
}

// Faults and island mode need no threshold, but they do need fresh data.
static void testFaultAndIsland() {
  printf("test: fault and island modes\n");
  resetWorld();
  relaySetMode(RelayMode::Fault);

  rctState.faultBits[1] = 1u << 5;
  feed(0, 0, 0, 100, 0);
  runFor(25 * 1000);
  check(on(), "fault switches the output on");

  rctState.faultBits[1] = 0;
  runFor(2 * 60 * 1000);
  check(!on(), "cleared fault switches it off");

  // An island flag that has not answered yet must not read as island.
  rctState.islandMode = true;
  rctState.islandKnown = false;
  runFor(30 * 1000);
  check(!on(), "unknown island flag does not switch on");

  relaySetMode(RelayMode::Island);
  rctState.islandKnown = true;
  runFor(25 * 1000);
  check(on(), "known island flag switches on");

  relaySetMode(RelayMode::Island);
  rctState.islandMode = false;
  runFor(2 * 60 * 1000);
  check(!on(), "grid back switches it off");
}

// The test sequence: 5 s on, 5 s off, twice - even with the rule on.
static void testSequence() {
  printf("test: test sequence\n");
  resetWorld();
  relaySetMode(RelayMode::GridDraw);
  relaySetThreshold(500);
  feed(900, 0, 0, 100, 0);
  runFor(25 * 1000);
  check(on(), "rule switched it on");

  check(relayStartTest(), "test starts");
  check(relayTestRunning(), "test running");
  check(on(), "test starts with the output on");

  // Second press while running: refused, no second sequence.
  check(!relayStartTest(), "second start refused");

  // The phases are 5 s each, and each new phase starts its own 5 s at the
  // moment the switch happens: on at t, off at t+5, on at t+10, off at t+15,
  // finished at t+20.
  runFor(4 * 1000);
  check(on(), "still on before the first 5 s are up");
  runFor(1 * 1000);
  check(!on(), "off after 5 s");
  runFor(4 * 1000);
  check(!on(), "off through the middle of the off phase");
  runFor(1 * 1000);
  check(on(), "on again after 10 s");
  runFor(4 * 1000);
  check(on(), "on through the middle of the second on phase");
  runFor(1 * 1000);
  check(!on(), "off again after 15 s");
  runFor(4 * 1000);
  check(relayTestRunning(), "still running at 19 s");
  runFor(1 * 1000);
  check(!relayTestRunning(), "test finished after 20 s");
  check(!on(), "off when the test ends");

  // The rule restarts from zero: still wanted, so the 20 s have to pass again.
  runFor(10 * 1000);
  check(!on(), "no immediate switch after the test");
  runFor(15 * 1000);
  check(on(), "rule takes over after the delay");
}

// Cycling the mode, and persistence.
static void testCycleAndPersist() {
  printf("test: mode cycle and persistence\n");
  resetWorld();
  check(relayMode() == RelayMode::Off, "starts at Off");
  relayCycleMode();
  check(relayMode() == RelayMode::GridDraw, "cycle -> GridDraw");
  relayCycleMode();
  check(relayMode() == RelayMode::PvSurplus, "cycle -> PvSurplus");
  relayCycleMode();
  check(relayMode() == RelayMode::Fault, "cycle -> Fault");
  relayCycleMode();
  check(relayMode() == RelayMode::Island, "cycle -> Island");
  relayCycleMode();
  check(relayMode() == RelayMode::Off, "cycle wraps to Off");

  check(Preferences::kCharSet, "mode was written");
  check((RelayMode)Preferences::kChar == RelayMode::Off, "written value is Off");

  // A changed mode restarts the timing.
  relaySetMode(RelayMode::GridDraw);
  relaySetThreshold(500);
  feed(900, 0, 0, 100, 0);
  runFor(19 * 1000);
  check(!on(), "not yet after 19 s");
  relaySetMode(RelayMode::PvSurplus); // surplus is -100 here
  runFor(30 * 1000);
  check(!on(), "mode change to a non-matching rule stays off");

  // Out-of-range input is ignored rather than silently clamped.
  relaySetMode((RelayMode)7);
  check(relayMode() == RelayMode::PvSurplus, "invalid mode ignored");
  relaySetThreshold(99999);
  check(relayThreshold() == 5000, "threshold clamped to 5000");
  relaySetThreshold(-5);
  check(relayThreshold() == 0, "threshold clamped to 0");
  relaySetThreshold(500);
}

// A corrupt NVS value must not produce a mode that does not exist.
static void testCorruptConfig() {
  printf("test: corrupt NVS values\n");
  Preferences::kChar = 200; // no such mode
  Preferences::kCharSet = true;
  Preferences::kInt = 999999;
  Preferences::kIntSet = true;
  g_millis = 1000;
  memset(&rctState, 0, sizeof(rctState));
  relayInit();
  check(relayMode() == RelayMode::Off, "unknown mode falls back to Off");
  check(relayThreshold() == 500, "out-of-range threshold falls back to 500");
  check(!relayIsOn(), "output off");
}

int main() {
  printf("== relay state machine (host test) ==\n");
  testBootState();
  testModeOff();
  testGridDraw();
  testHysteresisBand();
  testPvSurplus();
  testDataLoss();
  testFaultAndIsland();
  testSequence();
  testCycleAndPersist();
  testCorruptConfig();
  printf("== %d checks, %d failed ==\n", g_checks, g_fail);
  return g_fail == 0 ? 0 : 1;
}
