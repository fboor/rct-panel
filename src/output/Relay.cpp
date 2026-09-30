// Switched output (relay port) - see Relay.h for the design.
//
// SPDX-License-Identifier: MIT
#include "Relay.h"

#include "../rct/RctTypes.h"
#include "RelayPins.h"
#include <Preferences.h>

// --- Timing -----------------------------------------------------------------

// How long the rule must hold before the output closes.
static const uint32_t ON_DELAY_MS = 20000;
// How long it stays closed after switching on, whatever the rule says.
static const uint32_t MIN_HOLD_MS = 60000;
// Hysteresis band as a fraction of the threshold: the output switches off when
// the value falls below (threshold - band), and only above that it counts as
// "still over the threshold".
static const int BAND_PERMILLE = 200;
// 10 s is the poll interval; below that there is nothing new to read.
static const uint32_t EVAL_MS = 1000;
// No fresh data for this long: the state is unknown, and unknown switches off.
static const uint32_t DATA_MAX_AGE_MS = 120000;

// --- Test sequence (5 s on / 5 s off, twice) --------------------------------

static const uint32_t TEST_PHASE_MS = 5000;
static const int TEST_PHASES = 4; // on, off, on, off

// --- State ------------------------------------------------------------------

static Preferences prefs;
static RelayMode s_mode = RelayMode::Off;
static int s_threshold = 500; // W
static bool s_on = false;
static uint32_t s_wantSinceMs = 0;   // when the rule first asked for "on"
static uint32_t s_switchedOnMs = 0;  // when the output actually closed
static uint32_t s_lastEvalMs = 0;
static int s_testPhase = -1;   // -1 = no test running
static uint32_t s_testStartMs = 0;

void relayWrite(bool on) {
  digitalWrite(RELAY_PIN, on ? RELAY_LEVEL_ON : RELAY_LEVEL_OFF);
  if (on != s_on) {
    s_on = on;
    Serial.printf("Relais GPIO %d: %s\n", (int)RELAY_PIN, on ? "EIN" : "AUS");
  }
}

// --- Configuration ----------------------------------------------------------

void relayLoadConfig() {
  prefs.begin("relay", false);
  const uint8_t m = prefs.getUChar("mode", (uint8_t)RelayMode::Off);
  s_mode = (m < kRelayModeCount) ? (RelayMode)m : RelayMode::Off;
  s_threshold = prefs.getInt("thresh", 500);
  prefs.end();
  if (s_threshold < 0 || s_threshold > 5000) {
    s_threshold = 500;
  }
}

void relaySaveConfig() {
  prefs.begin("relay", false);
  prefs.putUChar("mode", (uint8_t)s_mode);
  prefs.putInt("thresh", s_threshold);
  prefs.end();
}

RelayMode relayMode() { return s_mode; }

void relaySetMode(RelayMode mode) {
  if ((int)mode < 0 || (int)mode >= kRelayModeCount) {
    return;
  }
  if (mode == s_mode) {
    return;
  }
  s_mode = mode;
  relaySaveConfig();
  // ASCII in the log, umlauts in relayModeName() (see there).
  static const char *const kLogName[kRelayModeCount] = {
      "Aus", "Netzbezug", "Ueberschuss", "Stoerung", "Inselbetrieb"};
  Serial.printf("Relais: Funktion jetzt '%s'\n", kLogName[(int)mode]);
  // A new function starts from scratch: neither the pending on-delay nor the
  // minimum hold of the old one says anything about the new one.
  s_wantSinceMs = 0;
  s_switchedOnMs = 0;
}

const char *relayModeName(RelayMode mode) {
  // With umlauts: these strings are what the display shows. The serial lines
  // below stay ASCII, because a log is read in terminals that do not all
  // handle UTF-8.
  switch (mode) {
  case RelayMode::Off: return "Aus";
  case RelayMode::GridDraw: return "Netzbezug";
  case RelayMode::PvSurplus: return "Überschuss";
  case RelayMode::Fault: return "Störung";
  case RelayMode::Island: return "Inselbetrieb";
  default: return "?";
  }
}

void relayCycleMode() {
  relaySetMode((RelayMode)(((int)s_mode + 1) % kRelayModeCount));
}

int relayThreshold() { return s_threshold; }

void relaySetThreshold(int watts) {
  if (watts < 0) {
    watts = 0;
  }
  if (watts > 5000) {
    watts = 5000;
  }
  if (watts == s_threshold) {
    return;
  }
  s_threshold = watts;
  relaySaveConfig();
}

bool relayIsOn() { return s_on; }

// --- The rule ---------------------------------------------------------------

// PV surplus: what is generated minus what the house uses.
//
// The S0 meter sits on the consumption side only, and that asymmetry is the
// whole point. The inverter's load meter does not see external generation
// (loadPower is already "house consumption minus S0 feed-in"), so the house is
// loadPower + s0Power. Putting s0Power on the *generation* side as well would
// cancel it out and call someone else's solar our surplus - with a 2 kW S0
// plant and a 1 kW house the panel would then switch a load on while we have
// none of our own. Own generation, own surplus: PV A+B minus the house.
static float pvSurplusW() {
  const float house = rctState.loadPower[0] + rctState.loadPower[1] +
                      rctState.loadPower[2] + rctState.s0Power;
  return rctState.pvPower[0] + rctState.pvPower[1] - house;
}

