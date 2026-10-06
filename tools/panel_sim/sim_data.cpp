// Values for the simulator to show, until it has some of its own.
//
// Taken from one real capture of the panel's own /api/energie.json, so the
// magnitudes are the ones the hardware produces: a house drawing 200 W, a battery
// at 33 % and a grid that is barely doing anything. Numbers a reader can check
// against a real installation are worth more than numbers that were chosen to
// look tidy.
//
// A capture from another installation is the same kind of file: replace the
// values, keep the keys.
//
// SPDX-License-Identifier: MIT
#include "sim_data.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "../../src/device/DeviceState.h"
#include "../../src/device/Json.h"

using json::getNumber;
using json::getString;
using json::has;

namespace {

std::string g_json;
std::string g_pfad;
DeviceState g_state;

// How many keys the file carries, counted by '{' rather than by parsing it: the
// number only goes into the startup line, and a counter that cannot fail is worth
// more here than a parser that can.
size_t anzahlSchluessel(const std::string &j) {
  size_t n = 0;
  for (char c : j) {
    if (c == '{') n++;
  }
  return n;
}

void zahl(const char *key, float *ziel) {
  double w = 0.0;
  if (getNumber(g_json.data(), g_json.size(), key, &w)) {
    *ziel = (float)w;
  }
}

void text(const char *key, char *ziel, size_t cap) {
  getString(g_json.data(), g_json.size(), key, ziel, cap);
}

}  // namespace

bool simDataLoad(const char *pfad) {
  g_pfad = (pfad != nullptr) ? pfad : "";
  FILE *f = fopen(g_pfad.c_str(), "rb");
  if (f == nullptr) {
    fprintf(stderr, "Datendatei nicht lesbar: %s\n", g_pfad.c_str());
    return false;
  }
  std::string s;
  char puffer[4096];
  size_t n;
  while ((n = fread(puffer, 1, sizeof(puffer), f)) > 0) {
    s.append(puffer, n);
  }
  fclose(f);
  g_json = s;

  memset(&g_state, 0, sizeof(g_state));
  g_state.haveData = true;
  g_state.haveBattery = true;
  g_state.connected = true;
  g_state.islandKnown = true;

  // Grid
  zahl("gridExchangeW", &g_state.gridExchangeW);
  for (int i = 0; i < 3; i++) {
    std::string k = "gridV" + std::to_string(i);
    zahl(k.c_str(), &g_state.gridV[i]);
    std::string hz = "gridHz" + std::to_string(i);
    zahl(hz.c_str(), &g_state.gridHz[i]);
  }

  // Generation, household
  zahl("genW0", &g_state.genW[0]);
  zahl("genW1", &g_state.genW[1]);
  zahl("extW", &g_state.extW);
  for (int i = 0; i < 3; i++) {
    std::string k = "houseW" + std::to_string(i);
    zahl(k.c_str(), &g_state.houseW[i]);
  }

  // Battery
  zahl("socPct", &g_state.socPct);
  zahl("batW", &g_state.batW);
  zahl("batV", &g_state.batV);
  zahl("batA", &g_state.batA);
  zahl("batteryTemp", &g_state.batteryTemp);
  zahl("batteryCycles", &g_state.batteryCycles);
  zahl("batterySoh", &g_state.batterySoh);

  // Day counters, in Wh
  zahl("dayGenWh", &g_state.dayGenWh);
  zahl("dayHouseWh", &g_state.dayHouseWh);
  zahl("dayFeedInWh", &g_state.dayFeedInWh);
  zahl("dayGridDrawWh", &g_state.dayGridDrawWh);
  zahl("dayExtWh", &g_state.dayExtWh);

  // Month, year, lifetime
  zahl("monthGenWh", &g_state.monthGenWh);
  zahl("yearGenWh", &g_state.yearGenWh);
  zahl("totalGenWh", &g_state.totalGenWh);
  zahl("monthHouseWh", &g_state.monthHouseWh);
  zahl("yearHouseWh", &g_state.yearHouseWh);
  zahl("totalHouseWh", &g_state.totalHouseWh);
  zahl("monthFeedInWh", &g_state.monthFeedInWh);
  zahl("yearFeedInWh", &g_state.yearFeedInWh);
  zahl("feedInTotalWh", &g_state.feedInTotalWh);
  zahl("monthGridDrawWh", &g_state.monthGridDrawWh);
  zahl("yearGridDrawWh", &g_state.yearGridDrawWh);
  zahl("gridDrawTotalWh", &g_state.gridDrawTotalWh);
  zahl("monthExtWh", &g_state.monthExtWh);
  zahl("yearExtWh", &g_state.yearExtWh);
  zahl("totalExtWh", &g_state.totalExtWh);

  // Device
  text("deviceName", g_state.deviceName, sizeof(g_state.deviceName));
  text("firmwareVersion", g_state.firmwareVersion,
       sizeof(g_state.firmwareVersion));
  zahl("coreTemp", &g_state.coreTemp);
  zahl("heatSinkTemp", &g_state.heatSinkTemp);

  // The RCT's own reading of its meters, which Rules.h turns into the four
  // quantities the panel shows. Set once, exactly as a driver would.
  g_state.semantics.loadMeterSeesExternal = false;
  g_state.semantics.genCounterSeesExternal = false;
  g_state.semantics.feedCounterNegative = true;

  printf("Daten: %s (%zu Bytes, %zu Objekte)\n", g_pfad.c_str(), g_json.size(),
         anzahlSchluessel(g_json));
  return true;
}

void simDataPoll() {
  // The published state's timestamp is what the panel's staleness rule reads, and
  // it compares against millis(). Zero would make every value look an hour old, so
  // the simulator stamps it now - the JSON file carries values, not ages.
  extern uint32_t millis();
  g_state.lastUpdateMs = millis();
}

const char *simDatenQuelle() { return g_pfad.c_str(); }

size_t simDatenBytes() { return g_json.size(); }

DeviceState &simDataState() { return g_state; }