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
#include <WiFi.h>

#include "../Diag.h"
#include "../device/Device.h"
#include "../device/RctDriver.h"
#include "../device/Rules.h"
#include "RctCrc.h"

#define RCT_RX_TIMEOUT_MS 2000 // per-frame receive window
// How long a device that has already answered may stay silent before the cycle is
// called done. The device answers a burst of reads and then nothing, so this is
// the pause that ends a poll - not a deadline it has to meet.
#define RCT_QUIET_MS 300
#define RCT_INFO_POLL_MS 10000 // device info group poll cadence
// Bounded connect() so an unreachable host can not freeze the UI task for
// long; offline reconnects are throttled independently of the poll cadence.
#define RCT_CONNECT_TIMEOUT_MS 400  // hard ceiling for one frozen frame
#define RCT_CONNECT_RETRY_MS 5000   // dead host: retry often, stall briefly

// Longest gap the S0 energy integration bridges. The fast group is polled every
// RCT_POLL_MS (10 s), so a normal step is 10 s. A much longer one means the
// device was not answering and the samples in between do not exist - averaging
// across such a gap would credit the generator with energy nobody measured.
#define RCT_S0_MAX_STEP_MS 30000

// The link, handed over by Device.cpp. Set in begin(); a poll without one would
// be a wiring bug rather than a runtime condition.
static DeviceTransport *s_link = nullptr;
static DeviceConfig s_cfg;

// ---------------------------------------------------------------------------
// Protocol helpers
// ---------------------------------------------------------------------------

// The checksum of a frame, in its own header so the check values can be
// verified on the build machine (tools/crc_test).
static inline uint16_t rctCrc16(const uint8_t *data, size_t len) {
  return rctcrc::compute(data, len);
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
  return s_link->write(out, oi) == oi;
}

// Incremental receive state machine. De-escaped bytes accumulate in rctRxBuf
// starting with the 0x2b start token. 128 bytes comfortably fit the largest
// frames we request (device name / version strings) plus other clients'
// chatter on the shared bus.
#define RCT_RX_BUF_SIZE 128
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

// Hook into the wait loop. Collecting a cycle means sitting in rctReceiveFrame()
// while the device answers, and LVGL runs in the same FreeRTOS task as this file -
// without a way to render in between, the panel stands still for as long as the
// wait lasts. main.cpp installs displayLooper()+lv_tick_inc here, which is the
// only safe place: it is the same task, so dev is still only ever touched
// from one context.
static void (*rctYieldHook)() = nullptr;

enum RCT_RX : int { RCT_RX_OK = 0, RCT_RX_TIMEOUT, RCT_RX_CRC };

