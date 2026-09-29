// rct-panel: phase marker + independent heartbeat, for finding hangs.
//
// Why this exists: main.cpp logs every loop iteration that *finishes* long. That
// measurement has one blind spot, and on 2025-09-29 21:49:37 it hit it - the
// serial log stopped after a touch event and never printed another line, not
// even a stall report, because a hang *inside* one iteration never reaches the
// end of the iteration where the report lives.
//
// So the reporting runs in its own task, and reports what the other tasks are
// doing rather than how long they took. Two numbers matter per task:
//
//   enterMs  when the current phase was entered. Age = stuck right now.
//   prevMs   how long the *previous* phase of the same task took. This is what
//            separates a phase that is merely slow (2 s waiting for a silent
//            device) from one that never returns.
//
// A task has to keep naming its phases, idle ones included. A worker that
// finishes its work and then sleeps without naming an idle phase keeps the age
// of its last phase rising, and gets reported as hung for as long as it sleeps -
// which is what the first version of this did to the card worker, and it made
// the report worse than useless because it pointed at a task that was fine.
//
// One entry per task, not one global: three tasks call diagPhase(), and a single
// shared slot would let a busy SD worker overwrite the phase of the GUI task
// that is actually stuck - the tool would report the wrong task, which is worse
// than reporting nothing. The entry is claimed by task handle, so it also names
// the task in the log instead of making the reader guess.
//
// Priority 2, not 1: the GUI/RCT task waits on the device inside a busy loop
// whose only concession is the yield hook, which renders but never hands the CPU
// back to the RTOS. A heartbeat at the same priority as loopTask would be
// starved for exactly the two seconds in which it is needed most.
//
// SPDX-License-Identifier: MIT
#include "Diag.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

// A phase this long is worth complaining about. The device is polled every 10 s
// and a single unanswered OID costs up to RCT_RX_TIMEOUT_MS (2 s), so the poll
// phase legitimately approaches 2 s and must not be reported as a hang.
#define DIAG_PHASE_WARN_MS 4000
// How often the heartbeat speaks while healthy. Slow enough to keep the log
// readable, fast enough to tell "the panel is quiet" from "the logger died".
#define DIAG_ALIVE_MS 30000

// The screenshot write legitimately occupies the card worker for the whole
// transfer: 691 kB at a 400 kHz SPI clock is ~16 s, far beyond the generic
// warning threshold, and the worker feeds the task watchdog between rows so
// a legitimate write no longer trips the WDT either. A transfer that really
// hangs still dies on the task watchdog (no feed, no yield), so the longer
// ceiling here only silences false alarms, it does not disable hang detection.
static uint32_t diagPhaseWarnMs(const char *phase) {
  return (phase != nullptr && strncmp(phase, "sd.shot", 7) == 0)
             ? 20000u
             : DIAG_PHASE_WARN_MS;
}

// LVGL's own heap. With the built-in pool backend (LV_STDLIB_BUILTIN) that is a
// fixed static array sized by LV_MEM_SIZE and completely invisible to
// ESP.getFreeHeap(); a draw that does not fit fails its allocation, and with
// LV_ASSERT_MALLOC armed the failure is `while(1)` - a silent freeze. An ESP
// heap showing a healthy 155 kB while the panel hung was exactly the blind spot
// this closes, and it is what pointed at LV_MEM_SIZE in the first place.
//
// With LV_STDLIB_CLIB (what this project now uses) LVGL has no separate pool
// and lv_mem_monitor() fills in nothing, so say so instead of printing zeroes
// that look like a full heap.
static void diagLvglMem(char *out, size_t outLen) {
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  if (mon.total_size == 0) {
    lv_snprintf(out, outLen, "lvgl aus dem ESP-Heap (kein eigener Pool)");
    return;
  }
  lv_snprintf(out, outLen, "lvgl %u/%u B frei, groesster %u B, %u%% belegt, %u%% fragmentiert",
              (unsigned)mon.free_size, (unsigned)mon.total_size,
              (unsigned)mon.free_biggest_size, (unsigned)mon.used_pct,
              (unsigned)mon.frag_pct);
}

#define DIAG_MAX_TASKS 6

struct DiagSlot {
  TaskHandle_t task;
  const char *volatile phase; // string literal, compared by pointer
  volatile uint32_t enterMs;  // entry into the current phase
  volatile uint32_t prevMs;   // duration of the previous phase of this task
  volatile uint32_t lastWarn; // when this slot last produced a report
};

static DiagSlot s_slots[DIAG_MAX_TASKS];

