// Host stand-in for NVS. Keeps the last written value per key, which is all
// the relay configuration needs (two keys, written on every change).
#ifndef STUB_PREFERENCES_H
#define STUB_PREFERENCES_H

#include <string.h>

#include "Arduino.h"

class Preferences {
 public:
  bool begin(const char *ns, bool readOnly) {
    (void)ns;
    (void)readOnly;
    return true;
  }
  void end() {}
  uint8_t getUChar(const char *key, uint8_t def) {
    return kCharSet ? kChar : def;
  }
  int getInt(const char *key, int def) {
    return (kIntSet && strcmp(key, "thresh") == 0) ? kInt : def;
  }
  void putUChar(const char *key, uint8_t v) {
    (void)key;
    kChar = v;
    kCharSet = true;
  }
  void putInt(const char *key, int v) {
    (void)key;
    kInt = v;
    kIntSet = true;
  }

  // Test-visible storage.
  static uint8_t kChar;
  static bool kCharSet;
  static int kInt;
  static bool kIntSet;
};

#endif // STUB_PREFERENCES_H
