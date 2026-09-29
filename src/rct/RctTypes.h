// Shared RCT Power data snapshot consumed by the GUI.
#ifndef RCT_TYPES_H
#define RCT_TYPES_H

#include <Arduino.h>

// Latest known-good values from the RCT Power device, one entry per meter
// slot. Values keep their last good reading until refreshed (a partially
// responsive device must not blank the panel).
struct RctSnapshot {
  float gridPower[3];     // g_sync.p_ac_sc[0..2]      grid power per phase [W]
  float gridVoltage[3];   // rb485.u_l_grid[0..2]      grid voltage per phase [V]
  float gridFrequency[3]; // rb485.f_grid[0..2]        grid frequency per phase [Hz]
  float feedInEnergyWh;   // energy.e_grid_feed_total  [Wh] (already Wh)
  float loadEnergyWh;     // energy.e_grid_load_total  [Wh] (already Wh)

  // Current-day totals (portal "Übersicht"/"Energiestatistiken", Heute).
  float dayPvWh;        // energy.e_dc_day[0]+[1]      generated today [Wh]
  float dayFeedInWh;    // energy.e_grid_feed_day      fed into the grid [Wh]
  float dayLoadWh;      // energy.e_load_day           household load [Wh]
  float dayGridLoadWh;  // energy.e_grid_load_day      drawn from grid [Wh]

  float loadPower[3];   // g_sync.p_ac_load[0..2]      household load per phase [W]
  float pvPower[2];     // dc_conv.dc_conv_struct[i].p_dc_lp  solar gen A/B [W]
  float s0Power;        // io_board.s0_external_power         S0 meter [W], 0 while absent
  float batterySoc;     // battery.soc     [%]
  float batteryCurrent; // battery.current [A]
  float batteryVoltage; // battery.voltage [V]
  float batteryPower;   // g_sync.p_acc_lp [W]; positive = charging

  // Service page: battery status bitfield + inverter fault bitfields.
  uint32_t batteryStatus; // battery.bat_status    status bitfield (INT32)
  uint32_t faultBits[4];  // fault[0..3].flt        128 fault bits (UINT32)

  // Device info (page "Gerät"). Kept as its own group so it can be tuned
  // independently (currently polled on the same cadence as the fast values).
  char deviceName[40];      // android_description          device name
  char firmwareVersion[24]; // svnversion                   control software version
  float coreTemp;           // db.core_temp                 core temperature [°C]
  float batteryTemp;        // battery.temperature          battery temperature [°C]
  float heatSinkTemp;       // db.temp1                     heat sink temperature [°C]
  uint32_t nextCalibTs;     // power_mng.bat_next_calib_date next calibration [unix s]
  float batteryCycles;      // battery.cycles               charge/discharge cycles
  float batterySoh;         // battery.soh                  state of health [%]
  bool islandMode;          // prim_sm.island_flag != 0     island (grid-separated) mode

  bool haveData;      // any value ever received from the device
  bool haveBattery;   // battery.soc ever answered (device has a battery)
  bool connected;     // TCP link up right now
  uint32_t lastUpdateMs; // timestamp of last received frame
};

extern RctSnapshot rctState;

#endif // RCT_TYPES_H