// Wait for and validate one response frame. Returns RCT_RX_OK on success,
// RCT_RX_TIMEOUT when no frame arrived within the receive window (the caller
// can ask the transport whether the peer is still there to see whether it
// closed the connection) and RCT_RX_CRC when a frame arrived but its
// checksum does not match. Resets the receive state in all cases.
//
// quietMs is the shorter window that applies once the device has answered
// something in this cycle and then stopped sending; 0 disables it. The device
// answers a whole burst of reads back to back, so silence means it has nothing
// left to say - and waiting out the full window here is what froze the panel
// for RCT_CYCLE_TIMEOUT_MS on every poll whose OID set came back incomplete
// (a value the device does not know never arrives, and its silence was read as
// "still coming").
static int rctReceiveFrame(uint8_t &command, uint32_t &oid, uint8_t *payload,
                           size_t payloadCapacity, size_t &payloadLen,
                           uint32_t quietMs) {
  unsigned long startMillisHere = millis();
  unsigned long lastByteMs = startMillisHere;
  bool gotAny = false;
  while (millis() - startMillisHere < RCT_RX_TIMEOUT_MS) {
    while (s_link->available()) {
      rctProcessByte((uint8_t)s_link->read());
      gotAny = true;
      lastByteMs = millis();
      if (rctRxComplete) {
        break;
      }
    }
    if (rctRxComplete) {
      break;
    }
    if (!s_link->peerOpen()) {
      rctRxLen = 0;
      rctRxEscaping = false;
      rctRxComplete = false;
      rctRxTotal = 0;
      return RCT_RX_TIMEOUT;
    }
    if (quietMs > 0 && gotAny &&
        (int32_t)(millis() - lastByteMs) >= (int32_t)quietMs) {
      rctRxLen = 0;
      rctRxEscaping = false;
      rctRxComplete = false;
      rctRxTotal = 0;
      return RCT_RX_TIMEOUT;
    }
    if (rctYieldHook) {
      deviceYieldHook(); // keep the panel rendering while we wait for the frame
    } else {
      delay(1);
    }
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

// Decode a big-endian integer payload. The response width depends on the
// object's storage type: FLOAT/INT32/UINT32 reply with 4 bytes, UINT16 (e.g.
// prim_sm.island_flag) with 2 bytes.
static uint32_t rctDecodeInt(const uint8_t *p, size_t n) {
  uint32_t v = 0;
  for (size_t i = 0; i < n && i < 4; i++) {
    v = (v << 8) | p[i];
  }
  return v;
}

// ---------------------------------------------------------------------------
// Values we track. Slot order groups contiguous slices so the publish step
// can memcpy whole arrays.
// ---------------------------------------------------------------------------
enum RCT_SLOT {
  RCT_SLOT_P0 = 0, // g_sync.p_ac_sc[0]      grid power L1 [W]
  RCT_SLOT_P1,     // g_sync.p_ac_sc[1]      grid power L2 [W]
  RCT_SLOT_P2,     // g_sync.p_ac_sc[2]      grid power L3 [W]
  RCT_SLOT_PGRIDSUM, // g_sync.p_ac_grid_sum_lp  grid exchange total [W], + = Bezug
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
  // "Energie" page: month / year / lifetime accumulators. They are static
  // counters, but they ride along in the fast group so the page never shows a
  // gap right after a period switch.
  RCT_SLOT_DCMONTH0, // energy.e_dc_month[0]  solar generator A month [Wh]
  RCT_SLOT_DCMONTH1, // energy.e_dc_month[1]  solar generator B month [Wh]
  RCT_SLOT_DCYEAR0,  // energy.e_dc_year[0]   solar generator A year [Wh]
  RCT_SLOT_DCYEAR1,  // energy.e_dc_year[1]   solar generator B year [Wh]
  RCT_SLOT_DCTOTAL0, // energy.e_dc_total[0]  solar generator A lifetime [Wh]
  RCT_SLOT_DCTOTAL1, // energy.e_dc_total[1]  solar generator B lifetime [Wh]
  RCT_SLOT_LOADMONTH, // energy.e_load_month  household month [Wh]
  RCT_SLOT_LOADYEAR,  // energy.e_load_year   household year [Wh]
  RCT_SLOT_LOADTOTAL, // energy.e_load_total  household lifetime [Wh]
  RCT_SLOT_FEEDMONTH, // energy.e_grid_feed_month  feed-in month [Wh]
  RCT_SLOT_FEEDYEAR,  // energy.e_grid_feed_year   feed-in year [Wh]
  RCT_SLOT_GRIDMONTH, // energy.e_grid_load_month grid draw month [Wh]
  RCT_SLOT_GRIDYEAR,  // energy.e_grid_load_year  grid draw year [Wh]
  // External energy (the S0 generator), the device's own counters. The _sum
  // variants are asked for: the registry lists both e_ext_* and e_ext_*_sum
  // (e.g. e_ext_day_sum, idx 43), and the sum is the one that aggregates the
  // S0 inputs rather than a single channel. Both are polled and logged raw so
  // the difference is measurable instead of assumed.
  RCT_SLOT_EXTDAY,   // energy.e_ext_day_sum      external day [Wh]
  RCT_SLOT_EXTMONTH, // energy.e_ext_month_sum    external month [Wh]
  RCT_SLOT_EXTYEAR,  // energy.e_ext_year_sum     external year [Wh]
  RCT_SLOT_EXTTOTAL, // energy.e_ext_total_sum    external lifetime [Wh]
  RCT_SLOT_EXTDAYP,  // energy.e_ext_day          external day, unsummed [Wh]
  RCT_SLOT_EXTMONP,  // energy.e_ext_month        external month, unsummed [Wh]
  // Service page (fast group): battery status + fault bitfields.
  RCT_SLOT_BATSTATUS, // battery.bat_status      status bitfield (INT32)
  RCT_SLOT_FLT0,      // fault[0].flt            fault bits  0-31 (UINT32)
  RCT_SLOT_FLT1,      // fault[1].flt            fault bits 32-63 (UINT32)
  RCT_SLOT_FLT2,      // fault[2].flt            fault bits 64-95 (UINT32)
  RCT_SLOT_FLT3,      // fault[3].flt            fault bits 96-127 (UINT32)
  // Device info group: name / software version / temperatures / calibration
  // date / cycles / SOH / island flag. RCT_SLOT_DEVNAME also marks the
  // boundary between the fast and the info poll groups.
  RCT_SLOT_DEVNAME, // android_description        device name (STRING)
  RCT_SLOT_SVN,     // svnversion                 control software version (STRING)
  RCT_SLOT_CORET,   // db.core_temp               core temperature [°C]
  RCT_SLOT_BTEMP,   // battery.temperature        battery temperature [°C]
  RCT_SLOT_HTEMP,   // db.temp1                   heat sink temperature [°C]
  RCT_SLOT_CALIB,   // power_mng.bat_next_calib_date  next calibration [unix s]
  RCT_SLOT_CYCLES,  // battery.cycles             charge/discharge cycles
  RCT_SLOT_SOH,     // battery.soh                state of health [%]
  RCT_SLOT_ISLAND,  // prim_sm.island_flag        island mode flag (UINT16)
  RCT_NUM_SLOTS
};

// Object IDs (registry: https://rctclient.readthedocs.io/en/latest/)
static const uint32_t rctOids[RCT_NUM_SLOTS] = {
    0x27BE51D9, // grid power L1 (W)
    0xF5584F90, // grid power L2 (W)
    0xB221BCFA, // grid power L3 (W)
    0x91617C58, // grid exchange total (W), + = grid import
    0x44D4C533, // e_grid_feed_total  lifetime feed-in energy (Wh)
    0x62FBE7DC, // e_grid_load_total  lifetime grid draw energy (Wh)
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
    0x81AE960B, // solar generator A month energy (Wh)
    0x7AB9B045, // solar generator B month energy (Wh)
    0xAF64D0FE, // solar generator A year energy (Wh)
    0xBD55D796, // solar generator B year energy (Wh)
    0xFC724A9E, // solar generator A lifetime energy (Wh)
    0x68EEFD3D, // solar generator B lifetime energy (Wh)
    0xF0BE6429, // household month energy (Wh)
    0xC7D3B479, // household year energy (Wh)
    0xEFF4B537, // household lifetime energy (Wh)
    0x65B624AB, // grid feed-in month energy (Wh)
    0x26EFFC2F, // grid feed-in year energy (Wh)
    0x126ABC86, // grid load month energy (Wh)
    0xDE17F021, // grid load year energy (Wh)
    0xC588B75,  // external day energy, summed S0 inputs (Wh)
    0x6FF4BD55, // external month energy, summed S0 inputs (Wh)
    0x3A9D2680, // external year energy, summed S0 inputs (Wh)
    0xF28E2E1,  // external lifetime energy, summed S0 inputs (Wh)
    0xB9A026F9, // external day energy, unsummed (Wh)
    0x31A6110,  // external month energy, unsummed (Wh)
    0x70A2AF4F, // battery status bitfield (INT32)
    0x37F9D5CA, // fault[0].flt  fault bits 0-31 (UINT32)
    0x234B4736, // fault[1].flt  fault bits 32-63 (UINT32)
    0x3B7FCD47, // fault[2].flt  fault bits 64-95 (UINT32)
    0x7F813D73, // fault[3].flt  fault bits 96-127 (UINT32)
    0xEBC62737, // device name (string)
    0xDDD1C2D0, // control software version (string)
    0xC24E85D0, // core temperature (°C)
    0x902AFAFB, // battery temperature (°C)
    0xF79D41D9, // heat sink temperature (°C)
    0xB6623608, // next battery calibration (unix time)
    0xC0DF2978, // battery cycles
    0x381B8BF9, // battery SOH (%)
    0x3623D82A, // island mode flag
};

static int rctSlotForOid(uint32_t oid) {
  for (int i = 0; i < RCT_NUM_SLOTS; i++) {
    if (rctOids[i] == oid) {
      return i;
    }
  }
  return -1;
}

// How each tracked object is decoded. The first RCT_SLOT_DEVNAME slots are
// all plain FLOAT grid/phase values; the slow device-info group is a mix.
enum RCT_DTYPE : uint8_t { RCT_DT_FLOAT = 0, RCT_DT_INT, RCT_DT_STRING };

static uint8_t rctTypeOf(int slot) {
  // Non-float fast-group slots: battery status + fault bitfields decode as
  // raw integers (exact 32-bit values; float would lose bits above 2^24).
  if (slot >= RCT_SLOT_BATSTATUS && slot <= RCT_SLOT_FLT3) {
    return RCT_DT_INT;
  }
  if (slot < RCT_SLOT_DEVNAME) {
    return RCT_DT_FLOAT;
  }
  static const uint8_t t[RCT_NUM_SLOTS - RCT_SLOT_DEVNAME] = {
      RCT_DT_STRING, // RCT_SLOT_DEVNAME
      RCT_DT_STRING, // RCT_SLOT_SVN
      RCT_DT_FLOAT,  // RCT_SLOT_CORET
      RCT_DT_FLOAT,  // RCT_SLOT_BTEMP
      RCT_DT_FLOAT,  // RCT_SLOT_HTEMP
      RCT_DT_INT,    // RCT_SLOT_CALIB  (UINT32)
      RCT_DT_INT,    // RCT_SLOT_CYCLES (INT32)
      RCT_DT_FLOAT,  // RCT_SLOT_SOH
      RCT_DT_INT,    // RCT_SLOT_ISLAND (UINT16)
  };
  return t[slot - RCT_SLOT_DEVNAME];
}

// String responses are NUL-terminated ASCII blobs; mirror rctclient's decode
// (cut at the first NUL) and truncate to the snapshot buffer.
#define RCT_STR_MAX 48
static char rctDevName[RCT_STR_MAX] = {0};
static char rctFwVersion[RCT_STR_MAX] = {0};

static void rctDecodeString(const uint8_t *p, size_t n, char *dst) {
  size_t c = 0;
  while (c < n && c < RCT_STR_MAX - 1 && p[c] != 0x00) {
    dst[c] = (char)p[c];
    c++;
  }
  dst[c] = '\0';
}

// The official RCT Power app sends this frame once after connecting to switch
// the device into request/response ("COM") mode:
//   0x2b 0x3c 0xe1 = start token, EXTENSION command, single payload byte.
// Most devices work without it, but some firmware stays silent for plain
// READs until it was received. No response is expected; any bytes the device
// sends back are drained so the following READ frames line up.
// Drain window after the extension frame. This runs on every reconnect, and a
// plain delay(1) here froze the GUI for the full 300 ms - not a long stall, but
// it lands right where the user is most likely to be switching pages.
static void rctDrain(uint32_t ms) {
  const unsigned long startMillisHere = millis();
  diagPhase("rct.drain");
  while (millis() - startMillisHere < ms) {
    while (s_link->available()) {
      s_link->read();
    }
    deviceYieldHook(); // see rctReceiveFrame(): the panel must keep drawing
    delay(1);
  }
}

static void rctSendExtension() {
  static const uint8_t ext[] = {0x2b, 0x3c, 0xe1};
  s_link->write(ext, sizeof(ext));
  rctDrain(300);
}

// The driver's own handle on the state. Everything outside src/rct/ reads it
// through dev; only a driver writes it, and only from the task that
// also runs LVGL - which is why this is a reference and not a copy.
static DeviceState &dev = deviceStateMutable();

// One-time note when the current sign had to be flipped, so the reconciliation
// is visible in the log instead of being a silent guess.
static bool rctCurrentFlipLogged = false;

// One collection run (mirrors parseRCT in the ported project). Called through
// DeviceDriver::poll(); the file-static state stays file-static, because there
// is exactly one RCT per panel.
static void rctPoll(uint32_t budgetMs) {
  // Once per boot: how this device's meters read, which is what makes the rules
  // in src/device/Rules.h right here. All three are false-then-true for the
  // RCT, and each of them was a correction that used to be written into the
  // display code at five places:
  //   loadMeterSeesExternal    false - its load meter reads the demand already
  //                            minus the S0 generator, so the household is
  //                            meter + external power
  //   genCounterSeesExternal   false - the e_dc_* family only counts the two DC
  //                            inputs, so the external counters are added
  //                            separately, to generation and to consumption
  //   feedCounterNegative      true  - the feed-in counters arrive negative,
  //                            the magnitude is what went in
  static bool semanticsSet = false;
  if (!semanticsSet) {
    semanticsSet = true;
    const DeviceSemantics rct = {false, false, true};
    deviceSetSemantics(rct);
    Serial.printf("RCT: Zaehlerregeln - Lastzaehler %s extern, "
                  "Erzeugungszaehler %s extern, Einspeisezahler %s negativ\n",
                  rct.loadMeterSeesExternal ? "sieht" : "sieht nicht",
                  rct.genCounterSeesExternal ? "sieht" : "sieht nicht",
                  rct.feedCounterNegative ? "kommen" : "kommen nicht");
  }

  if (!s_link->peerOpen()) {
    // No link: skip the attempt so the UI (same task) is never frozen by a
    // blocking connect. "not connected" is signaled through dev.
    if (WiFi.status() != WL_CONNECTED) {
      dev.connected = false;
      return;
    }

    // A connect attempt is a blocking lwIP call: the yield hook cannot run
    // inside it, so RCT_CONNECT_TIMEOUT_MS is the hard ceiling for a frozen
    // panel. Against a host on the local network the handshake completes in
    // well under 100 ms, so 400 ms costs nothing and turns a 2 s freeze into
    // a barely noticeable one. Recovery is cheap because the retry cadence,
    // not the timeout, governs how often we try.
    static uint32_t lastAtt = 0;
    const uint32_t nowAtt = millis();
    if ((int32_t)(nowAtt - lastAtt) >= (int32_t)RCT_CONNECT_RETRY_MS) {
      lastAtt = nowAtt;
      int port = atol(s_cfg.port);
      if (port <= 0) {
        port = 8899; // the RCT Power's standard port, if the field is empty
      }
      Serial.printf("RCT: connecting to %s:%d ...\n", s_cfg.host, port);
      // connect() is the one blocking lwIP call the yield hook cannot get into,
      // so it gets its own phase name: a panel that wedges on a connect is a
      // different fault than one that wedges on a poll, and the two need
      // different fixes.
      diagPhase("rct.connect");
      if (!s_link->open(s_cfg.host, s_cfg.port, RCT_CONNECT_TIMEOUT_MS)) {
        // A failed connect can leave the socket half-open; the observation on
        // the panel was 21 of them stacked up, which eventually exhausted the
        // peer's listen backlog and made every later connect fail too.
        s_link->close();
        Serial.println("RCT: connect failed");
        dev.connected = false;
        return;
      }
      dev.connected = true;
      deviceYieldHook();
      delay(20);
      rctSendExtension();
    } else {
      dev.connected = false;
      return;
    }
  }

  // Ask for every value we track. The device-info group runs on its own
  // cadence (RCT_INFO_POLL_MS) so the two groups can be tuned independently
  // without changing the fast group's bus share.
  static uint32_t lastInfoPoll = 0;
  const uint32_t nowMs = millis();
  const bool pollInfo = (int32_t)(nowMs - lastInfoPoll) >=
                        (int32_t)RCT_INFO_POLL_MS;
  for (int i = 0; i < RCT_NUM_SLOTS; i++) {
    if (i >= RCT_SLOT_DEVNAME && !pollInfo) {
      continue;
    }
    rctSendRead(rctOids[i]);
  }
  if (pollInfo) {
    lastInfoPoll = nowMs;
  }

  // Consume the stream for this poll; keep last-good values per slot. The
  // fast group gates the receive window; info responses are consumed
  // opportunistically whenever they arrive during the same window.
  //
  // This is the phase that blocks: every OID the device does not answer costs
  // the full RCT_RX_TIMEOUT_MS. With 60 slots now polled and the real device
  // answering 45 of them, that is the single longest stretch in the whole loop.
  diagPhase("rct.poll");
  static float rctCur[RCT_NUM_SLOTS];
  static uint32_t rctRaw[RCT_NUM_SLOTS]; // exact ints (calib ts, island ...)
  static uint32_t infoSeen = 0;          // slow slots that answered once
  static bool infoLogged = false;        // one-time bring-up log
  // 64-bit: the fast group has grown past 32 slots, so a 32-bit mask (and the
  // 1u << 32 shift that built its "all answered" mask) no longer works.
  uint64_t freshMask = 0;
  const uint32_t fastSlots = (uint32_t)RCT_SLOT_DEVNAME;
  const uint64_t allFast =
      (fastSlots >= 64) ? ~0ull : ((1ull << fastSlots) - 1ull);
  // The accumulated-energy block, for the one-time bring-up log below.
  // Contiguous energy block: month/year/lifetime accumulators plus the external
  // (S0) counters. The whole block has to answer before the one-time energy log
  // fires, so that log proves every value the "Energie" page shows for that
  // period - not just the ones that happen to have arrived first.
  const uint64_t energySlotsMask =
      ((1ull << (RCT_SLOT_EXTTOTAL - RCT_SLOT_DCMONTH0 + 1)) - 1ull)
      << RCT_SLOT_DCMONTH0;
  unsigned long deadline = millis() + budgetMs;
  bool streamQuiet = false;
  bool gotFrame = false;
  while (freshMask != allFast && !streamQuiet &&
         (int32_t)(millis() - deadline) < 0) {
    uint8_t command = 0;
    uint8_t payload[128];
    size_t payloadLen = 0;
    uint32_t respOid = 0;
    // The first frame gets the full window: after a reconnect the device may need
    // a moment. Every frame after it is only waited for briefly, because a device
    // that has gone quiet has already said everything it knows.
    int rc = rctReceiveFrame(command, respOid, payload, sizeof(payload),
                             payloadLen, gotFrame ? RCT_QUIET_MS : 0);
    if (rc == RCT_RX_OK) {
      gotFrame = true;
    }
    if (rc == RCT_RX_TIMEOUT) {
      if (!s_link->peerOpen()) {
        s_link->close();
        dev.connected = false;
      }
      streamQuiet = true;
      break;
    }
    if (rc == RCT_RX_CRC) {
      continue;
    }
    if (command == 0x05) {
      int slot = rctSlotForOid(respOid);
      if (slot >= 0) {
        switch (rctTypeOf(slot)) {
          case RCT_DT_STRING:
            rctDecodeString(payload, payloadLen,
                            slot == RCT_SLOT_DEVNAME ? rctDevName
                                                     : rctFwVersion);
            break;
          case RCT_DT_INT:
            rctRaw[slot] = rctDecodeInt(payload, payloadLen);
            rctCur[slot] = (float)rctRaw[slot];
            break;
          default:
            if (payloadLen == 4) {
              rctCur[slot] = rctDecodeFloat(payload);
            }
            break;
        }
        dev.haveData = true;
        if (slot == RCT_SLOT_SOC) {
          dev.haveBattery = true; // SOC only answers on battery devices
        }
        dev.lastUpdateMs = millis();
        if (slot >= RCT_SLOT_DEVNAME) {
          infoSeen |= (1u << (slot - RCT_SLOT_DEVNAME));
        } else {
          freshMask |= (1ull << slot);
        }
      }
    }
  }

  if (!dev.haveData) {
    Serial.println(F("RCT: no data yet (device unresponsive or only answers "
                     "while the app is connected)"));
    return;
  }

  // Publish the current best-known values to the snapshot.
  const float *powers = &rctCur[RCT_SLOT_P0];
  const float *voltages = &rctCur[RCT_SLOT_V0];
  const float *frequencies = &rctCur[RCT_SLOT_F0];
  const float *loads = &rctCur[RCT_SLOT_L0];
  memcpy(dev.gridW, powers, sizeof(dev.gridW));
  memcpy(dev.gridV, voltages, sizeof(dev.gridV));
  memcpy(dev.gridHz, frequencies, sizeof(dev.gridHz));
  memcpy(dev.houseW, loads, sizeof(dev.houseW));
  dev.gridExchangeW = rctCur[RCT_SLOT_PGRIDSUM];
  dev.feedInTotalWh = rctCur[RCT_SLOT_EFEED];
  dev.gridDrawTotalWh = rctCur[RCT_SLOT_ELOAD];

  dev.genW[0] = rctCur[RCT_SLOT_PV0];
  dev.genW[1] = rctCur[RCT_SLOT_PV1];
  dev.extW = rctCur[RCT_SLOT_S0];

  // Integrate the S0 external generator's power into an energy total, as an
  // independent check on the device's own e_ext_day_sum counter. The displayed
  // value comes from that counter, not from here: it survives a restart of the
  // panel, this does not. Kept because two numbers that are derived
  // differently should agree - when they do not, one of them is wrong and the
  // log is where that shows up.
  //
  // Trapezoidal over the interval since the last integration: a sample only
  // says how much was produced at that instant, and the generator may be
  // switched off between two polls - averaging the two ends is what keeps a
  // short burst from counting for the whole interval.
  //
  // Only the fresh S0 value is used, and only while it actually answers. A
  // stale zero would otherwise drain the accumulator after the generator stops
  // sending, and a stale non-zero would keep inflating it.
  if (freshMask & (1ull << RCT_SLOT_S0)) {
    const uint32_t nowMs = millis();
    static uint32_t lastS0Ms = 0;
    static float lastS0W = 0.0f;
    if (lastS0Ms != 0) {
      const uint32_t dtMs = nowMs - lastS0Ms;
      // Cap the step: a long outage would otherwise integrate the average
      // across minutes of production the panel never saw.
      if (dtMs > 0 && dtMs <= RCT_S0_MAX_STEP_MS) {
        dev.extEnergyWh +=
            (lastS0W + dev.extW) * 0.5f * (float)dtMs / 3600000.0f;
      }
    }
    lastS0Ms = nowMs;
    lastS0W = dev.extW;
  }
  // SOC/SOH are reported as 0..1 fractions; scale to percent.
  static const auto pct100 = [](float frac) {
    float v = frac * 100.0f;
    return v < 0.0f ? 0.0f : (v > 100.0f ? 100.0f : v);
  };
  dev.socPct = pct100(rctCur[RCT_SLOT_SOC]);
  // battery.current (OID 0x21961B58) is aligned with p_acc_lp by the physics
  // instead of by a hardcoded negation: P = U * I for a battery, so the signs
  // of U*I and P must agree. Whichever of the two is inconsistent gets flipped,
  // and that holds for either firmware convention.
  float current = rctCur[RCT_SLOT_IBAT];
  const float voltage = rctCur[RCT_SLOT_UBAT];
  const float power = rctCur[RCT_SLOT_PBAT];
  if (fabsf(power) > 50.0f && voltage > 1.0f && current != 0.0f) {
    const bool elecNeg = (voltage * current) < 0.0f;
    const bool powerNeg = power < 0.0f;
    if (elecNeg != powerNeg) {
      current = -current;
      if (!rctCurrentFlipLogged) {
        rctCurrentFlipLogged = true;
        Serial.printf("RCT: battery current sign flipped to match power "
                      "(I=%.2f A, U=%.1f V, P=%.0f W)\n",
                      (double)current, (double)voltage, (double)power);
      }
    }
  }
  dev.batA = current;
  dev.batV = voltage;
  dev.batW = power;

  // Service page data (raw, exact values).
  dev.batteryStatus = rctRaw[RCT_SLOT_BATSTATUS];
  for (int k = 0; k < 4; k++) {
    dev.faultBits[k] = rctRaw[RCT_SLOT_FLT0 + k];
  }

  // The two DC inputs only; the S0 external generator is added by the caller
  // from extEnergyWh, because this counter does not see it.
  dev.dayGenWh = rctCur[RCT_SLOT_DC0] + rctCur[RCT_SLOT_DC1];
  dev.dayFeedInWh = rctCur[RCT_SLOT_EFEEDDAY];
  dev.dayHouseWh = rctCur[RCT_SLOT_ELOADDAY];
  dev.dayGridDrawWh = rctCur[RCT_SLOT_EGRIDLOADDAY];

  dev.monthGenWh =
      rctCur[RCT_SLOT_DCMONTH0] + rctCur[RCT_SLOT_DCMONTH1];
  dev.yearGenWh =
      rctCur[RCT_SLOT_DCYEAR0] + rctCur[RCT_SLOT_DCYEAR1];
  dev.totalGenWh =
      rctCur[RCT_SLOT_DCTOTAL0] + rctCur[RCT_SLOT_DCTOTAL1];
  dev.totalGenAWh = rctCur[RCT_SLOT_DCTOTAL0];
  dev.totalGenBWh = rctCur[RCT_SLOT_DCTOTAL1];
  dev.monthHouseWh = rctCur[RCT_SLOT_LOADMONTH];
  dev.yearHouseWh = rctCur[RCT_SLOT_LOADYEAR];
  dev.totalHouseWh = rctCur[RCT_SLOT_LOADTOTAL];
  dev.monthFeedInWh = rctCur[RCT_SLOT_FEEDMONTH];
  dev.yearFeedInWh = rctCur[RCT_SLOT_FEEDYEAR];
  dev.monthGridDrawWh = rctCur[RCT_SLOT_GRIDMONTH];
  dev.yearGridDrawWh = rctCur[RCT_SLOT_GRIDYEAR];

  // External energy (S0 generator), the device's own counters. These are the
  // authoritative values: the e_ext_* family counts the external generator
  // where the e_dc_* family stops at the two DC inputs. The device's own
  // arithmetic around them is idiosyncratic - the register set carries both
  // summed and unsummed variants, and the registry documents neither as the
  // primary - so both are polled and logged raw, and the summed one is used for
  // the display. That is a measured choice, not a documented one, and the log
  // line makes the difference visible on the real device.
  dev.dayExtWh = rctCur[RCT_SLOT_EXTDAY];
  dev.monthExtWh = rctCur[RCT_SLOT_EXTMONTH];
  dev.yearExtWh = rctCur[RCT_SLOT_EXTYEAR];
  dev.totalExtWh = rctCur[RCT_SLOT_EXTTOTAL];
  dev.dayExtPlainWh = rctCur[RCT_SLOT_EXTDAYP];
  dev.monthExtPlainWh = rctCur[RCT_SLOT_EXTMONP];

  strlcpy(dev.deviceName, rctDevName, sizeof(dev.deviceName));
  strlcpy(dev.firmwareVersion, rctFwVersion,
          sizeof(dev.firmwareVersion));
  dev.coreTemp = rctCur[RCT_SLOT_CORET];
  dev.batteryTemp = rctCur[RCT_SLOT_BTEMP];
  dev.heatSinkTemp = rctCur[RCT_SLOT_HTEMP];
  dev.nextCalibTs = rctRaw[RCT_SLOT_CALIB];
  dev.batteryCycles = rctCur[RCT_SLOT_CYCLES];
  dev.batterySoh = pct100(rctCur[RCT_SLOT_SOH]);
  // prim_sm.island_flag (OID 0x3623D82A). rctmon (svalouch/rctmon) names it
  // inverter_grid_separated and forwards the whole value unmasked.
  //
  // Measured on the real device: the register read 0x00000002 while the
  // inverter was running normally on the grid (no island), so "non-zero means
  // islanded" is wrong - a whole-register boolean reads 0x02 as true. The
  // register is a bitfield and only bit 0 is the island flag; 0x02 has it
  // clear, which is what the device reported while grid-connected. rctmon
  // (svalouch/rctmon) forwards the whole value as inverter_grid_separated
  // without masking, so its field is truthy for this on-grid reading too.
  //
  // islandKnown distinguishes "the device said 0" from "the device has not
  // answered this OID yet" - both read as 0 in rctRaw, and the Service page
  // would otherwise claim "nein" before the first answer arrived.
  if (infoSeen & (1u << (RCT_SLOT_ISLAND - RCT_SLOT_DEVNAME))) {
    dev.islandKnown = true;
    dev.islandMode = (rctRaw[RCT_SLOT_ISLAND] & 1u) != 0;
  }

  // One-time bring-up log for the "Energie" page: proves the 13 accumulated
  // OIDs answer and that the units are kWh. Every meter prints in the period
  // order the page offers: Tag / Monat / Jahr / Gesamt.
  static bool energyLogged = false;
  if (!energyLogged && (freshMask & energySlotsMask) == energySlotsMask) {
    energyLogged = true;
    Serial.printf("RCT energy [kWh Tag/Monat/Jahr/Gesamt] PV %.1f/%.1f/%.1f/"
                  "%.1f | Verbrauch %.1f/%.1f/%.1f/%.1f | Einspeisung "
                  "%.1f/%.1f/%.1f/%.1f | Bezug %.1f/%.1f/%.1f/%.1f"
                  " | Extern %.1f/%.1f/%.1f/%.1f (Tag/Mon/Jahr/Ges, sum)\n",
                  dev.dayGenWh / 1000.0f, dev.monthGenWh / 1000.0f,
                  dev.yearGenWh / 1000.0f, dev.totalGenWh / 1000.0f,
                  dev.dayHouseWh / 1000.0f, dev.monthHouseWh / 1000.0f,
                  dev.yearHouseWh / 1000.0f, dev.totalHouseWh / 1000.0f,
                  dev.dayFeedInWh / 1000.0f,
                  dev.monthFeedInWh / 1000.0f,
                  dev.yearFeedInWh / 1000.0f,
                  dev.feedInTotalWh / 1000.0f,
                  dev.dayGridDrawWh / 1000.0f,
                  dev.monthGridDrawWh / 1000.0f,
                  dev.yearGridDrawWh / 1000.0f,
                  dev.gridDrawTotalWh / 1000.0f,
                  dev.dayExtWh / 1000.0f, dev.monthExtWh / 1000.0f,
                  dev.yearExtWh / 1000.0f, dev.totalExtWh / 1000.0f);
  }

  // One-time bring-up log once the whole slow group has answered.
  if (!infoLogged && infoSeen ==
                         ((1u << (RCT_NUM_SLOTS - RCT_SLOT_DEVNAME)) - 1u)) {
    infoLogged = true;
    // The raw island and bat_status values are printed alongside the decoded
    // ones: both are polarity questions that only the real device can settle,
    // and the decode is only trustworthy if the raw value is visible next to it.
    Serial.printf("RCT info: \"%s\" | SW %s | core %.1f C | bat %.1f C | "
                  "heat %.1f C | calib %lu | cycles %.0f | SOH %.1f %% | "
                  "island %u (raw 0x%08X) | bat_status 0x%08X\n",
                  dev.deviceName, dev.firmwareVersion,
                  dev.coreTemp, dev.batteryTemp,
                  dev.heatSinkTemp,
                  (unsigned long)dev.nextCalibTs, dev.batteryCycles,
                  dev.batterySoh, (unsigned)dev.islandMode,
                  (unsigned)rctRaw[RCT_SLOT_ISLAND],
                  (unsigned)dev.batteryStatus);
  }

  int freshCount = 0;
  for (uint64_t m = freshMask; m; m &= m - 1) {
    freshCount++;
  }
  Serial.printf("RCT: grid %.0f/%.0f/%.0f (sum %.0f) W | load %.0f/%.0f/%.0f W"
                " | PV %.2f kW (S0 %.0f W)"
                " | bat %.0f%% %.2f kW (%.1f A, %.1f V)"
                " | ext %.2f kWh (e_ext_day %.2f)"
                " | integriert %.2f kWh | %d/%d fresh\n",
                dev.gridW[0], dev.gridW[1],
                dev.gridW[2], dev.gridExchangeW,
                dev.houseW[0],
                dev.houseW[1], dev.houseW[2],
                (dev.genW[0] + dev.genW[1] +
                 dev.extW) / 1000.0f,
                dev.extW, dev.socPct,
                dev.batW / 1000.0f, dev.batA,
                dev.batV,
                dev.dayExtWh / 1000.0f,
                // The unsummed e_ext_day next to e_ext_day_sum. Which of the two
                // is the real external production is documented nowhere, so both
                // stay in the log until the difference is measured rather than
                // assumed. Measured so far: sum 2,83 kWh vs plain 0,00 kWh on
                // 2025-09-29, i.e. the unsummed variant reads zero even though
                // the generator ran - so the summed one is the displayed value.
                dev.dayExtPlainWh / 1000.0f,
                // Our own integration of extW, since boot. Independent of
                // the device: it must track the day counter's rise. A growing
                // gap means the counter is not what its name says.
                dev.extEnergyWh / 1000.0f, freshCount, RCT_NUM_SLOTS);
}

// ---------------------------------------------------------------------------
// DeviceDriver
// ---------------------------------------------------------------------------

void RctDriver::setTransport(DeviceTransport *link) { s_link = link; }

void RctDriver::begin(const DeviceConfig &cfg) {
  s_cfg = cfg;

  // What this family can report. Stated here rather than derived from what
  // happens to arrive: an RCT Power has all four meters and the island flag, and
  // the panel asks before it draws - so it has to be said.
  //
  // It also says it does not sleep: the inverter runs from the grid, so silence
  // after sunset is a fault on this family and is reported as one.
  deviceStateMutable().caps = DeviceCaps();
  DeviceCaps &c = deviceStateMutable().caps;
  c.houseMeter = true;  // the load meter reads the household
  c.gridMeter = true;   // and the grid exchange is its own register
  c.battery = true;     // state of charge answers on a device with a battery
  c.islandFlag = true;  // prim_sm.island_flag
  c.faultBits = true;   // four fault words, 128 bits
  c.sleepsWithoutGeneration = false;
}

void RctDriver::poll(uint32_t budgetMs) { rctPoll(budgetMs); }

bool RctDriver::connected() const { return dev.connected; }
