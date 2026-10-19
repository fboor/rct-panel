// The receiving half of the RCT "Serial Communication Protocol": bytes off the
// link become one frame, or nothing.
//
//   2b <command> <length> <object id> <payload> <checksum hi> <checksum lo>
//
// length counts the object id and the payload. The checksum covers everything
// between the command byte and itself. A byte 0x2b or 0x2d inside the frame is
// sent twice, the second one behind a 0x2d.
//
// There are TWO shapes, and the difference is the reason this file exists:
//
//   command 0x01 0x02 0x05    one length byte
//   command 0x03 0x04 0x06    TWO length bytes, so a frame can carry a
//                             kilobyte of payload
//
// The driver this was taken from knew only the first shape and read the length
// field as one byte whatever the command said. Against the small values that
// makes no difference - a wrong length is one a frame can survive - so the bug
// stayed invisible until a client on the bus asked the device for something
// big: the event log (logger.error_log_time_stamp) comes back as a 0x06 with a
// two-byte length and a payload of up to a few kilobytes. Read as one byte the
// frame ended early, the checksum was taken over a truncated frame, and the
// rest of the payload was then read as new frames.
//
// Header-only and free of Arduino, like RctCrc.h next to it, because the file
// this belongs to needs WiFi and the panel's task and can therefore not be run
// on the build machine - and a frame cut at the wrong length is silent: the
// device drops it and the panel shows "keine Daten" against a healthy
// inverter. tools/rctframe_test drives this with frames built by hand.
//
// Frames with bit 6 of the command set address a component on the bus (a
// battery's BMS) and carry a four-byte address between the length and the
// object id. The official app drops them outright - its receive state machine
// has no case for 0x41..0x46 and returns to looking for a start token - and so
// does this one, with the same result. Reading one as if it had no address
// would be the alternative, and nothing here says the layout is that.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef RCT_RCTFRAME_H
#define RCT_RCTFRAME_H

#include <stddef.h>
#include <stdint.h>

#include "RctCrc.h"

namespace rctframe {

const uint8_t kStart = 0x2b;  // start token
const uint8_t kEscape = 0x2d; // in front of a start token or an escape

const uint8_t kCmdRead = 0x01;
const uint8_t kCmdWrite = 0x02;
const uint8_t kCmdLongWrite = 0x03;
const uint8_t kCmdReadPeriodically = 0x04;
const uint8_t kCmdResponse = 0x05;
const uint8_t kCmdLongResponse = 0x06;

// Bit 6 of the command: addressed to another component on the bus.
const uint8_t kCmdPlantBit = 0x40;

// What feed() has to say. Only kComplete leaves a frame in the buffer, and only
// then does complete() hold.
enum Status : uint8_t {
  kPartial = 0, // frame still coming, or nothing seen yet
  kComplete,    // a whole frame is in the buffer, checksum not checked yet
  kTooLong,     // the declared length does not fit; the frame was dropped
  kIgnored      // a frame for another component on the bus; dropped like the
                // app drops it
};

// Reads the stream into a buffer the caller owns. The buffer is what makes the
// receiver usable in two places with very different needs: the firmware gives
// it kilobytes once, a test gives it a few dozen bytes.
class Receiver {
public:
  Receiver(uint8_t *buf, size_t capacity)
      : mBuf(buf), mCap(capacity), mLen(0), mTotal(0), mOidOff(0), mCmd(0),
        mDropCmd(0), mDropLen(0), mLong(false), mEscaping(false),
        mComplete(false), mStatus(kPartial) {}

  // Forget everything. The caller does this after a frame was consumed, after a
  // timeout, and after a frame was dropped - the last one matters: the bytes of
  // a dropped frame are still coming and must not be read as a new frame.
  void reset() {
    mLen = 0;
    mTotal = 0;
    mOidOff = 0;
    mCmd = 0;
    mLong = false;
    mEscaping = false;
    mComplete = false;
    mStatus = kPartial;
  }

