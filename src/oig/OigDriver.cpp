// The OpenInverterGateway driver.
//
// Structure of one poll, and why:
//
//   1. connect if needed (bounded, as for every driver - LVGL shares the task)
//   2. send "GET /status HTTP/1.0", which asks for the connection to close after
//      the answer. HTTP/1.0 is deliberate: HTTP/1.1 may answer with a chunked
//      body, and a chunked decoder on top of a byte transport that may drop a
//      frame is a lot of code for an endpoint that is 372 bytes long.
//   3. read until the peer closes or the budget is spent, keeping the body
//      after the blank line
//   4. read the body with src/device/Json.h
//
// Everything the driver publishes has a home in DeviceState, and everything the
// device does not have stays unset - the caps are the difference between a zero
// and a value that was never measured.
//
// SPDX-License-Identifier: MIT
#include "OigDriver.h"

#include <Arduino.h>

#include "../Diag.h"
#include "../device/Device.h"
#include "../device/Json.h"
#include "OigFields.h"

// The answer has to fit in one buffer, and how big that is has to be a fact and
// not a guess. Measured on real sticks: a Growatt1000s answers with 21 fields
// (about 500 bytes), and a Growatt MIC 1000 - same endpoint, same firmware
// family, but a hybrid with three-phase registers - with 64 fields and 1462
// bytes.
//
// 1024 was the guess, and it was wrong by exactly the 21 fields that follow the
// first kilobyte: every energy counter of that device. The panel showed "heute
// 0.00 kWh, gesamt 0.00 kWh" for a device that had been measuring for years, and
// nothing said why. So the buffer is four times as big, which is 3 kB of the
// panel's 320 kB, and a cut answer is now said out loud instead of being parsed
// as if it were whole.
//
// Static because this runs in the LVGL task and its stack is 8 kB.
static const size_t kOigBodyMax = 4096;
static char s_body[kOigBodyMax + 1];
static size_t s_bodyLen = 0;
// Bytes that arrived after the buffer was full. Counted rather than ignored: a
// truncated answer parses into a JSON that is still valid as far as it goes, and
// every quantity past the cut is then missing without a word.
static size_t s_bodyOver = 0;

static DeviceTransport *s_link = nullptr;
static DeviceConfig s_cfg;

// The largest absolute value the two meter registers have ever shown. A hybrid
// inverter publishes both whether or not a meter is fitted, and a register that
// has only ever read zero is a register with nothing behind it - see
// oigMeterVorhanden(). Kept here so the answer cannot be forgotten: a house that
// draws nothing this second has not lost its meter.
static float s_houseMaxW = 0.0f;
static float s_exportMaxW = 0.0f;
// Whether the proof has already been announced. A meter does not appear twice in
// the log.
static bool s_loggedHouse = false;
static bool s_loggedGrid = false;

// Connection state, kept here rather than in the state: the peerOpen() question
// belongs to the transport, and a driver that asked the transport twice per
// poll would learn nothing new the second time.
static uint32_t s_lastAttemptMs = 0;
static int s_sleepCycles = 0;

// A dead device is retried every 5 s, the same cadence the RCT driver uses.
// Faster gains nothing (the device is either off or reachable) and slower would
// make a reboot of the stick take noticeably longer to notice.
static const uint32_t kReconnectRetryMs = 5000;

// The connect itself is bounded tightly: an address that does not answer must
// not hold the panel still, and the state only needs to be right on the next
// poll.
static const uint32_t kConnectTimeoutMs = 400;

// The send-all or nothing rule, from the transport contract.
static bool sendAll(const char *s) {
  return s_link->write((const uint8_t *)s, strlen(s)) == strlen(s);
}

// True when the device is expected to be off: generation has been below the
// threshold for kSleepCycles polls. Set only after data has arrived at least
// once - a device that has never answered is a fault until proven otherwise,
// and calling it asleep would hide exactly the failure the badge exists for.
static bool expectAsleep(const DeviceState &st, float genW) {
  if (!st.haveData) {
    return false;
  }
  if (genW >= OigDriver::kSleepPowerW) {
    return false;
  }
  return s_sleepCycles >= OigDriver::kSleepCycles;
}

