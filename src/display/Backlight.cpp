// Backlight of the 4848S040: GPIO38, active high, dimmable (see Backlight.h).
//
// Until this file existed the panel switched the light as a plain GPIO - two
// lines in displayInit() that set it HIGH and never touched it again. That is
// fine for a bench and wrong for a wall: a backlight at full brightness all day
// is a lamp that never goes out, and in the evening it is the brightest thing in
// the room. So the light now does what every screen lock does - nobody touches
// it, after three minutes it dims, after five it is off, the first touch brings
// it back.
//
// Why LEDC PWM and not two levels:
//
//   - The three-minute step is the whole point. With on/off there is nothing
//     between "full" and "off", so "dimming" would mean jumping from full to
//     dark, which is a step, not a dimming.
//   - The step itself is ramped. A hard change of brightness in an otherwise
//     dark room is a flash, and the people who dim their screens by hard switch
//     turn the feature off again - which defeats the purpose.
//
// Decisions worth remembering (they are the ones a second reader will doubt):
//
//   - 1 kHz, 10 bits, LEDC channel 0. Far above the point where an LED behind a
//     diffuser is seen as flickering, far below anything the wiring or the
//     transistor on the board cares about, and channel 0 is otherwise unused -
//     nothing else in the firmware uses an LEDC peripheral.
//   - Full brightness is written as 2^10, not 1023. The Arduino core maps
//     "all resolution bits set" to one step beyond (see ledcWrite() in
//     esp32-hal-ledc.c) so that the output is constant high; asking for 1024
//     directly is the same thing without the detour. Every level below is
//     computed from BL_FULL, so this stays right if the resolution ever moves.
//   - Dimmed means 30 %: dark enough not to light up the room, bright enough to
//     read the numbers from a step back.
//   - The timers run from the last touch, not from each other: a touch at 4:59
//     restarts both, so the countdown to "off" always means five minutes
//     without a touch.
//   - Times and levels are compile-time constants. Nobody standing at the panel
//     changes three minutes to three minutes and ten, and a setting that cannot
//     be reached cannot be broken - the same argument as the relay's fixed 20 s
//     on-delay and 60 s hold.
//   - Screenshots (the web interface's /bilder page) are unaffected: they
//     render LVGL's own buffer, not what the backlight does. A photo of the
//     panel is therefore always taken at full brightness, whatever the light is
//     doing at that moment. That looks like a bug the first time one sees it.
//   - The idle time is an unsigned subtraction, so it stays right over the
//     millis() wrap (every 49 days). A wrap that compared as "no touch for six
//     weeks" would switch the panel off for good, with the panel looking
//     completely healthy.
//
// SPDX-License-Identifier: MIT
#include "Backlight.h"

#include "board/BoardPins.h"
#if !BOARD_BACKLIGHT_DIMMABLE
#include "board/Ch422G.h"
#endif

// Two backlights, one file. Where the light hangs on a GPIO (the Guition, and any
// board whose mod gives it one) this is the LEDC PWM below. Where it hangs on an
// IO expander - the Waveshare's EXIO2 - there is no PWM behind that pin at all,
// only one bit, so the same timers run with two levels: full, then off. Given up
// is the three-minute step in between; kept is what matters on a wall, which is
// that the light goes out when nobody is there.

// --- Timing, shared by both backlights ---------------------------------------
// The time after which the light goes out. It is the same number whichever
// backlight this is; the ramp times below exist only for the dimmable one.
static const uint32_t OFF_IDLE_MS = 5UL * 60 * 1000;

#if BOARD_BACKLIGHT_DIMMABLE

// --- Hardware ---------------------------------------------------------------

#define BL_CHANNEL 0
#define BL_RES_BITS 10
#define BL_FREQ_HZ 1000

// 1024 at 10 bits: see the comment at the top about ledcWrite().
#define BL_FULL ((uint32_t)1 << BL_RES_BITS)

// --- Timing and levels ------------------------------------------------------

static const uint32_t DIM_IDLE_MS = 3UL * 60 * 1000; // no touch -> dim

// Waking is fast - the person is there and looking. Dimming and switching off
// are slow - nobody is there, and the change should not be seen.
static const uint32_t WAKE_RAMP_MS = 400;
static const uint32_t FADE_MS = 1500;

// 30 % of full.
#define BL_DIM ((uint32_t)(BL_FULL * 30 / 100))

// --- State ------------------------------------------------------------------

static bool s_ready = false;
static bool s_wakeable = true;
static uint32_t s_lastTouchMs = 0; // start of both idle timers
static uint32_t s_duty = BL_FULL;  // what the LEDC is driven with right now
static uint32_t s_target = BL_FULL;// what the timers ask for
static uint32_t s_rampFrom = BL_FULL;
static uint32_t s_rampStartMs = 0;
static uint32_t s_rampMs = 0; // 0 = no ramp running, s_duty has settled

static void writeDuty(uint32_t duty) {
  if (duty == s_duty) {
    return;
  }
  s_duty = duty;
  if (s_ready) {
    ledcWrite(BL_CHANNEL, duty);
  }
}

// Start a linear ramp from the level the light is at now to `to`. Called only
// when the target changes, so a touch in the middle of the fade out starts the
// fade in from where the light actually is, not from the level it came from.
static void rampTo(uint32_t now, uint32_t to, uint32_t ms) {
  s_target = to;
  s_rampFrom = s_duty;
  s_rampStartMs = now;
  s_rampMs = ms;
}

