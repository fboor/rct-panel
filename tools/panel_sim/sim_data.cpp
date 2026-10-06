// Where the emulated driver gets its values from.
//
// This file does one thing now: it opens the file and hands the text to the driver,
// which is the piece that knows what the keys mean. It used to fill a DeviceState
// itself, which meant the simulator never ran Rules.h - and Rules.h is where the
// panel's four headline numbers come from, so a mistake in it would have shown up
// on a device and nowhere else.
//
// SPDX-License-Identifier: MIT
#include "sim_data.h"

#include "../../src/device/SimDriver.h"

#include <cstdio>
#include <string>

std::string g_pfad;
size_t g_bytes = 0;

void simDataLoad(const char *pfad) {
  g_pfad = (pfad != nullptr) ? pfad : "";
  g_bytes = 0;
  if (g_pfad.empty()) {
    simDriverSetDataFile("");
    return;
  }
  FILE *f = fopen(g_pfad.c_str(), "rb");
  if (f == nullptr) {
    fprintf(stderr, "Datendatei nicht lesbar: %s\n", g_pfad.c_str());
    simDriverSetDataFile("");
    return;
  }
  fseek(f, 0, SEEK_END);
  g_bytes = (size_t)ftell(f);
  fclose(f);
  printf("Daten: %s (%zu Bytes)\n", g_pfad.c_str(), g_bytes);
  simDriverSetDataFile(g_pfad.c_str());
}

const char *simDatenQuelle() { return g_pfad.c_str(); }

size_t simDatenBytes() { return g_bytes; }