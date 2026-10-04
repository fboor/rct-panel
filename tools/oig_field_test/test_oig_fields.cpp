// Host test for the OpenInverterGateway field tables (src/oig/OigFields.h).
//
// The field names are data in that header, and this is what keeps them honest:
// the expected field names of all seven Growatt protocols were extracted from
// the OpenInverterGateway source, so every list here is checked against what the
// devices really publish - and a name that no protocol has has no business being
// in a list.
//
// It exists because the frequency was missing on a real stick for a reason no
// test would have caught: the driver asked for GridFrequency, a Growatt305 says
// AcFrequency, and the value stayed 0. The display then showed a zero where a
// measurement belonged - the same failure as the OIG's household meter, arrived
// at from the other side.
//
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include <string>

#include "oig/OigFields.h"

static int g_checks = 0;
static int g_failed = 0;

static void check(bool ok, const char *what) {
  g_checks++;
  if (!ok) {
    g_failed++;
    printf("FAIL  %s\n", what);
  }
}

static void checkNearValue(float got, float want, const char *what) {
  g_checks++;
  if (got - want > 0.001f || want - got > 0.001f) {
    g_failed++;
    printf("FAIL  %s: got %f, want %f\n", what, (double)got, (double)want);
  }
}

// An answer carrying exactly the fields one protocol publishes. Building it from
// a name list rather than a fixed string is the point: the test states what a
// device says, and the tables have to find it in that.
static std::string answerWith(const char *const *names) {
  std::string j = "{";
  bool first = true;
  for (size_t i = 0; names[i] != nullptr; i++) {
    if (!first) {
      j += ",";
    }
    first = false;
    j += "\"";
    j += names[i];
    j += "\""; // the closing quote - without it the answer is not JSON at all
    // Every field a number, so that a name which fails to resolve fails because
    // the list does not know it - and not because the generator happened to
    // write a string here. A field that holds something else is its own test
    // (testStringIsNotAValue).
    j += ":1";
  }
  j += "}";
  return j;
}

// The frequency spellings, one per source. SIMULATE is the simulator's own
// eleven invented values, the rest are what the protocols publish.
static const char *const kSim[] = {"Hostname", "Status", "DcPower", "DcVoltage",
                                   "DcInputCurrent", "AcFreq", "AcVoltage",
                                   "AcPower", "EnergyToday", "EnergyTotal",
                                   "OperatingTime", "Temperature", nullptr};
static const char *const kP120[] = {"InputPower", "GridFrequency", "GridL1Voltage",
                                    "GridL2Voltage", "EnergyToday", "EnergyTotal",
                                    "InverterStatus", nullptr};
static const char *const kP124[] = {"InputPower", "GridFrequency", "SOC",
                                    "ACPowerToUser", "ACPowerToGrid",
                                    "ACPowerToUserTotal", "ACPowerToGridTotal",
                                    "EnergyToUserToday", "EnergyToGridToday",
                                    "L1ThreePhaseGridVoltage", "BatteryVoltage",
                                    "ChargePower", "BatteryTemperature",
                                    nullptr};
static const char *const kP305[] = {"DcPower", "DcVoltage", "DcInputCurrent",
                                    "AcFrequency", "AcOutputCurrent", "AcPower",
                                    "AcVoltage", "EnergyToday", "EnergyTotal",
                                    "InverterStatus", "OperatingTime",
                                    "Temperature", nullptr};
static const char *const kP307[] = {"InputPower", "GridFrequency",
                                    "L1ThreePhaseGridVoltage", "SOC",
                                    "INVPowerToLocalLoad", "ACPowerToUser",
                                    "ACPowerToGrid", "EnergyToGridToday",
                                    "ChargePower", "BatteryVoltage", nullptr};
static const char *const kPBP[] = {"InputPower", "GridFrequency",
                                   "L1ThreePhaseGridVoltage", "BatteryPercentage",
                                   "InverterStatus", nullptr};
