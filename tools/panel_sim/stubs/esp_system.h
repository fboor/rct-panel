// A stand-in for the few esp_system.h symbols the firmware uses.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_PANEL_SIM_ESP_SYSTEM_H
#define RCT_PANEL_SIM_ESP_SYSTEM_H
#include <cstdint>
inline uint32_t esp_random() { return 0; }
#endif // RCT_PANEL_SIM_ESP_SYSTEM_H
