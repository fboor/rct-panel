// Minimal Arduino stand-in so the relay state machine can be compiled and
// exercised on the host. Only what src/output/Relay.cpp actually uses.
#ifndef STUB_ARDUINO_H
#define STUB_ARDUINO_H

#include <stdint.h>
#include <stdio.h>

#define LOW 0
#define HIGH 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define OUTPUT 4

// The test drives these.
extern uint32_t g_millis;
inline uint32_t millis() { return g_millis; }

void pinMode(int pin, int mode);
void digitalWrite(int pin, int level);

// Only printf, and the F() macro (Flash strings are plain strings here).
struct StubSerial {
  void print(const char *s) { fputs(s, stdout); }
  void println(const char *s) { printf("%s\n", s); }
  void println() { printf("\n"); }
  void printf(const char *fmt, ...);
};
extern StubSerial Serial;

#define F(x) (x)

#endif // STUB_ARDUINO_H