static const char *const kPSPF[] = {"PV1ChargePwr", "PV2ChargePwr", "LineFrequency",
                                    "OutFrequency", "GridInVoltage", "OutVoltage",
                                    "BattSOC", "BattVoltage", "InverterStatus",
                                    nullptr};

// A Growatt MIC 1000 behind a stick, field for field out of a real /status: 64
// names, 1462 bytes. This one is not from the OpenInverterGateway source but off a
// stick answering on the network, because it is the device the panel is being
// tested with - and it disagrees with the tables in two ways that no test would
// have found: it calls the AC output "OutputPower" and the generation counters
// "TodayGenerateEnergy"/"TotalGenerateEnergy".
//
// The battery registers are all in here even though the device has no battery
// attached: BatteryState, SOC, ChargePower, DischargePower, BatteryVoltage and
// BatteryTemperature all answer 0. That is the case testBatteryPresence covers.
static const char *const kMic1000[] = {
    "ACChargeEnergyToday", "ACChargeEnergyTotal", "ACPowerToGrid",
    "ACPowerToGridTotal", "ACPowerToUser", "ACPowerToUserTotal",
    "ActivePowerRate", "BatteryState", "BatteryTemperature", "BatteryVoltage",
    "BoostTemperature", "ChargeEnergyToday", "ChargeEnergyTotal", "ChargePower",
    "Cnt", "DischargeEnergyToday", "DischargeEnergyTotal", "DischargePower",
    "EnergyToGridToday", "EnergyToGridTotal", "EnergyToUserToday",
    "EnergyToUserTotal", "GridFrequency", "HeapFragmentation", "HeapFree",
    "HeapMaxAlloc", "HeapMinFree", "Hostname", "INVPowerToLocalLoad",
    "INVPowerToLocalLoadTotal", "InputPower", "InverterStatus",
    "InverterTemperature", "L1ThreePhaseGridOutputCurrent",
    "L1ThreePhaseGridOutputPower", "L1ThreePhaseGridVoltage",
    "L2ThreePhaseGridOutputCurrent", "L2ThreePhaseGridOutputPower",
    "L2ThreePhaseGridVoltage", "L3ThreePhaseGridOutputCurrent",
    "L3ThreePhaseGridOutputPower", "L3ThreePhaseGridVoltage",
    "LocalLoadEnergyToday", "LocalLoadEnergyTotal", "Mac", "OutputPower",
    "PV1EnergyToday", "PV1EnergyTotal", "PV1InputCurrent", "PV1InputPower",
    "PV1Voltage", "PV2EnergyToday", "PV2EnergyTotal", "PV2InputCurrent",
    "PV2InputPower", "PV2Voltage", "PVEnergyTotal", "SOC", "TWorkTimeTotal",
    "TemperatureInsideIPM", "TodayGenerateEnergy", "TotalGenerateEnergy",
    "Uptime", "WifiRSSI", nullptr};

static const char *const kPTLXH[] = {"PV1Power", "PV2Power", "GridFrequency",
                                     "L1ThreePhaseGridVoltage", "BDCSysState",
                                     "TodayEnergyToGrid", "TodayEnergyToUser",
                                     "BDCChargePower", nullptr};

// The bug, as a test: a Growatt305 answers AcFrequency, the table used to ask
// for GridFrequency, and the frequency stayed 0.
static void testFrequencyOnEveryDevice() {
  struct {
    const char *const *fields;
    const char *what;
  } dev[] = {{kSim, "Simulator"}, {kP120, "Growatt120"}, {kP124, "Growatt124"},
            {kP305, "Growatt305"}, {kP307, "Growatt307"}, {kPBP, "GrowattBP"},
            {kPSPF, "GrowattSPF"}, {kPTLXH, "GrowattTLXH"}};
  for (const auto &d : dev) {
    const std::string j = answerWith(d.fields);
    float v = 0;
    const char *used = nullptr;
    const bool ok = oigNumber(j.c_str(), j.size(), kOigFrequency, &v, &used);
    char msg[96];
    snprintf(msg, sizeof(msg), "%s: Frequenz wird gefunden (%s)", d.what,
             used ? used : "-");
    check(ok, msg);
  }
  // The three spellings that have to work, named one by one, because "it works
  // for all seven" hides which one was wrong last time.
  const std::string s305 = answerWith(kP305);
  float v = 0;
  check(oigNumber(s305.c_str(), s305.size(), kOigFrequency, &v) &&
            strcmp(oigFieldPresent(s305.c_str(), s305.size(), kOigFrequency),
                   "AcFrequency") == 0,
        "Growatt305: es ist AcFrequency, nicht GridFrequency");
  const std::string sSim = answerWith(kSim);
  check(oigFieldPresent(sSim.c_str(), sSim.size(), kOigFrequency) != nullptr &&
            strstr(oigFieldPresent(sSim.c_str(), sSim.size(), kOigFrequency),
                   "AcFreq") != nullptr,
        "Simulator: AcFreq wird gefunden");
}