// Read the answer into s_body. Returns the body length, or 0 when nothing
// usable arrived. The header is skipped by looking for the blank line rather
// than by parsing status line and headers: the endpoint answers 200 with a
// Content-Length that matches, and a caller that got a 404 would find no
// '{' in the body and take the same path as a timeout.
static size_t readAnswer(uint32_t budgetMs) {
  const uint32_t started = millis();
  size_t total = 0;
  bool headerDone = false;
  s_bodyLen = 0;
  s_bodyOver = 0;

  while ((int32_t)(millis() - started) < (int32_t)budgetMs) {
    while (s_link->available() > 0) {
      const int c = s_link->read();
      if (c < 0) {
        break;
      }
      const uint8_t byte = (uint8_t)c;
      // The blank line between header and body. Four bytes in a row is not
      // possible inside a JSON answer, so counting them is enough.
      static uint8_t crlf = 0;
      if (headerDone) {
        if (s_bodyLen < kOigBodyMax) {
          s_body[s_bodyLen++] = (char)byte;
        } else {
          // Over the cap: keep reading and count, because the difference between
          // "this answer was too big" and "this answer ended here" is worth a
          // line in the log - and a device that grew a field must not do it
          // silently.
          s_bodyOver++;
        }
      } else {
        if (byte == '\r' || byte == '\n') {
          crlf = (uint8_t)(crlf + 1);
          if (crlf >= 4) {
            headerDone = true;
            crlf = 0;
          }
        } else {
          crlf = 0;
        }
      }
      total++;
    }
    if (!s_link->peerOpen()) {
      break; // HTTP/1.0 with Connection: close: the end is the socket closing
    }
    deviceYieldHook();
    delay(1);
  }

  s_body[s_bodyLen] = '\0';
  // Without a header separator there was no answer at all (a bare connect, a
  // reset); with one but no '{' there was an answer that is not this endpoint's.
  return (headerDone && s_bodyLen > 0 && s_body[0] == '{') ? s_bodyLen : 0;
}

// One quantity, with the list of names it can have (OigFields.h). The answer
// decides which one exists - nothing here knows a protocol, and a quantity the
// device does not publish keeps the value the caller had, which for a
// measurement that does not exist means "untouched": the same last-good rule the
// RCT driver uses.
static bool num(const char *const *names, float *out) {
  return oigNumber(s_body, s_bodyLen, names, out);
}

void OigDriver::setTransport(DeviceTransport *link) { s_link = link; }

void OigDriver::begin(const DeviceConfig &cfg) {
  s_cfg = cfg;

  // What an OpenInverterGateway can report depends on its model, and the only
  // honest source for that is an answer. So the caps start out at "nothing
  // known" and are set from the first complete answer - per field, so a model
  // that has a battery but no local load registers ends up with a battery and
  // without a household meter. Before that first answer the panel shows dashes,
  // which is the truth: nothing has been measured yet.
  deviceSetSemantics({true, true, false}); // meters see everything; feed in is positive

  DeviceState &st = deviceStateMutable();
  st.caps = DeviceCaps();
  st.caps.sleepsWithoutGeneration = true; // true of the family, not of the model
  st.asleep = false;
  s_sleepCycles = 0;
}

