// The emulated inverter: a DeviceDriver like the RCT and the OIG, in the
// simulator only.
//
// It exists so that the simulator can stop faking the device layer. Before this,
// sim_stubs.cpp answered deviceState() itself and Rules.h, the factories and the
// whole device abstraction were never executed on the build machine - so a bug in
// any of them would have been a bug that only a panel could find. Now the simulator
// runs the real Device.cpp, the real DeviceFactory and the real Rules.h, and this
// file is the only thing that is not shipped.
//
// WHAT IT PRODUCES, and why it is not just the JSON file again:
//
// The powers follow a day. A panel that shows the same three numbers for ever is a
// panel nobody can lay out against - the interesting question at 0,6 m is whether
// four digits, a decimal point and a unit fit, and that question does not get asked
// by a value that never changes. So the file supplies the facts that do not change
// (the counters, the device name, the version) and the driver supplies the powers,
// shaped by the clock.
//
// DETERMINISTIC ON PURPOSE. Everything is a function of the panel's clock, which the
// simulator pins before a screenshot. Two runs of the same build therefore produce
// the same picture, which is the only reason a before-and-after comparison of a
// refactor means anything.
//
// SPDX-License-Identifier: MIT
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "i18n/Lang.h"

#include "../../src/device/Device.h"
#include "../../src/device/Json.h"
#include "../../src/device/SimDriver.h"

#include "sim_data.h"
#include "sim_stubs.h"

using json::getNumber;
using json::getString;

namespace {

std::string g_pfad;
std::string g_json;
uint32_t g_nowMs = 0;

// The hour of the day the picture shows. Not the wall clock: the day starts at six
// in the morning, which is when the numbers a real installation shows are worth
// looking at, and the clock says how far into it we are. With the simulator's speed
// factor a screenshot at 20 s lands in the early morning.
constexpr float kTagBegin = 6.0f;
constexpr float kTagLaenge = 24.0f * 3600.0f;   // a whole day in seconds

float stundeAmTag() {
  const float t = (float)g_nowMs / 1000.0f;
  float h = kTagBegin + (t / kTagLaenge) * 24.0f;
  h = fmodf(h, 24.0f);
  return h;
}

// A bell that is zero at the edges and one in the middle, so nothing jumps when the
// hour wraps. Used for the PV curve and, shifted, for the household's.
static float Glocke(float h, float morgen, float abend) {
  if (h < morgen || h > abend) {
    return 0.0f;
  }
  const float t = (h - morgen) / (abend - morgen);
  return sinf(t * 3.14159265f);
}

class SimDriver : public DeviceDriver {
 public:
  void setTransport(DeviceTransport *) override {
    // No transport: there is nothing on the other end. A driver that reads a file
    // does not open a socket, and saying so here is better than opening one that
    // never connects.
  }

  void begin(const DeviceConfig &) override {
    memset(&m_state, 0, sizeof(m_state));
    if (!g_json.empty()) {
      fuelleAusDerDatei();
    } else {
      leereAnlagen();
    }
    // What this plant can report. A driver states it once, at begin(), and the rest
    // of the firmware asks: the web interface leaves a row out when the device has no
    // meter for it, and the flow diagram drops the node.
    //
    // The first version left the capabilities zeroed, and the JSON endpoint answered
    // "own": null, "draw": null, "load": null - three of the five day figures, gone
    // without a word, because an emulated device that says it can measure nothing
    // measures nothing. So: everything an RCT with a household meter can.
    m_state.caps.houseMeter = true;
    m_state.caps.gridMeter = true;
    m_state.caps.battery = true;
    m_state.caps.islandFlag = true;
    m_state.caps.faultBits = true;
    m_state.caps.sleepsWithoutGeneration = false;
    deviceStateMutable() = m_state;
    deviceSetSemantics(m_state.semantics);
  }