// Voltage: every protocol spells the grid differently.
static void testVoltageOnEveryDevice() {
  struct {
    const char *const *fields;
    const char *what;
  } dev[] = {{kSim, "Simulator"}, {kP120, "Growatt120"}, {kP124, "Growatt124"},
            {kP305, "Growatt305"}, {kP307, "Growatt307"}, {kPBP, "GrowattBP"},
            {kPSPF, "GrowattSPF"}, {kPTLXH, "GrowattTLXH"}};
  for (const auto &d : dev) {
    const std::string j = answerWith(d.fields);
    float v = 0;
    char msg[96];
    snprintf(msg, sizeof(msg), "%s: Spannung wird gefunden", d.what);
    check(oigNumber(j.c_str(), j.size(), kOigVoltage, &v), msg);
  }
}

// Generation: DcPower, InputPower or the per-string powers.
static void testGenerationOnEveryDevice() {
  struct {
    const char *const *fields;
    const char *what;
  } dev[] = {{kSim, "Simulator"}, {kP120, "Growatt120"}, {kP124, "Growatt124"},
            {kP305, "Growatt305"}, {kP307, "Growatt307"}, {kPBP, "GrowattBP"},
            {kPSPF, "GrowattSPF"}, {kPTLXH, "GrowattTLXH"}};
  for (const auto &d : dev) {
    const std::string j = answerWith(d.fields);
    float v = 0;
    char msg[96];
    snprintf(msg, sizeof(msg), "%s: Erzeugung wird gefunden", d.what);
    check(oigNumber(j.c_str(), j.size(), kOigGeneration, &v), msg);
  }
  // Two-string devices report the strings one by one; both have to be there, or
  // the second one is silently dropped and the total is half.
  const std::string spf = answerWith(kPSPF);
  float a = 0, b = 0;
  check(oigNumber(spf.c_str(), spf.size(), kOigGeneration, &a),
        "SPF: erster String");
  static const char *const kPv2[] = {"PV2ChargePwr", nullptr};
  check(oigNumber(spf.c_str(), spf.size(), kPv2, &b), "SPF: zweiter String");
}

// What a plain string inverter does not have: no household meter, no export
// meter, no battery. The tables must find nothing, so the driver reports the
// device as not having it.
static void testPlainInverterHasNoMeters() {
  const std::string j = answerWith(kP305);
  float v = 0;
  check(!oigNumber(j.c_str(), j.size(), kOigHouse, &v),
        "Growatt305: kein Hauszaehler - und das wird auch so gesagt");
  check(!oigNumber(j.c_str(), j.size(), kOigExport, &v),
        "Growatt305: kein Netzzaehler");
  check(!oigNumber(j.c_str(), j.size(), kOigSoc, &v),
        "Growatt305: kein Akku");

  // A hybrid has all three.
  const std::string h = answerWith(kP307);
  check(oigNumber(h.c_str(), h.size(), kOigHouse, &v), "Growatt307: Hauszaehler");
  check(oigNumber(h.c_str(), h.size(), kOigExport, &v), "Growatt307: Netzzaehler");
  check(oigNumber(h.c_str(), h.size(), kOigSoc, &v), "Growatt307: Ladestand");
}