static bool faultActive() {
  return (rctState.faultBits[0] | rctState.faultBits[1] | rctState.faultBits[2] |
          rctState.faultBits[3]) != 0u;
}

float relayTriggerValue() {
  switch (s_mode) {
  case RelayMode::GridDraw: return rctState.gridPowerSum; // + = import
  case RelayMode::PvSurplus: return pvSurplusW();
  default: return 0.0f;
  }
}

// One threshold mode, with the band. `curOn` is returned unchanged inside the
// band, which is what makes it a hysteresis and not a threshold.
static bool thresholdWants(bool curOn, float value) {
  const float t = (float)s_threshold;
  const float band = t * (float)BAND_PERMILLE / 1000.0f;
  if (value >= t) {
    return true;
  }
  if (value <= t - band) {
    return false;
  }
  return curOn;
}

static bool ruleWantsOn() {
  if (s_mode == RelayMode::Off) {
    return false;
  }
  // Unknown state switches off. Without this, a threshold that was crossed
  // before the link died would keep the output closed indefinitely.
  if (!rctState.haveData ||
      (int32_t)(millis() - rctState.lastUpdateMs) > (int32_t)DATA_MAX_AGE_MS) {
    return false;
  }
  switch (s_mode) {
  case RelayMode::GridDraw: return thresholdWants(s_on, rctState.gridPowerSum);
  case RelayMode::PvSurplus: return thresholdWants(s_on, pvSurplusW());
  case RelayMode::Fault: return faultActive();
  case RelayMode::Island: return rctState.islandKnown && rctState.islandMode;
  default: return false;
  }
}

// --- Lifecycle --------------------------------------------------------------

void relayInit() {
  // Before the pin becomes an output, hold the off level with an internal
  // pull: a pin that is briefly an input must not float to the level that
  // closes the relay. Then drive it. That is why relayInit() is the first call
  // in setup() after the diagnostics.
  pinMode(RELAY_PIN,
          RELAY_LEVEL_OFF == HIGH ? INPUT_PULLUP : INPUT_PULLDOWN);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_LEVEL_OFF);
  // All timing starts here, not at whatever the statics happened to hold. A
  // stale s_lastEvalMs leaves the eval gate closed until millis() catches up
  // with it - after a re-init that means minutes in which no rule runs at all.
  s_on = false;
  s_wantSinceMs = 0;
  s_switchedOnMs = 0;
  s_lastEvalMs = millis();
  s_testPhase = -1;
  s_testStartMs = 0;
  relayLoadConfig();
  static const char *const kLogName[kRelayModeCount] = {
      "Aus", "Netzbezug", "Ueberschuss", "Stoerung", "Inselbetrieb"};
  Serial.printf("Relais GPIO %d: Funktion '%s', Schwelle %d W, Aus bei Start\n",
                (int)RELAY_PIN, kLogName[(int)s_mode], s_threshold);
}

void relayUpdate() {
  const uint32_t now = millis();

  if (s_testPhase >= 0) {
    // The test owns the output: it is the answer to "which pin, which
    // polarity", and it has to be visible even though the rule disagrees.
    if ((int32_t)(now - s_testStartMs) >= (int32_t)TEST_PHASE_MS) {
      s_testStartMs = now;
      s_testPhase++;
      if (s_testPhase >= TEST_PHASES) {
        s_testPhase = -1;
        Serial.println(F("Relais: Test beendet"));
        s_wantSinceMs = 0;   // the rule starts over from here
        s_switchedOnMs = 0;
      } else {
        relayWrite((s_testPhase % 2) == 0);
      }
    }
    return;
  }

  if ((int32_t)(now - s_lastEvalMs) < (int32_t)EVAL_MS) {
    return;
  }
  s_lastEvalMs = now;

  const bool want = ruleWantsOn();
  if (want) {
    if (s_wantSinceMs == 0) {
      s_wantSinceMs = now == 0 ? 1 : now; // 0 is "none"; millis()==0 is once
    }
    const uint32_t since = now - s_wantSinceMs;
    if (!s_on && since >= ON_DELAY_MS) {
      relayWrite(true);
      s_switchedOnMs = now;
    }
    return;
  }

  s_wantSinceMs = 0;
  if (s_on && (int32_t)(now - s_switchedOnMs) >= (int32_t)MIN_HOLD_MS) {
    relayWrite(false);
  }
}

bool relayStartTest() {
  if (s_testPhase >= 0) {
    return false;
  }
  s_testPhase = 0;
  s_testStartMs = millis();
  relayWrite(true);
  Serial.println(F("Relais: Test laeuft (5 s an / 5 s aus, zweimal)"));
  return true;
}

bool relayTestRunning() { return s_testPhase >= 0; }
