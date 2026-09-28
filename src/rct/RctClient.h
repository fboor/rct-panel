// RCT Power "Serial Communication Protocol" client (TCP).
//
// Direct port of the RctParser from the Energy2Shelly_ESP project -
// Apache License 2.0. See NOTICE for details and copyright.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef RCT_CLIENT_H
#define RCT_CLIENT_H

#include <Arduino.h>

// Poll the RCT Power device once: (re)connect if needed, READ every tracked
// OID, consume the shared bus stream for one poll cycle and update the
// snapshot in RctTypes.h. Idempotent and self-healing; call at a fixed rate
// (e.g. every 5 s).
void rctParse();

#endif // RCT_CLIENT_H