// Switched output (relay port) of the panel.
//
// One output, one selectable function. The panel decides when to switch; the
// function and its threshold live in NVS and are changed either on the Service
// page (tap, for the common case) or in the web interface (a proper select,
// behind the code).
//
// Design notes worth keeping:
// - Off at boot, and off until the rule has been satisfied for ON_DELAY_MS. A
//   relay that snaps on during startup is a shock and, for a heating load, a
//   surprise bill; the default mode is Off anyway.
// - Symmetric hysteresis: ON_DELAY_MS before switching on, MIN_HOLD_MS before
//   switching off. Both are doubled from the values first chosen (10 s / 30 s):
//   clouds passing in front of the PV generator, and a house that draws and
//   releases in bursts, both produce crossings that a short window would
//   chatter on. A mechanical relay that switches every few seconds is a defect
//   report, not a feature.
// - The switch-off threshold is the switch-on threshold minus a 20 % band, so a
//   value sitting exactly on the threshold does not toggle the output once per
//   poll.
// - No fresh data means the state is unknown, and unknown switches off. A
//   latched-on output would keep running whatever the inverter was doing when
//   the link died.
// - The panel writes the pin before anything else in setup(), so the output is
//   at its off level before the display is even initialised.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_OUTPUT_RELAY_H
#define RCT_OUTPUT_RELAY_H

#include <Arduino.h>

// What the output follows. The order is what the Service page cycles through
// and what the numbers in the setup portal (0..4) mean.
enum class RelayMode : uint8_t {
  Off = 0,       // never switches
  GridDraw = 1,  // grid import above the threshold (night setback, storage heater)
  PvSurplus = 2, // PV surplus above the threshold (use what is generated)
  Fault = 3,     // inverter reports a fault
  Island = 4,    // island operation (grid separated)
};

static const int kRelayModeCount = 5;

void relayInit();

// Read the configuration from NVS. Called by relayInit(); only needed
// separately if something wants to reload without touching the pin.
void relayLoadConfig();
void relaySaveConfig();

// Evaluate the rule and apply it. Call from loop(); internally it runs at 1 Hz,
// which is finer than the 10 s poll of the values it reads.
void relayUpdate();

// The function the output follows, and its name for the display.
RelayMode relayMode();
void relaySetMode(RelayMode mode);
const char *relayModeName(RelayMode mode);
// Service page tap: Off -> GridDraw -> PvSurplus -> Fault -> Island -> Off.
void relayCycleMode();

// Switch-on threshold in W, used by the two threshold modes only. Clamped to
// 0..5000.
int relayThreshold();
void relaySetThreshold(int watts);

// True while the output is switched on - by the rule or by the test.
bool relayIsOn();

// The value the current mode watches, in W (grid import, PV surplus, or 0 for
// the modes that have no threshold). The Service page shows it, because a
// threshold is only useful next to the number it is compared against.
float relayTriggerValue();

// 5 s on / 5 s off, twice, regardless of the rule - the check for "is this the
// right pin, and is the polarity in RelayPins.h right". Returns false if a test
// is already running.
bool relayStartTest();
bool relayTestRunning();

#endif // RCT_OUTPUT_RELAY_H
