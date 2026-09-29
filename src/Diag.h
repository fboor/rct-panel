// rct-panel: phase marker + independent heartbeat, for finding hangs.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_PANEL_DIAG_H
#define RCT_PANEL_DIAG_H

#include <Arduino.h>

// Start the heartbeat task. Call once, after Serial is up.
void diagStart();

// Name the step the calling task is currently in.
//
// The name is compared by pointer, so pass a string literal ("rct.poll") and
// not a runtime buffer: this is called in tight loops and must stay free.
// Only the *change* does any work, so naming the same phase repeatedly (every
// loop iteration, every poll) costs one pointer comparison.
//
// A phase is considered stuck when the task has been inside it for longer than
// the heartbeat's warning threshold; the heartbeat then reports the name and
// the age until the phase changes or the panel is reset.
void diagPhase(const char *name);

// Say "still alive, still in this phase", without naming a new one.
//
// diagPhase() returns immediately when the name is unchanged, so a task that
// sits in one phase on purpose - a worker idling between two jobs, say - has to
// say so explicitly. Without this, a task that finished its work in 40 ms and
// then slept for an hour is indistinguishable from one that never came back,
// which is how the card worker first got reported as hung while it was doing
// exactly what it should.
void diagBeat();

// Log the LVGL heap state under a tag. LVGL allocates from its own fixed pool
// (LV_MEM_SIZE), which is invisible to ESP.getFreeHeap(); a pool that is
// exhausted is a silent freeze waiting to happen, so the interesting question
// is *when* it fills up. Cheap: a few lines during boot.
void diagMem(const char *tag);

#endif // RCT_PANEL_DIAG_H
