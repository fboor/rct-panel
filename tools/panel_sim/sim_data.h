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

// Hand the file to the emulated driver. It is not read into a state here any more:
// the driver reads it, fills the real DeviceState and the real Rules.h derive the
// four quantities from it, exactly as they do for the RCT.
void simDataLoad(const char *pfad);

const char *simDatenQuelle();
size_t simDatenBytes();

#endif // RCT_PANEL_SIM_DATA_H