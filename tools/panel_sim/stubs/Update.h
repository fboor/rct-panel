// A stand-in for the ESP32 OTA updater: the interface, and nothing behind it.
//
// The update page is served and the POST is accepted, because seeing the page is
// useful on a build machine. Writing a firmware image through it would be theatre,
// so every call reports "no" and the firmware's own handler prints the error it
// gets - which is the honest outcome and the same one a failed update has.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_PANEL_SIM_UPDATE_H
#define RCT_PANEL_SIM_UPDATE_H

#include <cstddef>
#include <cstdint>

// The two arguments Update.begin() is called with in the firmware's upload handler.
#define U_FLASH 0
#define UPDATE_SIZE_UNKNOWN (0xFFFFFFFF)

enum Update_error_t { UPDATE_ERROR_FLASH_WRONG_SIZE,
                      UPDATE_ERROR_FLASH_VERIFY_FAILED };

class UpdateClass {
 public:
  bool begin(size_t size, int command = 0) { (void)size; (void)command; return false; }
  size_t write(const uint8_t *d, size_t n) { (void)d; return n; }
  bool end(bool evenIfRemaining = false) { (void)evenIfRemaining; return false; }
  void abort() {}
  Update_error_t getError() { return UPDATE_ERROR_FLASH_VERIFY_FAILED; }
  const char *errorString() { return "kein Update im Simulator"; }
};
extern UpdateClass Update;

#endif // RCT_PANEL_SIM_UPDATE_H