static DiagSlot *slotForCurrentTask() {
  const TaskHandle_t me = xTaskGetCurrentTaskHandle();
  DiagSlot *freeSlot = nullptr;
  for (int i = 0; i < DIAG_MAX_TASKS; i++) {
    if (s_slots[i].task == me) {
      return &s_slots[i];
    }
    if (s_slots[i].task == nullptr && freeSlot == nullptr) {
      freeSlot = &s_slots[i];
    }
  }
  if (freeSlot != nullptr) {
    freeSlot->task = me;
    freeSlot->phase = nullptr;
    freeSlot->enterMs = millis();
    freeSlot->prevMs = 0;
    freeSlot->lastWarn = 0;
    return freeSlot;
  }
  return nullptr; // more tasks than slots; diagnostics only, never fatal
}

void diagPhase(const char *name) {
  DiagSlot *s = slotForCurrentTask();
  if (s == nullptr || name == s->phase) {
    return; // most common case: one compare, no writes
  }
  const uint32_t now = millis();
  if (s->phase != nullptr) {
    s->prevMs = now - s->enterMs;
  }
  s->enterMs = now;
  s->phase = name;
}

// C bridge for C sources that need to name a phase. LVGL is plain C and cannot
// call a C++ symbol, so this is the entry point when a freeze has to be traced
// *inside* the library - which is how the LVGL pool exhaustion of 2026-09-29 was
// found. Nothing in this project's own code goes through here.
extern "C" void diagPhaseC(const char *name) { diagPhase(name); }

void diagBeat() {
  DiagSlot *s = slotForCurrentTask();
  if (s != nullptr) {
    s->enterMs = millis();
  }
}

void diagMem(const char *tag) {
  char lvMem[96];
  diagLvglMem(lvMem, sizeof(lvMem));
  Serial.printf("[diag] mem %-14s frei %u B, %s\n", tag,
                (unsigned)ESP.getFreeHeap(), lvMem);
}

static void diagTask(void *) {
  uint32_t lastAlive = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));

    const uint32_t now = millis();
    int stuck = 0;
    for (int i = 0; i < DIAG_MAX_TASKS; i++) {
      DiagSlot &s = s_slots[i];
      if (s.task == nullptr) {
        continue;
      }
      const uint32_t age = now - s.enterMs;
      if (age < diagPhaseWarnMs(s.phase)) {
        continue;
      }
      // Repeat, but not every pass: at 500 ms per pass an 8-line flood per
      // stuck task was both unreadable and enough serial traffic to push other
      // lines out of the log. A hang that comes and go still gets several
      // reports, because it has to outlive the threshold to do that.
      // Counted before the throttle, or a stuck task that is merely reported
      // less often would make the summary claim everything is fine while the
      // very same pass printed a hang line for it.
      stuck++;
      if (now - s.lastWarn < diagPhaseWarnMs(s.phase)) {
        continue;
      }
      s.lastWarn = now;
      // The stack high water mark belongs in the hang report: an overflowed
      // stack does not crash, it corrupts a return address and the task then
      // spins wherever that address points, with the last phase marker as the
      // only evidence left.
      char lvMem[96];
      diagLvglMem(lvMem, sizeof(lvMem));
      Serial.printf("[diag] HAENGER %s: \"%s\" laeuft seit %.1f s"
                    " (vorige Phase %lu ms, frei %u B, Stack min %u B, %s)\n",
                    pcTaskGetName(s.task), s.phase ? s.phase : "-",
                    age / 1000.0f, (unsigned long)s.prevMs,
                    (unsigned)ESP.getFreeHeap(),
                    (unsigned)uxTaskGetStackHighWaterMark(s.task), lvMem);
    }
    if (stuck == 0 && now - lastAlive >= DIAG_ALIVE_MS) {
      lastAlive = now;
      // No LVGL pool figure here, only in the hang report: it is invisible to
      // ESP.getFreeHeap() when LVGL uses its built-in pool, but that is a fixed
      // build-time configuration, so the rare line is where it belongs and not
      // the heartbeat that prints every 30 s.
      Serial.printf("[diag] ok: alle Tasks laufen, frei %u B\n",
                    (unsigned)ESP.getFreeHeap());
    }
  }
}

void diagStart() {
  // 8192: this task only formats strings, but the floating point formatting of
  // the age runs on the (small) default stack of the other tasks.
  xTaskCreate(diagTask, "diag", 8192, nullptr, 2, nullptr);
  diagPhase("boot");
}
