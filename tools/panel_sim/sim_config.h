// What the simulator was last set to, kept next to it.
//
// On a panel this is the NVS: the settings page saves, the next boot reads. A build
// machine has no NVS, so it had nothing - the developer who always tests against a
// real inverter had to type the address again on every start, and the emulator came
// up on the mock every time because there was nothing to say otherwise.
//
// The file is NOT in the repository. It holds a device address, which is the same
// kind of thing the commit-message rule keeps out of the history: one developer's
// inverter is not the project's business. .gitignore has it, and deleting it puts
// the simulator back to its default, which is the mock.
//
// Precedence, and it is the only sensible order:
//   1. the command line   --device/--host/--port/--data/--size/--lang
//   2. this file
//   3. the default        SIM, the mock file, 480 x 480
//
// SPDX-License-Identifier: MIT
#ifndef RCT_PANEL_SIM_CONFIG_H
#define RCT_PANEL_SIM_CONFIG_H

#include <string>

struct SimConfig {
  std::string deviceType = "SIM";   // the default is the MOCK, not a device
  std::string deviceHost;
  std::string devicePort;
  bool themeHell = false;
  std::string dataFile = "data/rct_mock.json";
  std::string size = "480x480";
};

// Read the file. False when it does not exist or cannot be read - and the caller
// then uses the defaults, which is not an error: a fresh checkout has no file.
bool simConfigLoad(SimConfig *cfg);

// Write it. The settings page calls this after every save in a simulator build;
// on a panel the same save goes to NVS and this is not compiled.
bool simConfigSave(const SimConfig &cfg);

// Where the file is, so the README and the log can name it.
std::string simConfigPfad();

#endif // RCT_PANEL_SIM_CONFIG_H