  // One collection run: the plant answers, the four published powers balance, and
  // the counters and the state of charge follow from them.
  //
  // THE BALANCE IS THE POINT. A panel whose four numbers do not add up is not a
  // layout problem, it is a wrong reading - and the first version of this driver took
  // the grid's value out of the file and then used it in the balance, so generation,
  // household, grid and battery were four independent numbers that did not belong
  // together. The panel showed 665 W where the PV should have been and a grey line
  // where a red one belongs.
  //
  // Here the surplus decides: it charges the battery up to the battery's limit, and
  // what is left over goes to the grid - or, when there is a shortfall, the battery
  // covers it up to its limit and the grid makes up the rest.
  //
  //   gen + grid(+import) = house + bat(+discharge)
  //
  void poll(uint32_t) override {
    const float h = stundeAmTag();
    DeviceState &s = deviceStateMutable();

    // PV: two strings, the second a quarter of the first, both following the sun.
    // The peak is the one this installation reaches at midsummer noon, which is the
    // widest thing the flow diagram has to carry.
    const float g = Glocke(h, 5.0f, 21.0f);
    const float erzeugung = m_pvSpitzeW * g;
    s.genW[0] = erzeugung * 0.8f;
    s.genW[1] = erzeugung * 0.2f;
    s.extW = 0.0f;

    // Household: a base load plus an evening peak - the reason a battery earns its
    // place in the picture at all.
    const float last = m_lastBasisW + m_lastSpitzeW * Glocke(h, 16.0f, 23.0f);
    s.houseW[0] = last * 0.34f;
    s.houseW[1] = last * 0.31f;
    s.houseW[2] = last * 0.35f;

    const float bilanz = erzeugung - last;   // + = surplus
    float akku = 0.0f;
    if (bilanz > 0.0f) {
      akku = (bilanz < m_akkuW) ? bilanz : m_akkuW;          // discharges
    } else {
      akku = (bilanz > -m_akkuW) ? bilanz : -m_akkuW;        // charges
    }
    s.gridExchangeW = bilanz - akku;          // + = import, - = feed-in
    s.batW = akku;
    s.batA = (s.batV > 1.0f) ? (s.batW / s.batV) : 0.0f;
    s.batV = m_simBatV - 0.01f * akku / 100.0f;   // a little under load

    // State of charge follows the power, and stops at the ends rather than running
    // past them - a battery past 100 % is the kind of thing a reader notices.
    const float schritt = m_simSekundenProSchritt();
    if (schritt > 0.0f) {
      const float dAh = (akku / s.batV) * schritt / 3600.0f;   // charge in Ah
      s.socPct -= 100.0f * dAh / (m_kapazitahtAh * 1000.0f);
      if (s.socPct > 100.0f) s.socPct = 100.0f;
      if (s.socPct < 5.0f) s.socPct = 5.0f;
      s.dayGenWh += erzeugung * schritt / 3600.0f;
      s.dayHouseWh += last * schritt / 3600.0f;
      if (s.gridExchangeW > 0.0f) {
        s.dayGridDrawWh += s.gridExchangeW * schritt / 3600.0f;
      } else {
        s.dayFeedInWh += -s.gridExchangeW * schritt / 3600.0f;
      }
      s.batteryTemp = 20.0f + 0.02f * (akku / 1000.0f);
    }

    s.haveData = true;
    s.haveBattery = true;
    s.connected = true;
    s.islandKnown = true;
    s.islandMode = false;
    s.asleep = false;
    s.lastUpdateMs = millis();
  }

  bool connected() const override { return true; }
  const char *typeName() const override { return "SIM"; }

 private:
  float m_pvSpitzeW = 3400.0f;
  float m_lastBasisW = 210.0f;
  float m_lastSpitzeW = 380.0f;
  float m_akkuW = 2500.0f;        // what the battery will take or give
  float m_kapazitahtAh = 100.0f;  // enough to make the state of charge move visibly
  float m_simBatV = 392.0f;
  uint32_t m_letzterPollMs = 0;
  DeviceState m_state;