void backlightInit() {
  // Lit before the PWM takes over, so there is no black flash while LEDC is set
  // up (and so the panel is on even if the setup below fails).
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
  s_ready = false;

  if (ledcSetup(BL_CHANNEL, BL_FREQ_HZ, BL_RES_BITS) == 0) {
    Serial.println(F("Backlight: LEDC setup failed - light stays on"));
    return;
  }
  ledcAttachPin(PIN_LCD_BL, BL_CHANNEL);

  s_lastTouchMs = millis();
  s_duty = s_target = s_rampFrom = BL_FULL;
  s_rampMs = 0;
  s_ready = true;
  ledcWrite(BL_CHANNEL, s_duty);
  Serial.printf("Backlight: GPIO %d at %d Hz/%d bit, full %lu/1024\n",
                (int)PIN_LCD_BL, (int)BL_FREQ_HZ, (int)BL_RES_BITS,
                (unsigned long)s_duty);
}

void backlightSetWakeable(bool wakeable) {
  s_wakeable = wakeable;
  if (wakeable) {
    return;
  }
  // Without a touch controller nothing can call backlightActivity(), so the
  // timers must not run: a panel that switches itself off with no way to wake
  // it is not a dimming panel, it is a dead one. Any ramp in flight is dropped.
  s_rampMs = 0;
  s_target = BL_FULL;
  writeDuty(BL_FULL);
  Serial.println(F("Backlight: no touch controller - light stays on"));
}

void backlightActivity() {
  if (!s_ready) {
    return;
  }
  s_lastTouchMs = millis();
  if (s_target != BL_FULL) {
    rampTo(s_lastTouchMs, BL_FULL, WAKE_RAMP_MS);
    Serial.println(F("Backlight: touch - back to full"));
  }
}

void backlightUpdate() {
  if (!s_ready) {
    return;
  }
  const uint32_t now = millis();

  if (s_wakeable) {
    // Unsigned, so the millis() wrap stays harmless.
    const uint32_t idle = now - s_lastTouchMs;
    uint32_t want = BL_FULL;
    if (idle >= OFF_IDLE_MS) {
      want = 0;
    } else if (idle >= DIM_IDLE_MS) {
      want = BL_DIM;
    }
    if (want != s_target) {
      rampTo(now, want, want > s_duty ? WAKE_RAMP_MS : FADE_MS);
      Serial.printf("Backlight: idle %lus -> %s\n", (unsigned long)(idle / 1000),
                    want == 0 ? "off" : (want == BL_DIM ? "30 %" : "full"));
    }
  }

  if (s_rampMs) {
    const uint32_t t = now - s_rampStartMs;
    if (t >= s_rampMs) {
      s_rampMs = 0;
      writeDuty(s_target);
    } else {
      // Signed: s_target - s_rampFrom is negative on the way down.
      const int32_t delta = (int32_t)s_target - (int32_t)s_rampFrom;
      writeDuty((uint32_t)((int32_t)s_rampFrom + delta * (int32_t)t /
                                             (int32_t)s_rampMs));
    }
  }
}

uint8_t backlightPercent() {
  return (uint8_t)((s_duty * 100 + BL_FULL / 2) / BL_FULL);
}

#else // !BOARD_BACKLIGHT_DIMMABLE

// --- Behind an IO expander: one bit, no PWM ----------------------------------
//
// No LEDC here and no ramp: the pin is a single bit of a register, so "dimmer"
// would mean on and off with a different word for it. The three-minute step is
// therefore skipped rather than faked - the light stays at full until the five
// minutes are up, then goes out.

static bool s_ready = false;
static bool s_wakeable = true;
static bool s_on = false;
static uint32_t s_lastTouchMs = 0;

static void write(bool level) {
  if (level == s_on) {
    return;
  }
  s_on = level;
  if (s_ready) {
    ch422gWrite(BOARD_CH422G_PIN_BACKLIGHT,
                level ? (BOARD_BACKLIGHT_ON_LEVEL != 0) : (BOARD_BACKLIGHT_ON_LEVEL == 0));
  }
}

void backlightInit() {
  // The light is already on - the expander's start-up level says so - so there is
  // nothing to flash here, and a board whose expander is missing keeps whatever
  // its hardware does, which is a lit screen and a log line saying why it cannot
  // be switched.
  s_ready = ch422gFound();
  s_lastTouchMs = millis();
  if (!s_ready) {
    Serial.println(F("Backlight: kein IO-Expander - das Licht bleibt an"));
  } else {
    Serial.printf("Backlight: EXIO%d, an/aus ohne Stufen\n",
                  BOARD_CH422G_PIN_BACKLIGHT);
  }
}

void backlightSetWakeable(bool wakeable) {
  s_wakeable = wakeable;
  if (!wakeable) {
    write(true);
    Serial.println(F("Backlight: kein Touch - das Licht bleibt an"));
  }
}

void backlightActivity() {
  s_lastTouchMs = millis();
  write(true);
}

void backlightUpdate() {
  if (!s_ready || !s_wakeable) {
    return;
  }
  // Unsigned, so the millis() wrap stays harmless.
  if (s_on && (uint32_t)(millis() - s_lastTouchMs) >= OFF_IDLE_MS) {
    write(false);
    Serial.println(F("Backlight: 5 min ohne Touch - aus"));
  }
}

uint8_t backlightPercent() { return s_on ? 100 : 0; }

#endif // BOARD_BACKLIGHT_DIMMABLE