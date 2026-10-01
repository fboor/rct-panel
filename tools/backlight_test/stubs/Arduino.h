// Minimal Arduino stand-in so the backlight state machine can be compiled and
// exercised on the host. Only what src/display/Backlight.cpp actually uses:
// millis() from a global the test drives, and the LEDC calls recorded instead of
// driving a pin.
//
// DUTCH here means "duty" in the LEDC sense: the value written to the channel,
// 1024 = constant high = full brightness.
#ifndef STUB_ARDUINO_H
#define STUB_ARDUINO_H

#include <stdint.h>
#include <stdio.h>

#define LOW 0
#define HIGH 1
#define OUTPUT 4

extern uint32_t g_millis;
inline uint32_t millis() { return g_millis; }

void pinMode(int pin, int mode);
void digitalWrite(int pin, int level);

// LEDC stand-ins: ledcSetup() answers the frequency it would have set (0 means
// "refused", which is how the real one reports failure).
uint32_t ledcSetup(uint8_t channel, uint32_t freq, uint8_t resolution);
void ledcAttachPin(uint8_t pin, uint8_t chan);
void ledcWrite(uint8_t chan, uint32_t duty);

struct StubSerial {
  void print(const char *s) { fputs(s, stdout); }
  void println(const char *s) { printf("%s\n", s); }
  void println() { printf("\n"); }
  void printf(const char *fmt, ...);
};
extern StubSerial Serial;

#define F(x) (x)

#endif // STUB_ARDUINO_H