  // The wall-clock seconds one poll covers. Only used for the counters, and zero
  // when the clock is pinned - which is deliberate: a pinned clock must not make
  // the totals grow, or two runs of the same build would differ on the energy page.
  float m_simSekundenProSchritt() {
    if (m_letzterPollMs == 0) {
      m_letzterPollMs = g_nowMs;
      return 0.0f;
    }
    const float s = (float)(g_nowMs - m_letzterPollMs) / 1000.0f;
    m_letzterPollMs = g_nowMs;
    return s;
  }

  // The name and version the emulator reports. Both have to be things a real
  // inverter cannot report, so that nobody mistakes a run here for the panel:
  //
  //   the name says SIMULIERT - a real device answers with its model
  //   the version is 0.0.0 with a marker - a real one answers with a number
  //                                above 1000, seen in a capture of this plant
  //
  // firmwareVersion is 24 bytes; the marker has to fit or strlcpy truncates it and
  // the difference is gone - which would be the same bug in a new place.
  static void setzeIdentitaet(DeviceState &s) {
    // The name is the string the settings page already offers for this device type,
    // so it comes out in the language the emulator is running in. Writing a German
    // literal here instead was what the first version did, and it showed up as
    // "SIMULIERT (kein Geraet)" on the English page - a German sentence on an
    // English screen, in the emulator whose whole job is to show what the panel
    // shows.
    //
    // 23 characters plus the terminator against deviceName[40]: strlcpy truncates
    // silently at the buffer, and a truncated identity is indistinguishable from a
    // real one.
    snprintf(s.deviceName, sizeof(s.deviceName), "%s", tr(T_OPT_TYPE_SIM));
    snprintf(s.firmwareVersion, sizeof(s.firmwareVersion), "0.0.0-sim");
  }

  void leereAnlagen() {
    DeviceState &s = m_state;
    s.semantics.loadMeterSeesExternal = false;
    s.semantics.genCounterSeesExternal = false;
    s.semantics.feedCounterNegative = true;
    // Identity, set again in fuelleAusDerDatei() AFTER the file has been read - see
    // the note there. Kept here too so that a run without a file still says what it
    // is.
    setzeIdentitaet(s);
    s.socPct = 42.0f;
    s.batV = m_simBatV;
    s.batterySoh = 100.0f;
    s.gridV[0] = 231.0f;
    s.gridV[1] = 231.0f;
    s.gridV[2] = 231.0f;
    s.gridHz[0] = 50.0f;
    s.gridHz[1] = 50.0f;
    s.gridHz[2] = 50.0f;
  }

