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
#include <math.h>
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
// The flash-string readers. On the ESP32 they read from flash; here the strings are
// already in RAM, so both are the ordinary C functions.
#define pgm_read_byte(addr)  (*(const uint8_t *)(addr))
#define pgm_read_word(addr)  (*(const uint16_t *)(addr))
#define strlen_P(s)   strlen(s)
#define strcpy_P(d, s) strcpy(d, s)
#define strncpy_P(d, s, n) strncpy(d, s, n)
#define strcmp_P(a, b) strcmp(a, b)
#define strstr_P(h, n) strstr(h, n)
#define FPSTR(s) (s)

// The subset of Arduino's String the firmware uses. A std::string with the two
// conversions Arduino's has, so that a line like
// `cfg.name.c_str()` reads the same as it does on the panel.
class String {
 public:
  String() {}
  String(const char *s) : m_str(s ? s : "") {}
  String(const std::string &s) : m_str(s) {}
  String(int v) : m_str(std::to_string(v)) {}
  String(long v) : m_str(std::to_string(v)) {}
  String(unsigned v) : m_str(std::to_string(v)) {}
  String(unsigned long v) : m_str(std::to_string(v)) {}
  String(float v) : m_str(std::to_string(v)) {}

  const char *c_str() const { return m_str.c_str(); }
  size_t length() const { return m_str.size(); }
  bool isEmpty() const { return m_str.empty(); }
  String &operator=(const char *s) {
    m_str = (s != nullptr) ? s : "";
    return *this;
  }
  bool operator==(const char *s) const { return m_str == (s ? s : ""); }
  bool operator!=(const char *s) const { return !(*this == s); }
  bool operator==(const String &s) const { return m_str == s.m_str; }
  String &operator+=(const char *s) { m_str += (s != nullptr) ? s : ""; return *this; }
  String &operator+=(const String &s) { m_str += s.m_str; return *this; }
  String &operator+=(char c) { m_str.push_back(c); return *this; }
  void reserve(size_t n) { m_str.reserve(n); }
  // The firmware builds a header from a String and a bare char* here, so the free
  // operator is part of what "a String" means and not a convenience.
  friend String operator+(const String &a, const char *b) {
    String r(a);
    r += b;
    return r;
  }
  friend String operator+(const char *a, const String &b) {
    String r(a);
    r += b.m_str;
    return r;
  }
  friend String operator+(const String &a, const String &b) {
    String r(a);
    r += b.m_str;
    return r;
  }

  // The three methods the shipped web interface calls beyond the C string ones.
  // Arduino's replace() replaces EVERY occurrence, not the first, and returns
  // nothing - both of which the page templates in pages.h rely on.
  void replace(const String &was, const String &neu) {
    if (was.length() == 0) {
      return;
    }
    std::string r;
    const std::string w = was.m_str, n = neu.m_str;
    size_t i = 0;
    while (i < m_str.size()) {
      if (m_str.compare(i, w.size(), w) == 0) {
        r += n;
        i += w.size();
      } else {
        r.push_back(m_str[i]);
        i++;
      }
    }
    m_str = r;
  }
  void replace(const char *was, const char *neu) {
    replace(String(was), String(neu));
  }
  int indexOf(const String &was) const {
    const size_t k = m_str.find(was.m_str);
    return (k == std::string::npos) ? -1 : (int)k;
  }
  int toInt() const { return atoi(m_str.c_str()); }

  std::string m_str;
};

// One IPv4 address. Only toString() is ever asked for, and only so the info page
// can print the panel's own address.
// 127.0.0.1, not the panel's address. The firmware prints its own IP on the info page
// and in the web interface's start line, and here those must name the machine the
// reader is actually looking at - a build machine that answers to 192.168.1.228 is
// claiming to be a panel, which is the one thing this program must not do.
struct IPAddress {
  uint8_t o[4] = {127, 0, 0, 1};
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
  // The info page shows the free heap in kB. On the build machine the heap is the
  // C library's, so the number is real - it is simply not the same heap.
  // A restart on a build machine would kill the process, which is the correct
  // meaning and an unhelpful one for a tool. So it says so and does nothing: the
  // settings page then reports the same "neu starten" answer the panel would, and
  // the window stays open.
  void restart() { printf("ESP.restart() im Simulator: kein Neustart\n"); }
  uint32_t getFreeHeap() { return 262144u; }
  uint32_t getHeapSize() { return 1048576u; }
  uint32_t getMinFreeHeap() { return 131072u; }
};
extern EspClass esp;
// The global the ESP32 core calls esp, spelled the same way so that firmware code
// which says ESP works unchanged.
extern EspClass ESP;

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t) { return 0; }

#endif // RCT_PANEL_SIM_ARDUINO_H