// The English texts - what -DRCT_LANG_EN builds.
//
// Same order and same IDs as LangDe.cpp, one line per ID, which is what
// tools/i18n_test checks. Two rules for this file:
//
//   * no German: umlauts and a sharp s are what a leftover German text looks
//     like, and the test fails on them. English texts are plain ASCII.
//   * the same placeholders as the German line, in the same order. A sentence
//     with a value in it takes a %s or %d here too, or the value would simply
//     be missing from the page.
//
// SPDX-License-Identifier: MIT
#include "i18n/Lang.h"

#if defined(RCT_LANG_EN)

const char *const kLang[T_COUNT] = {
    // --- web: navigation and page titles -----------------------------------
    "Home",                             // T_NAV_HOME
    "History",                          // T_NAV_VERLAUF
    "Data",                             // T_NAV_DATA
    "Pictures",                         // T_NAV_SHOTS
    "Update",                           // T_NAV_UPDATE
    "Settings",                         // T_NAV_SETTINGS
    "RCT Power Panel",                  // T_PAGE_ROOT
    "History",                          // T_PAGE_VERLAUF
    "Data",                             // T_PAGE_DATA
    "Pictures",                         // T_PAGE_SHOTS
    "Update",                           // T_PAGE_UPDATE
    "Settings",                         // T_PAGE_SETTINGS
    "Home page",                        // T_MSG_TO_HOME
    "lang=\"en\"",                       // T_HTML_LANG

    // --- web: the heading over the four value cards -------------------------
    "Current",                         // T_H_CURRENT

    // --- web: the four value cards on the overview -------------------------
    "Grid",                              // T_CARD_GRID
    "PV",                               // T_CARD_PV
    "Battery",                          // T_CARD_BATTERY
    "Load",                             // T_CARD_LOAD

    // --- web: the overview tables ------------------------------------------
    "Device",                           // T_H_DEVICE
    "Inverter",                         // T_ROW_INVERTER
    "connected",                        // T_ROW_INVERTER_OK
    "not reachable",                    // T_ROW_INVERTER_NO
    "waiting",                          // T_ROW_INVERTER_WAIT
    "asleep (normal)",                  // T_ROW_INVERTER_ASLEEP
    "Name",                             // T_ROW_INVERTER_NAME
    "Controller firmware",              // T_ROW_CONTROLLER
    "Panel firmware",                   // T_ROW_FW_PANEL
    "Uptime",                           // T_ROW_UPTIME
    "Free memory",                      // T_ROW_FREEMEM
    "SD card",                          // T_ROW_SDCARD
    "SD clock",                         // T_ROW_SDCLK
    "no card",                          // T_ROW_SDCLK_NONE
    "Address here",                     // T_ROW_ADDR
    "As name",                          // T_ROW_ADDR_NAME

    // --- web: the switched output ------------------------------------------
    "Output",                           // T_H_OUTPUT
    "The output switches on when the value stays above the threshold for "
    "20 s, and stays on for at least 60 s after switching. Surplus means "
    "energy is being fed into the grid (the grid power is below zero). If the "
    "inverter is unreachable for "
    "ten minutes, the output switches off; until then it works with the last "
    "value received.",                                  // T_NOTE_OUTPUT
    "Function",                         // T_ROW_FUNCTION
    "State",                            // T_ROW_STATE
    "Test running",                     // T_STATE_TEST
    "on",                               // T_STATE_ON
    "off",                              // T_STATE_OFF
    "%s &middot; %d W now",             // T_STATE_ON_NOW
    "Threshold in watts",               // T_TITLE_THRESHOLD
    "Apply",                            // T_BTN_APPLY
    "Test: 5 s on, 5 s off",             // T_BTN_TEST
    "Off (never switches)",             // T_RELAY_LONG_OFF
    "Grid draw above threshold",        // T_RELAY_LONG_GRID
    "Surplus above threshold",          // T_RELAY_LONG_SURPLUS
    "Inverter fault",                   // T_RELAY_LONG_FAULT
    "Island mode (grid disconnected)",  // T_RELAY_LONG_ISLAND
    "Off",                              // T_RELAY_SHORT_OFF
    "Grid draw",                        // T_RELAY_SHORT_GRID
    "Surplus",                          // T_RELAY_SHORT_SURPLUS
    "Fault",                            // T_RELAY_SHORT_FAULT
    "Island mode",                      // T_RELAY_SHORT_ISLAND

    // --- web: maintenance ----------------------------------------------------
    "Maintenance",                      // T_H_MAINT
    "Update, restart and Wi-Fi setup require the 4-digit code. It is shown on "
    "the panel's <em>Service</em> page.",                 // T_NOTE_MAINT
    "Restart panel",                    // T_BTN_RESTART
    "Set up Wi-Fi again",               // T_BTN_SETUP

    // --- web: the two listing pages ------------------------------------------
    "download",                         // T_LINK_LOAD
    "view",                             // T_LINK_VIEW
    "Reload page",                      // T_BTN_RELOAD
    "Take screenshot",                  // T_BTN_SHOT
    "Capture",                          // T_H_CAPTURE
    "There is nothing on the SD card in <code>%s</code>.",   // T_NOTE_EMPTY
    "The &bdquo;download&ldquo; button fetches the last 64 kB (about two "
    "days). Put <code>?tail=0</code> into the link for the whole file. While a "
    "download runs, the panel does not answer any further requests.",
                                                        // T_NOTE_TAIL
    "More files on the card than fit here. The remaining files can be opened "
    "directly by their name: <code>%s/filename</code>.", // T_NOTE_TRUNC
    "A capture is being written right now. The page reloads itself once in a "
    "few seconds, then the new file is at the top.",  // T_NOTE_SHOT_RUN
    "The capture shows the current page. The image lands on the card as "
    "<code>shot...bmp</code>; the page reloads itself once afterwards.",
                                                       // T_NOTE_SHOT_IDLE

    // --- web: the charts -------------------------------------------------
    "Could not load the data.",                                 // T_ERR_LOAD_FAILED
    "No measurements yet.",                                     // T_NOTE_NO_DATA
    "Updated %s.",                                              // T_NOTE_UPDATED
    "Refreshes itself every 5 seconds.",                       // T_NOTE_REFRESHED
    "The last 24 hours",                                        // T_NOTE_LIVE
    "Loading the log &hellip;",                            // T_NOTE_LOADING
    "There is no file for this period on the card.",         // T_NOTE_NO_FILE
    "Some rows have no sums (from before the update): days entirely before it "
    "show 0, and a period across the change starts at the first row that "
    "has them.",                                       // T_NOTE_OLD_SUMS
    "24 h",                                                     // T_RANGE_LIVE
    "Day",                                                      // T_RANGE_DAY
    "Week",                                                     // T_RANGE_WEEK
    "Month",                                                    // T_RANGE_MONTH
    "Back",                                                     // T_BTN_PREV
    "Forward",                                                  // T_BTN_NEXT

    // --- web: /update --------------------------------------------------------
    "Update firmware",                  // T_H_FIRMWARE
    "Write firmware",                   // T_BTN_FW_WRITE
    "Choose the <code>firmware.bin</code> file from the build folder. The "
    "panel stays usable while it is being written and restarts afterwards. If "
    "an update goes wrong, the panel carries on with the previous firmware.",
                                                      // T_NOTE_FIRMWARE

    // --- web: the settings page --------------------------------------------
    "Inverter",                         // T_H_SET_DEVICE
    "Display",                          // T_H_SET_THEME
    "Type",                             // T_LBL_DEVICE_TYPE
    "Address",                          // T_LBL_DEVICE_HOST
    "Port",                             // T_LBL_DEVICE_PORT
    "Background",                       // T_LBL_THEME
    "RCT Power (TCP 8899)",             // T_OPT_TYPE_RCT
    "OpenInverterGateway (HTTP 80)",    // T_OPT_TYPE_OIG
    "Save",                             // T_BTN_SAVE
    "The settings are stored permanently. The panel restarts so that the "
    "chosen driver and the new address take effect.",
                                                      // T_NOTE_RESTART
    "Saved. The panel is restarting.",  // T_OK_SAVED
    "Unknown device type.",             // T_ERR_BAD_TYPE
    "Please enter an address.",         // T_ERR_BAD_HOST
    "Please enter a number from 1 to 65535.",  // T_ERR_BAD_PORT

    // --- web: the answers to an action ---------------------------------------
    "Firmware written. The panel is restarting now.",   // T_OK_FW_WRITTEN
    "The update was not carried out. Please try again.",  // T_ERR_FW_NOTRUN
    "The update could not be completed. The old firmware keeps running.",
                                                        // T_ERR_FW_END
    "The panel is restarting.",         // T_OK_RESTART
    "The panel is starting Wi-Fi setup (network: RCT-Panel).",  // T_OK_SETUP
    "Applied. The panel shows the new function on the Service page.",
                                                        // T_OK_APPLIED
    "Test running: 5 s on, 5 s off, twice.",         // T_OK_TEST_RUN
    "A test is already running.",        // T_ERR_TEST_RUNNING
    "The code is not correct. It is shown on the panel's Service page.",
                                                        // T_ERR_CODE
    "A capture is already running, or there is no SD card in the panel.",
                                                     // T_ERR_SHOT_RUNNING
    "Unknown function.",                // T_ERR_FUNCTION
    "Unknown action.",                  // T_ERR_ACTION
    "This page does not exist.",        // T_ERR_PAGE

    // --- web: the answers to a download --------------------------------------
    "A download is already running.",   // T_ERR_DOWNLOAD_RUNNING
    "A firmware is being written right now.",   // T_ERR_FW_WRITING
    "No SD card.",                      // T_ERR_NO_SDCARD
    "The file does not exist, is 0 bytes long (for pictures: a capture that "
    "did not finish) or the card is gone.",        // T_ERR_NO_FILE
    "Unknown file.",                    // T_ERR_NO_FILE_DIR
    "The file could not be read from the card.",    // T_ERR_READ_FAILED
    "The SD card could not be read.",   // T_ERR_SD_UNREADABLE

    // --- the SD status line, shown on the display and in the web overview ----
    "SD: -- | %d queued (%s)",          // T_SD_QUEUED_NCARD
    "SD: OK | %u %s lost",              // T_SD_LOST
    "row",                               // T_SD_ROW
    "rows",                              // T_SD_ROWS
    "SD: OK | %d queued (%s) | %.1f GB free",   // T_SD_QUEUED
    "SD: OK | %.1f GB free",            // T_SD_FREE
    "SD: --",                            // T_SD_OK

    // --- Anzeige: die Seitenüberschrift ----------------------------
    "Overview",      // T_D_HEAD_OVERVIEW
    "Energy",        // T_D_HEAD_ENERGY
    "Today",         // T_D_HEAD_HEUTE
    "24 h history",  // T_D_HEAD_GRAPH
    "Info",          // T_D_HEAD_INFO
    "Battery",       // T_D_HEAD_BATTERY
    "Service",       // T_D_HEAD_SERVICE

    // --- Anzeige: Übersicht, die vier Zeilen unter dem Flussdiagramm ---
    "Production",   // T_D_ROW_PRODUCTION
    "Consumption",  // T_D_ROW_CONSUMPTION
    "Battery",      // T_D_ROW_BATTERY

    // --- Anzeige: was diese Zeilen gerade sagen (Portal-Tendenzwörter) ---
    "Grid draw",    // T_D_TEND_MAINS
    "Standalone",   // T_D_TEND_SELF
    "Inactive",     // T_D_TEND_INACTIVE
    "Load",         // T_D_TEND_NOLOAD
    "Discharging",  // T_D_TEND_DISCHARGE
    "Charging",     // T_D_TEND_CHARGE
    "Standby",      // T_D_TEND_STANDBY
    "no battery",   // T_D_TEND_NOBAT

    // --- Anzeige: Energie, die fünf Zeilen und die vier Zeiträume ---
    "PV production",     // T_D_EN_PV
    "Self-consumption",  // T_D_EN_SELFUSE
    "Grid export",       // T_D_EN_EXPORT
    "Grid import",       // T_D_EN_IMPORT
    "Consumption",       // T_D_EN_LOAD
    "Day",               // T_D_PER_DAY
    "Month",             // T_D_PER_MONTH
    "Year",              // T_D_PER_YEAR
    "Total",             // T_D_PER_TOTAL

    // --- Anzeige: Heute, die sieben Karten -------------------------
    "Produced",                // T_D_CARD_PRODUCED
    "Self-consumed",           // T_D_CARD_SELFUSE
    "Fed in",                  // T_D_CARD_FEDIN
    "Consumed",                // T_D_CARD_CONSUMED
    "Imported",                // T_D_CARD_IMPORTED
    "Self-sufficiency",        // T_D_CARD_SELF
    "Self-consumption share",  // T_D_CARD_SELFRATE

    // --- Anzeige: 24 h Verlauf -------------------------------------
    "Grid",                               // T_D_SER_GRID
    "Consumption",                        // T_D_SER_CONSUMPTION
    "PV",                                 // T_D_SER_PV
    "EXT",                                // T_D_SER_EXT
    "Battery",                            // T_D_SER_BATTERY
    "SOC",                                // T_D_SER_SOC
    "1 gap, %lu min without readings",    // T_D_GAP_ONE
    "%d gaps, %lu min without readings",  // T_D_GAP_MANY

    // --- Anzeige: Info ---------------------------------------------
    "Name:",            // T_D_IF_NAME
    "Software:",        // T_D_IF_SOFTWARE
    "RCT host:",        // T_D_IF_HOST
    "RCT port:",        // T_D_IF_PORT
    "Link:",            // T_D_IF_LINK
    "Last data:",       // T_D_IF_LASTDATA
    "Uptime:",          // T_D_IF_UPTIME
    "Grid L1:",         // T_D_IF_L1
    "Grid L2:",         // T_D_IF_L2
    "Grid L3:",         // T_D_IF_L3
    "PV:",              // T_D_IF_PV
    "Core:",            // T_D_IF_CORE
    "Heatsink:",        // T_D_IF_HEATSINK
    "Grid frequency:",  // T_D_IF_FREQ
    "connected",        // T_D_LINK_UP
    "offline",          // T_D_LINK_DOWN
    "%lu s ago",        // T_D_LASTDATA_AGO

    // --- Anzeige: Akku ---------------------------------------------
    "Battery SOC:",      // T_D_BA_SOC
    "Battery:",          // T_D_BA_POWER
    "Battery temp:",     // T_D_BA_TEMP
    "Calibration:",      // T_D_BA_CALIB
    "Cycles:",           // T_D_BA_CYCLES
    "SOH:",              // T_D_BA_SOH
    "Island mode:",      // T_D_BA_ISLAND
    "yes",               // T_D_YES
    "no",                // T_D_NO
    "%s (overdue)",      // T_D_CALIB_OVERDUE
    "%s (today)",        // T_D_CALIB_TODAY
    "%s (in %ld days)",  // T_D_CALIB_DAYS

    // --- Anzeige: der ausgelesene Batteriezustand auf der Service-Seite ---
    "Ready",                    // T_D_ST_READY
    "Calibration (charge)",     // T_D_ST_CALCHARGE
    "Calibration (discharge)",  // T_D_ST_CALDISCHARGE
    "Balancing",                // T_D_ST_BALANCING
    "Undervoltage",             // T_D_ST_UNDERVOLT
    "Disconnected",             // T_D_ST_OFF
    "Status %lu",               // T_D_ST_RAW
    " + Balancing",             // T_D_ST_BALPLUS
    "  (discharging)",          // T_D_ST_DISCHARGING
    "  (charging)",             // T_D_ST_CHARGING

    // --- Anzeige: Service, die Abschnitte --------------------------
    "Battery status",         // T_D_SV_HEAD_BAT
    "SD log",                 // T_D_SV_HEAD_SD
    "Faults",                 // T_D_SV_HEAD_FAULTS
    "Web interface",          // T_D_SV_HEAD_WEB
    "Output",                 // T_D_SV_HEAD_OUTPUT
    "No faults",              // T_D_NO_FAULTS
    " ... and %d more",       // T_D_MORE_FAULTS
    "F%d %s\n",               // T_D_FAULT_LINE
    " Start setup",           // T_D_BTN_SETUP
    " Screenshot",            // T_D_BTN_SHOT
    "Test: 5 s on, 5 s off",  // T_D_BTN_TEST
    "Code: ----",             // T_D_CODE_EMPTY
    "Code: %s",               // T_D_CODE
    "tap = new one",          // T_D_CODE_HINT
    "IP: %s",                 // T_D_IP
    "no network",             // T_D_NO_NET

    // --- Anzeige: der geschaltete Ausgang --------------------------
    "%s > %d W",         // T_D_OUT_THRESHOLD
    "Test: %s",          // T_D_OUT_TEST
    "%s · %s",           // T_D_OUT_MODE
    "%s · %d W now",     // T_D_OUT_NOW
    "%s · %d W (last reading)", // T_D_OUT_NOW_STALE
    "ON",                // T_D_OUT_ON
    "OFF",               // T_D_OUT_OFF
    "nothing switched",  // T_D_OUT_NOTHING

    // --- Anzeige: die Screenshot-Ausgabe ---------------------------
    "A capture is already running",  // T_D_SHOT_BUSY
    "no SD card",                    // T_D_SHOT_NOCARD
    "Capture in %d s ...",           // T_D_SHOT_COUNT
    "writing",                       // T_D_SHOT_WRITING
    "saved to /shot",                // T_D_SHOT_SAVED
    "failed",                        // T_D_SHOT_FAILED
    "no memory",                     // T_D_SHOT_NOMEM

    // --- Anzeige: die WLAN-Einrichtung als Vollbild ----------------
    "Setup: configure Wi-Fi",  // T_D_AP_TITLE
    "Connect to the network and open\n"
    "http://192.168.4.1 in the browser", // T_D_AP_HINT

    // --- Anzeige: das Statusabzeichen in der Kopfzeile -------------
    "connecting",    // T_D_BADGE_CONNECTING
    "no data",       // T_D_BADGE_NODATA
    "live",          // T_D_BADGE_LIVE
    "reconnecting",  // T_D_BADGE_RECONNECT
    "waiting",       // T_D_BADGE_WAITING
    "asleep",        // T_D_BADGE_ASLEEP

    // --- Anzeige: die 128 Fehlertexte (Bit n aus fault[0..3].flt)
    "TRAP triggered",                                             // T_FAULT_0
    "RTC not configurable",                                       // T_FAULT_1
    "RTC 1 Hz signal timeout",                                    // T_FAULT_2
    "Hardware stop by 3.3 V fault",                               // T_FAULT_3
    "Hardware stop by PWM logic",                                 // T_FAULT_4
    "Hardware stop by Uzk overvoltage",                           // T_FAULT_5
    "Uzk+ above limit",                                           // T_FAULT_6
    "Uzk- above limit",                                           // T_FAULT_7
    "Choke overcurrent phase L1",                                 // T_FAULT_8
    "Choke overcurrent phase L2",                                 // T_FAULT_9
    "Choke overcurrent phase L3",                                 // T_FAULT_10
    "Buffer capacitor voltage",                                   // T_FAULT_11
    "Crystal fault",                                              // T_FAULT_12
    "Grid undervoltage phase 1",                                  // T_FAULT_13
    "Grid undervoltage phase 2",                                  // T_FAULT_14
    "Grid undervoltage phase 3",                                  // T_FAULT_15
    "Battery overcurrent",                                        // T_FAULT_16
    "Relay test failed",                                          // T_FAULT_17
    "Board overtemperature",                                      // T_FAULT_18
    "Core overtemperature",                                       // T_FAULT_19
    "Heatsink 1 overtemperature",                                 // T_FAULT_20
    "Heatsink 2 overtemperature",                                 // T_FAULT_21
    "I2C fault with power board",                                 // T_FAULT_22
    "Power board fault",                                          // T_FAULT_23
    "PWM outputs faulty",                                         // T_FAULT_24
    "Insulation too low or implausible",                          // T_FAULT_25
    "DC component I max (1 A)",                                   // T_FAULT_26
    "DC component I max slow (47 mA)",                            // T_FAULT_27
    "Possible DSD channel fault (offset too large)",              // T_FAULT_28
    "RS485 fault, relay box",                                     // T_FAULT_29
    "Overvoltage between phases",                                 // T_FAULT_30
    "IGBT L1 BH faulty",                                          // T_FAULT_31
    "IGBT L1 BL faulty",                                          // T_FAULT_32
    "IGBT L2 BH faulty",                                          // T_FAULT_33
    "IGBT L2 BL faulty",                                          // T_FAULT_34
    "IGBT L3 BH faulty",                                          // T_FAULT_35
    "IGBT L3 BL faulty",                                          // T_FAULT_36
    "Long-term overvoltage phase 1",                              // T_FAULT_37
    "Long-term overvoltage phase 2",                              // T_FAULT_38
    "Long-term overvoltage phase 3",                              // T_FAULT_39
    "Overvoltage phase 1, stage 1",                               // T_FAULT_40
    "Overvoltage phase 1, stage 2",                               // T_FAULT_41
    "Overvoltage phase 2, stage 1",                               // T_FAULT_42
    "Overvoltage phase 2, stage 2",                               // T_FAULT_43
    "Overvoltage phase 3, stage 1",                               // T_FAULT_44
    "Overvoltage phase 3, stage 2",                               // T_FAULT_45
    "Overfrequency, stage 1",                                     // T_FAULT_46
    "Overfrequency, stage 2",                                     // T_FAULT_47
    "Undervoltage phase 1, stage 1",                              // T_FAULT_48
    "Undervoltage phase 1, stage 2",                              // T_FAULT_49
    "Undervoltage phase 2, stage 1",                              // T_FAULT_50
    "Undervoltage phase 2, stage 2",                              // T_FAULT_51
    "Undervoltage phase 3, stage 1",                              // T_FAULT_52
    "Undervoltage phase 3, stage 2",                              // T_FAULT_53
    "Underfrequency, stage 1",                                    // T_FAULT_54
    "Underfrequency, stage 2",                                    // T_FAULT_55
    "CPU exception NMI",                                          // T_FAULT_56
    "CPU exception HardFault",                                    // T_FAULT_57
    "CPU exception MemManage",                                    // T_FAULT_58
    "CPU exception BusFault",                                     // T_FAULT_59
    "CPU exception UsageFault",                                   // T_FAULT_60
    "RTC power-on reset",                                         // T_FAULT_61
    "RTC oscillator stopped",                                     // T_FAULT_62
    "RTC supply voltage collapsed",                               // T_FAULT_63
    "RCD jump DC + AC > 30 mA",                                   // T_FAULT_64
    "RCD jump DC > 60 mA",                                        // T_FAULT_65
    "RCD jump AC > 150 mA",                                       // T_FAULT_66
    "RCD current > 300 mA",                                       // T_FAULT_67
    "+5 V faulty",                                                // T_FAULT_68
    "-9 V faulty",                                                // T_FAULT_69
    "+9 V faulty",                                                // T_FAULT_70
    "+3.3 V faulty",                                              // T_FAULT_71
    "RDC calibration failed",                                     // T_FAULT_72
    "I2C fault",                                                  // T_FAULT_73
    "AFI frequency generator fault",                              // T_FAULT_74
    "Heatsink temperature too high",                              // T_FAULT_75
    "Uzk above limit",                                            // T_FAULT_76
    "Usg A above limit",                                          // T_FAULT_77
    "Usg B above limit",                                          // T_FAULT_78
    "Switch-on condition Umin phase 1",                           // T_FAULT_79
    "Switch-on condition Umax phase 1",                           // T_FAULT_80
    "Switch-on condition Fmin phase 1",                           // T_FAULT_81
    "Switch-on condition Fmax phase 1",                           // T_FAULT_82
    "Switch-on condition Umin phase 2",                           // T_FAULT_83
    "Switch-on condition Umax phase 2",                           // T_FAULT_84
    "Battery current sensor faulty",                              // T_FAULT_85
    "Battery booster faulty",                                     // T_FAULT_86
    "Switch-on condition Umin phase 3",                           // T_FAULT_87
    "Switch-on condition Umax phase 3",                           // T_FAULT_88
    "Voltage step/offset at AC terminals too large (phase loss)", // T_FAULT_89
    "Inverter disconnected from the grid",                        // T_FAULT_90
    "+9 V difference DSP/PIC too large",                          // T_FAULT_91
    "1.5 V fault",                                                // T_FAULT_92
    "2.5 V fault",                                                // T_FAULT_93
    "1.5 V measurement difference",                               // T_FAULT_94
    "2.5 V measurement difference",                               // T_FAULT_95
    "Battery voltage outside expected range",                     // T_FAULT_96
    "PIC software will not start",                                // T_FAULT_97
    "PIC bootloader detected unexpectedly",                       // T_FAULT_98
    "Phase angle error (not 120°)",                               // T_FAULT_99
    "Battery overvoltage",                                        // T_FAULT_100
    "Choke current unstable",                                     // T_FAULT_101
    "Grid voltage difference internal/external too large phase"
    "1", // T_FAULT_102
    "Grid voltage difference internal/external too large phase"
    "2", // T_FAULT_103
    "Grid voltage difference internal/external too large phase"
    "3", // T_FAULT_104
    "External emergency stop active",         // T_FAULT_105
    "Battery empty: no energy for standby",   // T_FAULT_106
    "CAN timeout with battery",               // T_FAULT_107
    "Timing problem",                         // T_FAULT_108
    "Heatsink overtemperature battery IGBT",  // T_FAULT_109
    "Battery heatsink temperature too high",  // T_FAULT_110
    "Internal relay box fault",               // T_FAULT_111
    "Relay box PE off fault",                 // T_FAULT_112
    "Relay box PE on fault",                  // T_FAULT_113
    "Internal battery fault",                 // T_FAULT_114
    "Parameters changed",                     // T_FAULT_115
    "3 island formation attempts failed",     // T_FAULT_116
    "Undervoltage between phases",            // T_FAULT_117
    "System reset detected",                  // T_FAULT_118
    "Update detected",                        // T_FAULT_119
    "FRT overvoltage",                        // T_FAULT_120
    "FRT undervoltage",                       // T_FAULT_121
    "IGBT L1 freewheel diode faulty",         // T_FAULT_122
    "IGBT L2 freewheel diode faulty",         // T_FAULT_123
    "IGBT L3 freewheel diode faulty",         // T_FAULT_124
    "Single-phase mode active, not allowed for this device"
    "class", // T_FAULT_125
    "Island mode detected",     // T_FAULT_126
    "Neutral conductor fault",  // T_FAULT_127

    // --- Display: the background -------------------------------
    "Light theme",  // T_D_BTN_THEME_LIGHT
    "Dark theme",   // T_D_BTN_THEME_DARK
};

#endif // RCT_LANG_EN
