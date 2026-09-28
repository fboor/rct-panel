// RCT Power "Serial Communication Protocol" client (TCP).
//
// Direct port of the RctParser from the Energy2Shelly_ESP project -
// Apache License 2.0. See NOTICE for details and copyright.
//
// Protocol behaviour (quoted from the ported source):
//   The device behaves like a shared bus: every connected client also sees the
//   frames of every other client. Frame order is therefore arbitrary. We treat
//   the connection as a stream:
//    - send a READ for every value we track each poll,
//    - consume the stream, accepting RESPONSE frames for any tracked OID and
//      ignoring everything else (CRC-damaged frames, WRITEs, other clients'
//      strings and responses),
//    - never drop the connection because a frame does not match,
//    - drop it only when the socket died, and reconnect on the next poll,
//    - per-slot: keep the last good value, so a partially responsive device
//      still feeds the panel instead of dropping it to zero.
//
// SPDX-License-Identifier: Apache-2.0
#include "RctClient.h"

#include <WiFi.h>

#include "../config/Configuration.h"
#include "RctTypes.h"

#define RCT_RX_TIMEOUT_MS 2000    // per-frame receive window
#define RCT_CYCLE_TIMEOUT_MS 4000 // total per-poll collection budget

static WiFiClient rctClient;

// ---------------------------------------------------------------------------
// Protocol helpers
// ---------------------------------------------------------------------------

// CRC16 as implemented by the rctclient reference (see rctclient.utils.CRC16).
static uint16_t rctCrc16(const uint8_t *data, size_t len) {
  uint32_t crcsum = 0xFFFF;
  const uint32_t polynom = 0x1021;
  size_t paddedLen = len + (len & 0x01); // append 0x00 if length is odd

  for (size_t i = 0; i < paddedLen; i++) {
    uint8_t byte = (i < len) ? data[i] : 0x00;
    crcsum ^= ((uint32_t)byte) << 8;
    for (int j = 0; j < 8; j++) {
      crcsum <<= 1;
      if (crcsum & 0x7FFF0000) {
        crcsum = (crcsum & 0x0000FFFF) ^ polynom;
      }
    }
  }
  return (uint16_t)(crcsum & 0xFFFF);
}

// Build and send a READ frame for an OID.
static bool rctSendRead(uint32_t oid) {
  uint8_t frame[8];
  frame[0] = 0x01; // READ
  frame[1] = 0x04; // length: 4 OID bytes
  frame[2] = (uint8_t)(oid >> 24);
  frame[3] = (uint8_t)(oid >> 16);
  frame[4] = (uint8_t)(oid >> 8);
  frame[5] = (uint8_t)(oid);

  uint16_t crc = rctCrc16(frame, 6);
  frame[6] = (uint8_t)(crc >> 8);
  frame[7] = (uint8_t)(crc & 0xFF);

  uint8_t out[18]; // 2b + 8 bytes, each possibly escaped by 0x2d
  size_t oi = 0;
  out[oi++] = 0x2b;
  for (size_t i = 0; i < sizeof(frame); i++) {
    if (frame[i] == 0x2b || frame[i] == 0x2d) {
      out[oi++] = 0x2d;
    }
    out[oi++] = frame[i];
  }
  return rctClient.write(out, oi) == oi;
}

// Incremental receive state machine. De-escaped bytes accumulate in rctRxBuf
// starting with the 0x2b start token. 64 bytes fit the largest frames the
// device can send on a shared connection (other clients' WRITEs and string
// payloads can exceed the 13 bytes of a grid value).
#define RCT_RX_BUF_SIZE 64
static uint8_t rctRxBuf[RCT_RX_BUF_SIZE];
static size_t rctRxLen = 0;
static bool rctRxEscaping = false;
static bool rctRxComplete = false;
static size_t rctRxTotal = 0;

static void rctProcessByte(uint8_t c) {
  if (rctRxLen == 0) {
    if (c == 0x2b) {
      rctRxBuf[0] = c;
      rctRxLen = 1;
      rctRxEscaping = false;
      rctRxComplete = false;
      rctRxTotal = 0;
    }
    return;
  }

  if (rctRxEscaping) {
    rctRxEscaping = false;
  } else if (c == 0x2d) {
    rctRxEscaping = true;
    return;
  }

  rctRxBuf[rctRxLen++] = c;

  if (rctRxLen == 3) {
    // header complete: 2b <command> <length>; length counts OID + payload
    rctRxTotal = 5 + rctRxBuf[2]; // 2b + cmd + len + oid + payload + crc
  }

  if (rctRxTotal > 0 && rctRxLen >= rctRxTotal) {
    rctRxComplete = true;
    return;
  }

  // Unexpectedly large frame (e.g. a string payload we never request):
  // drop it and resync on the next start token.
  if (rctRxLen >= RCT_RX_BUF_SIZE) {
    rctRxLen = 0;
    rctRxEscaping = false;
  }
}