  void fuelleAusDerDatei() {
    DeviceState &s = m_state;
    leereAnlagen();
    const char *j = g_json.data();
    const size_t n = g_json.size();

    // getNumber writes a double: the file's numbers are read at double precision
    // and narrowed here, so a value like 0,1 kWh is not decided by the file's last
    // digit.
    auto zahl = [&](const char *k, float *ziel) {
      double d = 0.0;
      if (getNumber(j, n, k, &d)) {
        *ziel = (float)d;
      }
    };
    for (int i = 0; i < 3; i++) {
      const std::string kv = std::string("gridV") + std::to_string(i);
      const std::string kh = std::string("gridHz") + std::to_string(i);
      const std::string kl = std::string("houseW") + std::to_string(i);
      zahl(kv.c_str(), &s.gridV[i]);
      zahl(kh.c_str(), &s.gridHz[i]);
      zahl(kl.c_str(), &s.houseW[i]);
    }
    zahl("genW0", &s.genW[0]);
    zahl("genW1", &s.genW[1]);
    zahl("extW", &s.extW);
    zahl("gridExchangeW", &s.gridExchangeW);
    zahl("socPct", &s.socPct);
    zahl("batV", &s.batV);
    zahl("batW", &s.batW);
    zahl("batA", &s.batA);
    zahl("batteryTemp", &s.batteryTemp);
    zahl("batteryCycles", &s.batteryCycles);
    zahl("batterySoh", &s.batterySoh);
    zahl("dayGenWh", &s.dayGenWh);
    zahl("dayHouseWh", &s.dayHouseWh);
    zahl("dayFeedInWh", &s.dayFeedInWh);
    zahl("dayGridDrawWh", &s.dayGridDrawWh);
    zahl("dayExtWh", &s.dayExtWh);
    zahl("monthGenWh", &s.monthGenWh);
    zahl("yearGenWh", &s.yearGenWh);
    zahl("totalGenWh", &s.totalGenWh);
    zahl("monthHouseWh", &s.monthHouseWh);
    zahl("yearHouseWh", &s.yearHouseWh);
    zahl("totalHouseWh", &s.totalHouseWh);
    zahl("monthFeedInWh", &s.monthFeedInWh);
    zahl("yearFeedInWh", &s.yearFeedInWh);
    zahl("feedInTotalWh", &s.feedInTotalWh);
    zahl("monthGridDrawWh", &s.monthGridDrawWh);
    zahl("yearGridDrawWh", &s.yearGridDrawWh);
    zahl("gridDrawTotalWh", &s.gridDrawTotalWh);
    zahl("monthExtWh", &s.monthExtWh);
    zahl("yearExtWh", &s.yearExtWh);
    zahl("totalExtWh", &s.totalExtWh);
    zahl("coreTemp", &s.coreTemp);
    zahl("heatSinkTemp", &s.heatSinkTemp);
    // The file may carry a real capture's deviceName and firmwareVersion, and it
    // does - the bundled mock has the values of a real inverter in it. Read them,
    // because they are part of what a capture is, and then overwrite them again: a
    // screenshot of the emulator must not be mistakable for a screenshot of the
    // panel, and "PS 10.0 32WB" + "2.3.5689" is exactly a real inverter's identity.
    //
    // Read first, then set: the other order was the bug, and it is a quiet one
    // because everything else in the file loads correctly.
    getString(j, n, "deviceName", s.deviceName, sizeof(s.deviceName));
    getString(j, n, "firmwareVersion", s.firmwareVersion,
              sizeof(s.firmwareVersion));
    setzeIdentitaet(s);

    // The plant the curve is built from, taken out of the file so that a real
    // capture's magnitudes can be used without editing this file.
    zahl("simPvSpitzeW", &m_pvSpitzeW);
    zahl("simLastBasisW", &m_lastBasisW);
    zahl("simLastSpitzeW", &m_lastSpitzeW);
    zahl("simAkkuW", &m_akkuW);
    zahl("simKapazitahtAh", &m_kapazitahtAh);
    m_simBatV = s.batV;
  }
};

SimDriver *g_driver = nullptr;

}  // namespace

DeviceDriver *makeSimDriver() {
  if (g_driver == nullptr) {
    g_driver = new SimDriver();
  }
  return g_driver;
}

void simDriverSetDataFile(const char *pfad) {
  g_pfad = (pfad != nullptr) ? pfad : "";
  g_json.clear();
  if (g_pfad.empty()) {
    return;
  }
  FILE *f = fopen(g_pfad.c_str(), "rb");
  if (f == nullptr) {
    fprintf(stderr, "Datendatei nicht lesbar: %s\n", g_pfad.c_str());
    return;
  }
  char puffer[4096];
  size_t k;
  while ((k = fread(puffer, 1, sizeof(puffer), f)) > 0) {
    g_json.append(puffer, k);
  }
  fclose(f);
  printf("SIM-Daten: %s (%zu Bytes)\n", g_pfad.c_str(), g_json.size());
}

void simDriverSetNowMs(uint32_t ms) { g_nowMs = ms; }