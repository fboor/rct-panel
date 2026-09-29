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

// Register a callback that runs repeatedly while rctParse() waits for a
// response frame (an OID that never answers costs up to RCT_RX_TIMEOUT_MS).
// The panel needs it to keep rendering and reading touch: LVGL shares the task
// with this client, so without it the GUI stalls for seconds. Must run in the
// same task, i.e. call it from setup() or loop(), not from another FreeRTOS
// task - rctState is shared without locking.
void rctSetYieldHook(void (*fn)());

#endif // RCT_CLIENT_H