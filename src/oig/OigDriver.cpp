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

// The stick's answer fits in one buffer with room to spare; a Growatt307 with
// battery reports around forty fields. 1024 is the point past which something is
// wrong, and the buffer is static because this runs in the LVGL task and its
// stack is 8 kB.
static const size_t kOigBodyMax = 1024;
static char s_body[kOigBodyMax + 1];
static size_t s_bodyLen = 0;

static DeviceTransport *s_link = nullptr;
static DeviceConfig s_cfg;

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
        }
        // Over the cap: the answer is not one of ours. Stop reading rather than
        // growing, and let the body be rejected below.
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
  float load = 0.0f;
  if (num(kOigHouse, &load)) {
    st.houseW[0] = load;
    st.caps.houseMeter = true;
  }

  float toGrid = 0.0f;
  if (num(kOigExport, &toGrid)) {
    // The stick reports the feed-in; the panel's sign convention is + = draw
    // from the grid, so a feed-in is negative. And it is a measurement, so
    // genCounterSeesExternal does not come into it - the meter does not exist on
    // the other side.
    st.gridExchangeW = -toGrid;
    st.caps.gridMeter = true;
  }

  // --- Battery -------------------------------------------------------------
  // The field names differ per protocol: SOC, BattSOC and BatteryPercentage all
  // occur, all in percent.
  float soc = 0.0f;
  if (num(kOigSoc, &soc)) {
    st.socPct = soc;
    st.haveBattery = true;
    st.caps.battery = true;
    float charge = 0.0f;
    if (num(kOigCharge, &charge)) {
      // ChargePower positive is charging; the panel's convention is positive =
      // discharging.
      st.batW = -charge;
    }
    float volt = 0.0f;
    if (num(kOigBatteryVoltage, &volt)) {
      st.batV = volt;
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

  Serial.printf("OIG: DC %.0f W | AC %.0f W | heute %.2f kWh gesamt %.2f kWh | "
                "%d Felder | Haus%s Netz%s Akku%s\n",
                (double)dcPower, (double)acPower,
                (double)(st.dayGenWh / 1000.0f),
                (double)(st.totalGenWh / 1000.0f), (int)s_bodyLen,
                st.caps.houseMeter ? "j" : "n", st.caps.gridMeter ? "j" : "n",
                st.caps.battery ? "j" : "n");
}

bool OigDriver::connected() const { return deviceState().connected; }