enum RCT_RX : int { RCT_RX_OK = 0, RCT_RX_TIMEOUT, RCT_RX_CRC };

// Wait for and validate one response frame. Returns RCT_RX_OK on success,
// RCT_RX_TIMEOUT when no frame arrived within the receive window (the caller
// can check rctClient.connected() to see whether the peer closed the
// connection) and RCT_RX_CRC when a frame arrived but its checksum does not
// match. Resets the receive state in all cases.
static int rctReceiveFrame(uint8_t &command, uint32_t &oid, uint8_t *payload,
                           size_t payloadCapacity, size_t &payloadLen) {
  unsigned long startMillisHere = millis();
  while (millis() - startMillisHere < RCT_RX_TIMEOUT_MS) {
    while (rctClient.available()) {
      rctProcessByte(rctClient.read());
      if (rctRxComplete) {
        break;
      }
    }
    if (rctRxComplete) {
      break;
    }
    if (!rctClient.connected()) {
      rctRxLen = 0;
      rctRxEscaping = false;
      rctRxComplete = false;
      rctRxTotal = 0;
      return RCT_RX_TIMEOUT;
    }
    delay(1);
  }

  if (!rctRxComplete) {
    rctRxLen = 0;
    rctRxEscaping = false;
    rctRxComplete = false;
    rctRxTotal = 0;
    return RCT_RX_TIMEOUT;
  }

  // CRC covers everything after the start token, up to the checksum bytes
  uint16_t calc = rctCrc16(&rctRxBuf[1], rctRxTotal - 3);
  uint16_t recv = ((uint16_t)rctRxBuf[rctRxTotal - 2] << 8) | rctRxBuf[rctRxTotal - 1];
  if (calc != recv) {
    Serial.println("RCT: CRC mismatch, skipping frame");
    rctRxLen = 0;
    rctRxEscaping = false;
    rctRxComplete = false;
    rctRxTotal = 0;
    return RCT_RX_CRC;
  }

  command = rctRxBuf[1];
  oid = ((uint32_t)rctRxBuf[3] << 24) | ((uint32_t)rctRxBuf[4] << 16) |
        ((uint32_t)rctRxBuf[5] << 8) | rctRxBuf[6];
  payloadLen = rctRxBuf[2] - 4;

  // Copy only what fits the caller's buffer; oversized frames are filtered
  // out by the caller via payloadLen anyway.
  if (payload != nullptr && payloadLen > 0 && payloadLen <= payloadCapacity) {
    memcpy(payload, &rctRxBuf[7], payloadLen);
  }

  rctRxLen = 0;
  rctRxEscaping = false;
  rctRxComplete = false;
  rctRxTotal = 0;

  return RCT_RX_OK;
}

// Decode a big-endian 4-byte IEEE-754 float (reference: rctclient.decode_value).
static float rctDecodeFloat(const uint8_t *p) {
  uint32_t i = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | p[3];
  float f;
  memcpy(&f, &i, sizeof(f));
  return f;
}

