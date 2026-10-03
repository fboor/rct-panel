// How a device is reached: the byte level and nothing else.
//
// The driver owns the frames - the start token, the escaping, the checksum, the
// request/response cycle - and this interface owns the bytes underneath. That
// split is what lets two frame languages coexist later without the driver
// changing: the RCT bus escapes bytes and answers in arbitrary order because
// every client sees every other client's frames, while a Modbus RTU master
// would address one slave, wrap every request in a function code, check its own
// CRC16 and wait out a pause of 3.5 character times at the configured baud
// rate. None of that belongs in the byte layer.
//
// Three properties of the interface are there for that later variant, not for
// TCP:
//
//   - Milliseconds come from the driver (millis()), never from here. A serial
//     line's frame gaps are a function of the baud rate, so a transport that
//     computed them would have to know the baud rate - and a driver that could
//     not ask would have to guess.
//   - peerOpen() is what a socket can say and a bare line cannot. The RCT driver
//     treats a closed peer as the one reason to drop the connection and
//     reconnect; on RS485 the answer is always yes and the timeout is the only
//     failure signal, so the name is deliberately not "connected()".
//   - The transmit direction of an RS485 line (DE/RE) belongs to the wiring and
//     therefore to the transport. There is deliberately no method for it yet: an
//     empty one would suggest a feature that is not there.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_DEVICE_TRANSPORT_H
#define RCT_DEVICE_TRANSPORT_H

#include <Arduino.h>

class DeviceTransport {
public:
  virtual ~DeviceTransport() = default;

  // Bring the link up. timeoutMs bounds the wait, because a dead host must not
  // freeze the task that also runs LVGL.
  virtual bool open(const char *host, const char *port, uint32_t timeoutMs) = 0;

  // Write all n bytes or none of them: a half-written frame is worse than a
  // failed one, and the driver's frame order depends on it.
  virtual size_t write(const uint8_t *buf, size_t n) = 0;

  // Bytes waiting, and the next one (-1 when there is none).
  virtual int available() = 0;
  virtual int read() = 0;

  // Is the other end still there? Not the same question as "is this link
  // configured"; see the header.
  virtual bool peerOpen() = 0;

  // Drop the link. Must be safe to call when there is none - a failed connect
  // can leave a socket half-open.
  virtual void close() = 0;
};

#endif // RCT_DEVICE_TRANSPORT_H