// A stand-in for the ESP32 NVS, in memory.
//
// The panel's settings and its theme live in Preferences. A simulator that had no
// storage would come up with nothing chosen, which is a page layout nobody has to
// look at: a default is more useful than an empty one, and it is also more honest,
// because a real device comes up with its stored values.
//
// Nothing here is written anywhere. The values live until the process ends.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_PANEL_SIM_PREFERENCES_H
#define RCT_PANEL_SIM_PREFERENCES_H

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

class Preferences {
 public:
  bool begin(const char *name, bool readOnly = false) {
    (void)name;
    (void)readOnly;
    return true;
  }
  void end() {}

  int getInt(const char *key, int def = 0) {
    auto it = m.find(key);
    return (it == m.end()) ? def : it->second.i;
  }
  uint8_t getUChar(const char *key, uint8_t def = 0) {
    auto it = m.find(key);
    return (it == m.end()) ? def : (uint8_t)it->second.i;
  }
  bool getBool(const char *key, bool def = false) {
    auto it = m.find(key);
    return (it == m.end()) ? def : (it->second.i != 0);
  }
  std::string getString(const char *key, const char *def = "") {
    auto it = m.find(key);
    return (it == m.end() || it->second.s.empty()) ? std::string(def) : it->second.s;
  }

  void putInt(const char *key, int v) { m[key] = {v, ""}; }
  void putUChar(const char *key, uint8_t v) { m[key] = {v, ""}; }
  void putBool(const char *key, bool v) { m[key] = {v ? 1 : 0, ""}; }
  void putString(const char *key, const char *v) { m[key] = {0, v ? v : ""}; }

  bool remove(const char *key) {
    auto it = m.find(key);
    if (it == m.end()) {
      return false;
    }
    m.erase(it);
    return true;
  }
  bool clear() {
    m.clear();
    return true;
  }

 private:
  struct Slot {
    int i;
    std::string s;
  };
  std::map<std::string, Slot> m;
};

#endif // RCT_PANEL_SIM_PREFERENCES_H