// Every quantity the panel draws, read out of that device's own answer. Before
// these names were added, the AC power, both generation counters and the battery
// temperature came out as zero on a device that reports all of them.
static void testMic1000() {
  const std::string j = answerWith(kMic1000);
  const char *jt = j.c_str();
  const size_t n = j.size();
  float v = 0;
  struct {
    const char *const *table;
    const char *want; // the name this device uses, so the test says which
    const char *what;
  } q[] = {
      {kOigGeneration, "InputPower", "Erzeugung"},
      {kOigAcPower, "OutputPower", "AC-Leistung"},
      {kOigVoltage, "L1ThreePhaseGridVoltage", "Netzspannung"},
      {kOigFrequency, "GridFrequency", "Frequenz"},
      {kOigHouse, "ACPowerToUser", "Haus"},
      {kOigExport, "ACPowerToGrid", "Einspeisung"},
      {kOigEnergyToday, "TodayGenerateEnergy", "Erzeugung heute"},
      {kOigEnergyTotal, "TotalGenerateEnergy", "Erzeugung gesamt"},
      {kOigEnergyToGrid, "EnergyToGridToday", "Einspeisung heute"},
      {kOigEnergyToUser, "EnergyToUserToday", "Haus heute"},
      {kOigSoc, "SOC", "Ladestand"},
      {kOigCharge, "ChargePower", "Ladeleistung"},
      {kOigDischarge, "DischargePower", "Entladeleistung"},
      {kOigBatteryState, "BatteryState", "Batteriezustand"},
      {kOigBatteryVoltage, "BatteryVoltage", "Batteriespannung"},
      {kOigBatteryTemperature, "BatteryTemperature", "Batterietemperatur"},
      {kOigTemperature, "InverterTemperature", "Gerätetemperatur"},
      {kOigStatus, "InverterStatus", "Status"},
  };
  for (const auto &x : q) {
    const bool ok = oigNumber(jt, n, x.table, &v);
    char msg[96];
    snprintf(msg, sizeof(msg), "MIC1000: %s wird gelesen (%s)", x.what, x.want);
    check(ok, msg);
    snprintf(msg, sizeof(msg), "MIC1000: %s heisst dort %s", x.what, x.want);
    check(ok && strcmp(oigFieldPresent(jt, n, x.table), x.want) == 0, msg);
  }
}

// The battery question, which is not the same as the SOC question. This device
// publishes six battery registers and has no battery attached; a panel that reads
// "SOC exists" draws a battery node full of zeros, which is the mistake the whole
// capability layer was built to avoid.
static void testBatteryPresence() {
  // The real answer of the device without a battery, with the zeros it sends.
  const std::string ohne = "{\"BatteryState\":0,\"SOC\":0,\"ChargePower\":0,"
                           "\"DischargePower\":0,\"BatteryVoltage\":0,"
                           "\"BatteryTemperature\":0}";
  float state = -1.0f;
  check(!oigBatteryPresent(ohne.c_str(), ohne.size(), &state),
        "MIC1000 ohne Akku: kein Akku, obwohl sechs Batterieregister antworten");
  checkNearValue(state, 0.0f, "und der Rohwert steht im Protokoll");

  // The same device with one attached: every register answers, the state does not.
  const std::string mit = "{\"BatteryState\":1,\"SOC\":78,\"ChargePower\":0,"
                          "\"DischargePower\":540,\"BatteryVoltage\":52.3}";
  check(oigBatteryPresent(mit.c_str(), mit.size(), &state),
        "MIC1000 mit Akku: Akku vorhanden");
  checkNearValue(state, 1.0f, "und der Rohwert steht im Protokoll");
  // And the power that belongs to it: discharge wins, because a device that
  // publishes both says nothing on ChargePower while it discharges.
  float w = 0;
  check(oigNumber(mit.c_str(), mit.size(), kOigDischarge, &w),
        "Entladeleistung wird gelesen");
  check(oigNumber(mit.c_str(), mit.size(), kOigBatteryVoltage, &w),
        "Batteriespannung wird gelesen");

  // A protocol without a state register keeps the old rule: the registers are the
  // only answer the device gives.
  const std::string spf = answerWith(kPSPF);
  check(oigBatteryPresent(spf.c_str(), spf.size(), &state),
        "SPF ohne Zustandsregister: ein SOC-Feld gilt als Akku");
  const std::string p120 = answerWith(kP120);
  check(!oigBatteryPresent(p120.c_str(), p120.size(), &state),
        "Growatt120 ohne SOC: kein Akku");
}

