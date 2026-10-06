// The values the simulator shows, out of a JSON file.
//
// A file and not a fixed struct in the code, because the moment a picture has to
// carry real numbers the mock has to be replaceable by a capture without touching
// anything else. The format is flat keys, which is what src/device/Json.h already
// reads and what the inverter actually sends - so a file captured from a real
// panel is the same kind of file.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_PANEL_SIM_DATA_H
#define RCT_PANEL_SIM_DATA_H

#include <cstddef>
#include <cstdint>

// Read the file and publish it as the device state. False if it cannot be read
// or has no state object in it, and it says which.
bool simDataLoad(const char *pfad);

// The state object itself, filled once per simPoll(). deviceStateMutable() in the
// simulator's stubs hands out this one, so the GUI reads exactly the struct the
// driver would have written.
void simDataPoll();
struct DeviceState;
DeviceState &simDataState();

const char *simDatenQuelle();
size_t simDatenBytes();

#endif // RCT_PANEL_SIM_DATA_H