  Status feed(uint8_t c) {
    if (mLen == 0) {
      if (c != kStart) {
        return mStatus;
      }
      mBuf[mLen++] = c;
      return mStatus;
    }

    if (mEscaping) {
      mEscaping = false;
    } else if (c == kEscape) {
      mEscaping = true;
      return mStatus;
    } else if (c == kStart) {
      // A start token inside a frame can only have been an escaped one - and an
      // escaped one was consumed by the branch above, so the receiver never
      // sees it here. Unescaped, it can only mean that the length it read was
      // wrong: a frame of another client was cut off, or the connection started
      // in the middle of one. Waiting for that frame to finish would swallow
      // every frame behind it, because inside a frame a start token looks like
      // any other byte. Starting over here costs one frame and ends the
      // silence.
      reset();
      mBuf[mLen++] = c;
      return mStatus;
    }

    if (mLen >= mCap) {
      // Cannot happen while a length is in force (it was checked against the
      // capacity), but a buffer that runs over would be a memory error rather
      // than a dropped frame.
      mDropLen = mTotal;
      rememberDrop();
      reset();
      mStatus = kTooLong;
      return mStatus;
    }
    mBuf[mLen++] = c;

    if (mLen == 2) {
      mCmd = mBuf[1];
      if (mCmd & kCmdPlantBit) {
        rememberDrop();
        reset();
        mStatus = kIgnored;
        return mStatus;
      }
      mLong = isLong(mCmd);
      mOidOff = mLong ? 4 : 3; // object id sits behind the length field
    }

    // The length is known as soon as the last of its bytes is in. Until then
    // mTotal stays 0 and the frame is still growing.
    if (mTotal == 0 && mLen >= (size_t)mOidOff) {
      const size_t lenField =
          mLong ? (((size_t)mBuf[2] << 8) | mBuf[3]) : (size_t)mBuf[2];
      const size_t total = (mLong ? 6u : 5u) + lenField;
      // Shorter than an object id is not a frame, and one that does not fit is
      // a frame this receiver cannot hold: both are dropped and the stream is
      // picked up again at the next start token.
      if (lenField < 4 || total > mCap) {
        mDropLen = lenField;
        rememberDrop();
        reset();
        mStatus = kTooLong;
        return mStatus;
      }
      mTotal = total;
    }

    if (mTotal != 0 && mLen >= mTotal) {
      mComplete = true;
      mStatus = kComplete;
    }
    return mStatus;
  }

  bool complete() const { return mComplete; }
  Status status() const { return mStatus; }
  uint8_t command() const { return mCmd; }

  // What was thrown away, kept across the reset that a drop does: a frame that
  // does not fit is the one case where the reason is worth a line in the log,
  // and "it did not fit" without its length is a guess to go looking with.
  uint8_t droppedCommand() const { return mDropCmd; }
  size_t droppedLength() const { return mDropLen; }

  // Valid once complete() holds. Before that they answer 0 rather than reading
  // a buffer that is not there: a dropped frame leaves nothing to point at, and
  // the caller has to be able to ask without checking first.
  uint32_t oid() const { return mComplete ? be32(&mBuf[mOidOff]) : 0; }
  const uint8_t *payload() const { return &mBuf[mOidOff + 4]; }
  size_t payloadLen() const {
    return mComplete ? mTotal - (size_t)mOidOff - 6 : 0;
  }

  bool crcOk() const {
    if (!mComplete) {
      return false;
    }
    const uint16_t calc = rctcrc::compute(&mBuf[1], mTotal - 3);
    const uint16_t got =
        (uint16_t)(((uint16_t)mBuf[mTotal - 2] << 8) | mBuf[mTotal - 1]);
    return calc == got;
  }

  // How many bytes the frame occupies in the buffer, for a log line.
  size_t frameLen() const { return mTotal; }

  static bool isLong(uint8_t cmd) {
    return cmd == kCmdLongWrite || cmd == kCmdReadPeriodically ||
           cmd == kCmdLongResponse;
  }

private:
  // Keep the command across the reset() a drop does - mDropLen is set by the
  // caller, which is the only place that knows how long the frame claimed to
  // be.
  void rememberDrop() { mDropCmd = mCmd; }

  static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
  }

  uint8_t *mBuf;
  size_t mCap;
  size_t mLen;
  size_t mTotal;
  uint8_t mOidOff;
  uint8_t mCmd;
  uint8_t mDropCmd;
  size_t mDropLen;
  bool mLong;
  bool mEscaping;
  bool mComplete;
  Status mStatus;
};

} // namespace rctframe

#endif // RCT_RCTFRAME_H