// ---------------------------------------------------------------------------
// Values we track. Slot order groups contiguous slices so the publish step
// can memcpy whole arrays.
// ---------------------------------------------------------------------------
enum RCT_SLOT {
  RCT_SLOT_P0 = 0, // g_sync.p_ac_sc[0]      grid power L1 [W]
  RCT_SLOT_P1,     // g_sync.p_ac_sc[1]      grid power L2 [W]
  RCT_SLOT_P2,     // g_sync.p_ac_sc[2]      grid power L3 [W]
  RCT_SLOT_EFEED,  // energy.e_grid_feed_total raw, already Wh
  RCT_SLOT_ELOAD,  // energy.e_grid_load_total raw, already Wh
  RCT_SLOT_V0,     // rb485.u_l_grid[0]      grid voltage L1 [V]
  RCT_SLOT_V1,     // rb485.u_l_grid[1]      grid voltage L2 [V]
  RCT_SLOT_V2,     // rb485.u_l_grid[2]      grid voltage L3 [V]
  RCT_SLOT_F0,     // rb485.f_grid[0]        grid frequency L1 [Hz]
  RCT_SLOT_F1,     // rb485.f_grid[1]        grid frequency L2 [Hz]
  RCT_SLOT_F2,     // rb485.f_grid[2]        grid frequency L3 [Hz]
  RCT_SLOT_L0,     // g_sync.p_ac_load[0]    household load L1 [W]
  RCT_SLOT_L1,     // g_sync.p_ac_load[1]    household load L2 [W]
  RCT_SLOT_L2,     // g_sync.p_ac_load[2]    household load L3 [W]
  RCT_SLOT_PV0,    // dc_conv.dc_conv_struct[0].p_dc_lp  solar generator A [W]
  RCT_SLOT_PV1,    // dc_conv.dc_conv_struct[1].p_dc_lp  solar generator B [W]
  RCT_SLOT_S0,     // io_board.s0_external_power           S0 meter power [W]
  RCT_SLOT_SOC,    // battery.soc  [%]
  RCT_SLOT_IBAT,   // battery.current [A]
  RCT_SLOT_UBAT,   // battery.voltage [V]
  RCT_SLOT_PBAT,   // g_sync.p_acc_lp battery power [W], + = charge
  RCT_SLOT_DC0,    // energy.e_dc_day[0]        solar generator A day [Wh]
  RCT_SLOT_DC1,    // energy.e_dc_day[1]        solar generator B day [Wh]
  RCT_SLOT_ELOADDAY, // energy.e_load_day       household day [Wh]
  RCT_SLOT_EFEEDDAY, // energy.e_grid_feed_day  feed-in day [Wh]
  RCT_SLOT_EGRIDLOADDAY, // energy.e_grid_load_day grid draw day [Wh]
  RCT_NUM_SLOTS
};

// Object IDs (registry: https://rctclient.readthedocs.io/en/latest/)
static const uint32_t rctOids[RCT_NUM_SLOTS] = {
    0x27BE51D9, // grid power L1 (W)
    0xF5584F90, // grid power L2 (W)
    0xB221BCFA, // grid power L3 (W)
    0x44D4C533, // feed-in energy (Wh)
    0x62FBE7DC, // load energy (Wh)
    0x93F976AB, // grid voltage L1 (V)
    0x7A9091EA, // grid voltage L2 (V)
    0x21EE7CBB, // grid voltage L3 (V)
    0x9558AD8A, // grid frequency L1 (Hz)
    0xFAE429C5, // grid frequency L2 (Hz)
    0x0104EB6A, // grid frequency L3 (Hz)
    0x3A39CA2,  // household load L1 (W)
    0x2788928C, // household load L2 (W)
    0xF0B436DD, // household load L3 (W)
    0xDB11855B, // solar generator A power (W)
    0xCB5D21B,  // solar generator B power (W)
    0xE96F1844, // S0 meter external power (W)
    0x959930BF, // battery SOC (%)
    0x21961B58, // battery current (A)
    0x65EED11B, // battery voltage (V)
    0x400F015B, // battery power (W), positive = charging
    0x2AE703F2, // solar generator A day energy (Wh)
    0xFBF3CE97, // solar generator B day energy (Wh)
    0x2F3C1D7D, // household day energy (Wh)
    0x3C87C4F5, // day energy grid feed-in (Wh)
    0x867DEF7D, // day energy grid load (Wh)
};

static int rctSlotForOid(uint32_t oid) {
  for (int i = 0; i < RCT_NUM_SLOTS; i++) {
    if (rctOids[i] == oid) {
      return i;
    }
  }
  return -1;
}

// The official RCT Power app sends this frame once after connecting to switch
// the device into request/response ("COM") mode:
//   0x2b 0x3c 0xe1 = start token, EXTENSION command, single payload byte.
// Most devices work without it, but some firmware stays silent for plain
// READs until it was received. No response is expected; any bytes the device
// sends back are drained so the following READ frames line up.
static void rctSendExtension() {
  static const uint8_t ext[] = {0x2b, 0x3c, 0xe1};
  rctClient.write(ext, sizeof(ext));
  unsigned long startMillisHere = millis();
  while (millis() - startMillisHere < 300) {
    while (rctClient.available()) {
      rctClient.read();
    }
    delay(1);
  }
}

RctSnapshot rctState = {};

