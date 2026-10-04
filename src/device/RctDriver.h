// The RCT Power driver: its "Serial Communication Protocol" over a link the
// panel hands over.
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
// All of that is this device family's business and stays here: the panel asks
// for DeviceState and knows nothing about OIDs, escaping or the shared bus.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef RCT_DEVICE_RCT_DRIVER_H
#define RCT_DEVICE_RCT_DRIVER_H

#include "DeviceDriver.h"

class RctDriver : public DeviceDriver {
public:
  // The link is the panel's: a driver must not open a socket itself, so that
  // the transport can be swapped and so that a test can hand over its own.
  void setTransport(DeviceTransport *link) override;

  void begin(const DeviceConfig &cfg) override;

  // budgetMs bounds one collection run. The per-frame windows below it are
  // this protocol's own; the budget is the caller's.
  void poll(uint32_t budgetMs) override;

  bool connected() const override;

  // "RCT" - the family, and the prefix of the log file names.
  const char *typeName() const override { return "RCT"; }
};

#endif // RCT_DEVICE_RCT_DRIVER_H