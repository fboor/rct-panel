// Shared RCT Power data snapshot consumed by the GUI.
#ifndef RCT_TYPES_H
#define RCT_TYPES_H

#include <Arduino.h>

// Latest known-good values from the RCT Power device, one entry per meter
// slot. Values keep their last good reading until refreshed (a partially
// responsive device must not blank the panel).
struct RctSnapshot {
  // True once prim_sm.island_flag has actually answered. The flag is 0 both
  // for "grid lost" and for "not received yet", so the GUI may only draw the
  // island warning after this is set - otherwise a device that is merely
  // quiet would be reported as islanding.
  float gridPower[3];     // g_sync.p_ac_sc[0..2]      grid power per phase [W]
  float gridPowerSum;     // g_sync.p_ac_grid_sum_lp   grid exchange [W], + = Bezug (import)
  float gridVoltage[3];   // rb485.u_l_grid[0..2]      grid voltage per phase [V]
  float gridFrequency[3]; // rb485.f_grid[0..2]        grid frequency per phase [Hz]
  float feedInEnergyWh;   // energy.e_grid_feed_total  lifetime feed-in [Wh] (already Wh)
  float loadEnergyWh;     // energy.e_grid_load_total  lifetime grid draw [Wh] (already Wh)

  // Current-day totals (portal "Übersicht"/"Energiestatistiken", Heute).
  float dayPvWh;        // energy.e_dc_day[0]+[1]      generated today [Wh]
  float dayFeedInWh;    // energy.e_grid_feed_day      fed into the grid [Wh]
  float dayLoadWh;      // energy.e_load_day           household load [Wh]
  float dayGridLoadWh;  // energy.e_grid_load_day      drawn from grid [Wh]

  // Month / year totals for the "Energie" page. The lifetime ("Gesamt")
  // counterparts already live above: feedInEnergyWh = e_grid_feed_total and
  // loadEnergyWh = e_grid_load_total (both grid meters, despite the name).
  float monthPvWh;      // energy.e_dc_month[0]+[1]    generated this month [Wh]
  float yearPvWh;       // energy.e_dc_year[0]+[1]     generated this year [Wh]
  float totalPvWh;      // energy.e_dc_total[0]+[1]    generated lifetime [Wh]
  float monthLoadWh;    // energy.e_load_month         household this month [Wh]
  float yearLoadWh;     // energy.e_load_year          household this year [Wh]
  float totalLoadWh;    // energy.e_load_total         household lifetime [Wh]
  float monthFeedInWh;  // energy.e_grid_feed_month    feed-in this month [Wh]
  float yearFeedInWh;   // energy.e_grid_feed_year     feed-in this year [Wh]
  float monthGridLoadWh; // energy.e_grid_load_month   grid draw this month [Wh]
  float yearGridLoadWh;  // energy.e_grid_load_year    grid draw this year [Wh]

  float loadPower[3];   // g_sync.p_ac_load[0..2]      household load per phase [W]
  float pvPower[2];     // dc_conv.dc_conv_struct[i].p_dc_lp  solar gen A/B [W]
  float s0Power;        // io_board.s0_external_power         S0 meter [W], 0 while absent
  // Energy integrated from s0Power, in Wh, accumulated since boot. The device
  // provides no S0 energy counter (its e_dc_* meters cover the two DC inputs
  // only; the registry lists just io2_s0_imp_per_kwh, which counts pulses, not
  // energy), so "PV Erzeugung" can only include the external generator by
  // integrating its power here. For the day period that is the day's
  // production; for month/year/lifetime it is a lower bound, since it only
  // covers the time since the panel was switched on.
  float s0EnergyWh;
  float batterySoc;     // battery.soc     [%]
  // Sign convention, measured on the real device: PV 0 W | Haus 832 W |
  // Netz +4 W | Batterie +810 W. With no production the battery cannot be
  // charging, and 810 + 4 balances the 832 W the house draws, so positive is
  // discharging. The current register uses the opposite sign to the power, so
  // it is reconciled against U*I vs P in RctClient.cpp before it is stored.
  float batteryCurrent; // battery.current [A]; positive = discharging
  float batteryVoltage; // battery.voltage [V]
  float batteryPower;   // g_sync.p_acc_lp [W]; positive = discharging

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
  // prim_sm.island_flag is a bitfield; only bit 0 is the island flag. Measured
  // on the real device: 0x00000002 while running normally on the grid.
  bool islandMode;          // island (grid-separated) mode
  bool islandKnown;         // island flag has answered at least once

  bool haveData;      // any value ever received from the device
  bool haveBattery;   // battery.soc ever answered (device has a battery)
  bool connected;     // TCP link up right now
  uint32_t lastUpdateMs; // timestamp of last received frame
};

extern RctSnapshot rctState;

#endif // RCT_TYPES_H