// Public poll entry point (mirrors parseRCT in the ported project).
void rctParse() {
  if (!rctClient.connected()) {
    int port = atol(rct_port);
    if (port <= 0) {
      port = 8899;
    }
    Serial.printf("RCT: connecting to %s:%d ...\n", rct_host, port);
    if (!rctClient.connect(rct_host, port)) {
      Serial.println("RCT: connect failed");
      rctState.connected = false;
      return;
    }
    rctState.connected = true;
    delay(20);
    rctSendExtension();
  }

  // Ask for every value we track.
  for (int i = 0; i < RCT_NUM_SLOTS; i++) {
    rctSendRead(rctOids[i]);
  }

  // Consume the stream for this poll; keep last-good values per slot.
  static float rctCur[RCT_NUM_SLOTS];
  uint32_t freshMask = 0;
  const uint32_t allSlots = (1u << RCT_NUM_SLOTS) - 1;
  unsigned long deadline = millis() + RCT_CYCLE_TIMEOUT_MS;
  bool streamQuiet = false;
  while (freshMask != allSlots && !streamQuiet &&
         (int32_t)(millis() - deadline) < 0) {
    uint8_t command = 0;
    uint8_t payload[64];
    size_t payloadLen = 0;
    uint32_t respOid = 0;
    int rc = rctReceiveFrame(command, respOid, payload, sizeof(payload), payloadLen);
    if (rc == RCT_RX_TIMEOUT) {
      if (!rctClient.connected()) {
        rctClient.stop();
        rctState.connected = false;
      }
      streamQuiet = true;
      break;
    }
    if (rc == RCT_RX_CRC) {
      continue;
    }
    if (command == 0x05 && payloadLen == 4) {
      int slot = rctSlotForOid(respOid);
      if (slot >= 0) {
        rctCur[slot] = rctDecodeFloat(payload);
        freshMask |= (1u << slot);
        rctState.haveData = true;
        if (slot == RCT_SLOT_SOC) {
          rctState.haveBattery = true; // SOC only answers on battery devices
        }
        rctState.lastUpdateMs = millis();
      }
    }
  }

  if (!rctState.haveData) {
    Serial.println(F("RCT: no data yet (device unresponsive or only answers "
                     "while the app is connected)"));
    return;
  }

  // Publish the current best-known values to the snapshot.
  const float *powers = &rctCur[RCT_SLOT_P0];
  const float *voltages = &rctCur[RCT_SLOT_V0];
  const float *frequencies = &rctCur[RCT_SLOT_F0];
  const float *loads = &rctCur[RCT_SLOT_L0];
  memcpy(rctState.gridPower, powers, sizeof(rctState.gridPower));
  memcpy(rctState.gridVoltage, voltages, sizeof(rctState.gridVoltage));
  memcpy(rctState.gridFrequency, frequencies, sizeof(rctState.gridFrequency));
  memcpy(rctState.loadPower, loads, sizeof(rctState.loadPower));
  rctState.feedInEnergyWh = rctCur[RCT_SLOT_EFEED];
  rctState.loadEnergyWh = rctCur[RCT_SLOT_ELOAD];

  rctState.pvPower[0] = rctCur[RCT_SLOT_PV0];
  rctState.pvPower[1] = rctCur[RCT_SLOT_PV1];
  rctState.s0Power = rctCur[RCT_SLOT_S0];
  rctState.batterySoc = rctCur[RCT_SLOT_SOC];
  rctState.batteryCurrent = rctCur[RCT_SLOT_IBAT];
  rctState.batteryVoltage = rctCur[RCT_SLOT_UBAT];
  rctState.batteryPower = rctCur[RCT_SLOT_PBAT];

  rctState.dayPvWh = rctCur[RCT_SLOT_DC0] + rctCur[RCT_SLOT_DC1];
  rctState.dayFeedInWh = rctCur[RCT_SLOT_EFEEDDAY];
  rctState.dayLoadWh = rctCur[RCT_SLOT_ELOADDAY];
  rctState.dayGridLoadWh = rctCur[RCT_SLOT_EGRIDLOADDAY];

  int freshCount = 0;
  for (uint32_t m = freshMask; m; m &= m - 1) {
    freshCount++;
  }
  Serial.printf("RCT: grid %.0f/%.0f/%.0f W | load %.0f/%.0f/%.0f W | PV %.2f kW"
                " | bat %.0f%% %.2f kW (%d/%d fresh)\n",
                rctState.gridPower[0], rctState.gridPower[1],
                rctState.gridPower[2], rctState.loadPower[0],
                rctState.loadPower[1], rctState.loadPower[2],
                (rctState.pvPower[0] + rctState.pvPower[1] +
                 rctState.s0Power) / 1000.0f,
                rctState.batterySoc, rctState.batteryPower / 1000.0f,
                freshCount, RCT_NUM_SLOTS);
}