void OigDriver::poll(uint32_t budgetMs) {
  DeviceState &st = deviceStateMutable();

  if (!s_link->peerOpen()) {
    st.connected = false;
    // A sleeping device is not a device to reconnect to. Once it has gone quiet
    // after a real sunset, the poll returns immediately until the panel sees
    // generation again - which it cannot, because the device is the only source
    // of that. So the sleep is left to time, not to the retry counter: after
    // roughly a night the link is tried again every 5 s as before.
    const uint32_t now = millis();
    if ((int32_t)(now - s_lastAttemptMs) < (int32_t)kReconnectRetryMs) {
      return;
    }
    s_lastAttemptMs = now;
    diagPhase("oig.connect");
    if (!s_link->open(s_cfg.host, s_cfg.port, kConnectTimeoutMs)) {
      s_link->close(); // a failed connect can leave a half-open socket
      st.connected = false;
      return;
    }
    st.connected = true;
    deviceYieldHook();
  }

  diagPhase("oig.poll");
  char req[96];
  snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
           kOigStatusPath, s_cfg.host);
  if (!sendAll(req)) {
    s_link->close();
    st.connected = false;
    return;
  }

  const size_t got = readAnswer(budgetMs);
  if (got == 0) {
    // Nothing came. If the device had reported no generation for a while, this
    // is a night, not a fault - the state says so and DataStatus.h turns that
    // into its own badge instead of a red one.
    if (expectAsleep(st, st.genW[0] + st.genW[1])) {
      st.asleep = true;
    }
    s_link->close(); // keep the socket clean; reconnect on the next poll
    st.connected = false;
    return;
  }

  s_link->close();
  st.connected = true;
  st.asleep = false;

  // --- What the device is -------------------------------------------------
  char name[40];
  if (json::getString(s_body, s_bodyLen, "Hostname", name, sizeof(name)) &&
      name[0] != '\0') {
    strlcpy(st.deviceName, name, sizeof(st.deviceName));
  }

  // --- Generation ----------------------------------------------------------
  // DcPower is the string inverter's own input power and the closest thing it
  // measures to what the array delivers; AcPower is what comes out of it. Which
  // of the two a model publishes varies, so both are tried and the first that
  // exists is used - and the existence of each is what sets the caps.
  float dcPower = 0.0f, acPower = 0.0f;
  float pv2 = 0.0f;
  const bool haveDc = num(kOigGeneration, &dcPower);
  // A two-string device reports its strings one by one (SPF: PV1ChargePwr /
  // PV2ChargePwr, TLXH: PV1Power / PV2Power), and the second one is added:
  // stopping at the first name that exists would silently report half.
  static const char *const kPv2[] = {"PV2ChargePwr", "PV2Power", nullptr};
  const bool havePv2 = num(kPv2, &pv2);
  const bool haveAc = num(kOigAcPower, &acPower);
  if (haveDc) {
    st.genW[0] = dcPower;
    if (havePv2) {
      st.genW[1] = pv2;
    }
  }
  if (haveDc || haveAc) {
    st.caps.gridMeter = false; // the inverter's AC power is not the grid meter
    s_sleepCycles = (dcPower < kSleepPowerW) ? s_sleepCycles + 1 : 0;
  }

  // --- Household and grid --------------------------------------------------
  // A model with local-load registers (Growatt307's ACPowerToUser and its
  // EnergyToUserToday) has measured the house; the simplest one has not, and
  // then the household value stays unmeasured rather than zero.
  // Whether the register is a meter is not the same as whether it exists - see
  // oigMeterVorhanden(). Both conclusions accumulate from here on: the largest
  // value each register has ever shown, which is what decides, and never goes back
  // down. A house that draws nothing this second has not lost its meter.
  float load = 0.0f;
  if (num(kOigHouse, &load)) {
    st.houseW[0] = load;
    s_houseMaxW = fmaxf(s_houseMaxW, fabsf(load));
  }
  st.caps.houseMeter = oigMeterVorhanden(s_houseMaxW);

  float toGrid = 0.0f;
  if (num(kOigExport, &toGrid)) {
    // The stick reports the feed-in; the panel's sign convention is + = draw
    // from the grid, so a feed-in is negative. And it is a measurement, so
    // genCounterSeesExternal does not come into it - the meter does not exist on
    // the other side.
    st.gridExchangeW = -toGrid;
    s_exportMaxW = fmaxf(s_exportMaxW, fabsf(toGrid));
  }
  st.caps.gridMeter = oigMeterVorhanden(s_exportMaxW);

  // --- Battery -------------------------------------------------------------
  // Whether there IS a battery is a different question from whether the device
  // publishes battery registers, and a hybrid inverter answers the second one
  // with "yes" on a system that has none: a MIC 1000 without a battery reports
  // BatteryState 0, SOC 0, ChargePower 0, DischargePower 0, BatteryVoltage 0. From
  // the registers alone the panel would have drawn a battery node and a battery
  // pill full of zeros - the same mistake as the household meter, one layer down.
  //
  // So the state register decides when it exists (zero means none attached, see
  // OigFields.h for what is and is not documented about that), and only a
  // protocol without such a register falls back to the old rule "an SOC field
  // exists". The raw state value goes into the log, so a battery whose value
  // means something else shows up there instead of in a guess here.
  float soc = 0.0f;
  const bool haveSoc = num(kOigSoc, &soc);
  float bstate = 0.0f;
  const bool batterieDa = oigBatteryPresent(s_body, s_bodyLen, &bstate);
  const bool haveState =
      oigFieldPresent(s_body, s_bodyLen, kOigBatteryState) != nullptr;
  st.caps.battery = batterieDa;
  st.haveBattery = batterieDa;
  if (haveState) {
    static bool logged = false;
    if (!logged && haveSoc) {
      logged = true;
      Serial.printf("OIG: BatteryState %.0f -> Akku %s\n", (double)bstate,
                    batterieDa ? "vorhanden" : "nicht vorhanden");
    }
  }
  if (batterieDa) {
    if (haveSoc) {
      st.socPct = soc;
    }
    // Discharge first: a device that publishes both reports nothing on the
    // charging register while it discharges, so preferring charge would make a
    // discharging battery look idle. The stick's sign is already the panel's
    // (positive = discharging), so nothing is negated here.
    float entladen = 0.0f;
    if (num(kOigDischarge, &entladen)) {
      st.batW = entladen;
    } else {
      float charge = 0.0f;
      if (num(kOigCharge, &charge)) {
        // ChargePower positive is charging; the panel's convention is positive =
        // discharging.
        st.batW = -charge;
      }
    }
    float volt = 0.0f;
    if (num(kOigBatteryVoltage, &volt)) {
      st.batV = volt;
    }
    float btemp = 0.0f;
    if (num(kOigBatteryTemperature, &btemp)) {
      st.batteryTemp = btemp;
    }
  }

  // --- Counters ------------------------------------------------------------
  // The stick's energy fields are kWh; the panel counts in Wh.
  float e = 0.0f;
  if (num(kOigEnergyToday, &e)) {
    st.dayGenWh = e * 1000.0f;
  }
  if (num(kOigEnergyTotal, &e)) {
    st.totalGenWh = e * 1000.0f;
  }
  if (num(kOigEnergyToGrid, &e)) {
    st.dayFeedInWh = e * 1000.0f;
  }
  if (num(kOigEnergyToUser, &e)) {
    st.dayHouseWh = e * 1000.0f;
  }

  // --- Device detail -------------------------------------------------------
  float t = 0.0f;
  if (num(kOigTemperature, &t)) {
    st.coreTemp = t;
  }
  float gridV = 0.0f;
  if (num(kOigVoltage, &gridV)) {
    st.gridV[0] = gridV;
  }
  float gridHz = 0.0f;
  if (num(kOigFrequency, &gridHz)) {
    st.gridHz[0] = gridHz;
  }

  // A fault register exists on the protocols that have one; InverterStatus is
  // the stick's own summary and 3 means an error (GrowattTypes.h).
  float status = 0.0f;
  if (num(kOigStatus, &status) && status >= 3.0f) {
    st.faultBits[0] = 0x1u; // one bit: something is wrong, no detail claimed
    st.caps.faultBits = true;
  }

  // Island / off-grid: no OpenInverterGateway register for it. The cap stays
  // false, so the panel draws no island triangle and the switching output's
  // island rule reports "unknown" rather than guessing.
  st.caps.islandFlag = false;

  // A meter that proves itself after the first answers is worth a line of its own:
  // the layout changes when it appears, and the log is where that has to be
  // explainable. The first line says what the device claims at first sight, this
  // one says what it has actually shown.
  if ((st.caps.houseMeter && !s_loggedHouse) ||
      (st.caps.gridMeter && !s_loggedGrid)) {
    if (st.caps.houseMeter && !s_loggedHouse) {
      s_loggedHouse = true;
      Serial.printf("OIG: ACPowerToUser hat %.0f W gezeigt -> Hauszaehler da\n",
                    (double)s_houseMaxW);
    }
    if (st.caps.gridMeter && !s_loggedGrid) {
      s_loggedGrid = true;
      Serial.printf("OIG: ACPowerToGrid hat %.0f W gezeigt -> Netzzaehler da\n",
                    (double)s_exportMaxW);
    }
  }

  st.haveData = true;
  st.lastUpdateMs = millis();

  static bool logged = false;
  if (!logged) {
    logged = true;
    Serial.printf(
        "OIG: %s meldet DC %.0f W, AC %.0f W, heute %.2f kWh, Haus%s, "
        "Netz%s, Akku%s - Felder der Antwort bestimmen die Anzeige\n",
        st.deviceName[0] ? st.deviceName : "(ohne Namen)", (double)dcPower,
        (double)acPower, (double)(st.dayGenWh / 1000.0f),
        st.caps.houseMeter ? " ja" : " nein", st.caps.gridMeter ? " ja" : " nein",
        st.caps.battery ? " ja" : " nein");
  }

  // The grid quality is in the line because it is the value whose name varies
  // most between the protocols, and because a stick reporting 49.8 Hz instead of
  // 50 is worth seeing in a log rather than only on a page nobody opens. A device
  // that publishes neither shows a zero here - the caps above say why it is
  // missing, so the two lines together are unambiguous.
  // The length is in bytes, and it used to be labelled "Felder" - which is how a
  // truncated answer (1024 of 1462) could sit in the log for a whole evening
  // looking like a field count. If the answer did not fit, that is said here, in
  // the line that is always written, instead of in a line nobody reads twice.
  Serial.printf("OIG: DC %.0f W | AC %.0f W | heute %.2f kWh gesamt %.2f kWh | "
                "%.1f V %.2f Hz | %d Byte%s | Haus%s Netz%s Akku%s\n",
                (double)dcPower, (double)acPower,
                (double)(st.dayGenWh / 1000.0f),
                (double)(st.totalGenWh / 1000.0f), (double)st.gridV[0],
                (double)st.gridHz[0], (int)(s_bodyLen + s_bodyOver),
                s_bodyOver > 0 ? ", ABGESCHNITTEN" : "",
                st.caps.houseMeter ? "j" : "n", st.caps.gridMeter ? "j" : "n",
                st.caps.battery ? "j" : "n");
}

bool OigDriver::connected() const { return deviceState().connected; }