// The meter question, which no register answers and only time does. The device
// used for the test publishes ACPowerToUser and ACPowerToGrid in both cases; what
// separates them is whether anything but a zero has ever been in them.
static void testMeterPresence() {
  // No meter behind the registers: every poll says zero, forever.
  check(!oigMeterVorhanden(0.0f), "nur Nullen: das ist kein Zaehler");
  check(oigMeterVorhanden(1.0f), "ein Watt: das ist einer");
  check(oigMeterVorhanden(0.5f), "ein halber Watt zaehlt auch");
  check(!oigMeterVorhanden(0.4f), "unter einem halben Watt nicht - das ist Rauschen");
  // A house that is asleep right now has not lost its meter: the driver keeps the
  // largest value it has seen, and that is what this takes.
  check(oigMeterVorhanden(1480.0f), "ein Haus mit 1480 W Spitze hatte einen Zaehler");

  // And the device that answers both ways: the fields exist in either case, so
  // only the value distinguishes them.
  const std::string j = answerWith(kMic1000);
  float v = 0;
  check(oigNumber(j.c_str(), j.size(), kOigHouse, &v),
        "MIC1000: das Hausregister gibt es");
  check(oigNumber(j.c_str(), j.size(), kOigExport, &v),
        "MIC1000: das Einspeiseregister gibt es auch");
}

// What the inverter delivers, minus what it feeds in - usable as a household only
// where both halves are measurements, which is what the driver checks before it
// uses it (see the fourth rule in DeviceCaps.h). The arithmetic is worth checking
// on its own, especially the clamp: a household that consumes negative watts does
// not exist, and a rounding difference between two registers must not invent one.
static void testHouseholdFromAc() {
  checkNearValue(oigHausAusAc(181.0f, 0.0f), 181.0f,
                 "ohne Zaehler: alles, was der Wechselrichter abgibt, ist der Hausverbrauch");
  checkNearValue(oigHausAusAc(181.0f, 60.0f), 121.0f,
                 "mit Einspeisung: die Einspeisung gehoert nicht ins Haus");
  checkNearValue(oigHausAusAc(100.0f, 101.0f), 0.0f,
                 "mehr Einspeisung als Erzeugung: null, nicht negativ");
  checkNearValue(oigHausAusAc(0.0f, 0.0f), 0.0f, "nichts geliefert, nichts eingespeist");
}

// A key that holds a string is not a measurement. The list must move on instead
// of reporting a quantity it cannot read.
static void testStringIsNotAValue() {
  // answerWith() writes a string for every third field, so build one by hand.
  const std::string j = "{\"AcFrequency\":\"50\",\"GridFrequency\":50}";
  float v = 0;
  const char *used = nullptr;
  check(oigNumber(j.c_str(), j.size(), kOigFrequency, &v, &used),
        "Frequenz aus dem zweiten Namen, nachdem der erste Text war");
  check(used != nullptr && strcmp(used, "GridFrequency") == 0,
        "und es ist der Name, der eine Zahl traegt");
  checkNearValue(v, 50.0f, "Wert stimmt");
}

int main() {
  testFrequencyOnEveryDevice();
  testVoltageOnEveryDevice();
  testGenerationOnEveryDevice();
  testPlainInverterHasNoMeters();
  testMic1000();
  testBatteryPresence();
  testMeterPresence();
  testHouseholdFromAc();
  testStringIsNotAValue();

  printf("%s: %d Prüfungen, %d fehlgeschlagen\n",
         g_failed == 0 ? "OK" : "FEHLER", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}