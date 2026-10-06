// A stand-in for the Arduino core, enough of it for the shipped firmware headers
// to compile on the build machine.
//
// This is the seam the simulator needs and the firmware must not feel: the panel
// builds against the real Arduino, this builds against this. Nothing here is a
// second implementation of anything the panel draws - it is a clock, a log and
// three small value types.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_PANEL_SIM_ARDUINO_H
#define RCT_PANEL_SIM_ARDUINO_H

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "sim_stubs.h"   // Serial, millis, delay, micros, delayMicroseconds, yield
#include "esp_platform.h" // strlcpy, heap_caps_*, MALLOC_CAP_*

using std::min;
using std::max;

// The panel puts these in PROGMEM, which on the host is nothing: a plain read.
#define PROGMEM
#define PGM_P const char *
#define PSTR(s) (s)
#define F(s) (s)
#define ICACHE_RAM_ATTR

// The subset of Arduino's String the firmware uses. A std::string with the two
// conversions Arduino's has, so that a line like
// `cfg.name.c_str()` reads the same as it does on the panel.
class String : public std::string {
 public:
  String() {}
  String(const char *s) : std::string(s ? s : "") {}
  String(const std::string &s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  String(long v) : std::string(std::to_string(v)) {}
  String(unsigned v) : std::string(std::to_string(v)) {}
  String(unsigned long v) : std::string(std::to_string(v)) {}
  String(float v) : std::string(std::to_string(v)) {}

  const char *c_str() const { return std::string::c_str(); }
  size_t length() const { return std::string::size(); }
  String &operator=(const char *s) {
    std::string::operator=(s ? s : "");
    return *this;
  }
};

// One IPv4 address. Only toString() is ever asked for, and only so the info page
// can print the panel's own address.
struct IPAddress {
  uint8_t o[4] = {192, 168, 1, 228};
  String toString() const {
    char b[16];
    snprintf(b, sizeof(b), "%u.%u.%u.%u", o[0], o[1], o[2], o[3]);
    return String(b);
  }
  bool operator==(const IPAddress &) const { return true; }
};

// The chip's identifiers. The panel logs them at boot; in a simulator there is no
// chip, so they are zeros and the log line says that they are made up.
struct EspClass {
  uint32_t getEfuseMac() { return 0; }
  uint32_t getChipId() { return 0; }
  const char *getSdkVersion() { return "sim"; }
};
extern EspClass esp;

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t) { return 0; }

#endif // RCT_PANEL_SIM_ARDUINO_H