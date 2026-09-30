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
    "Data",                             // T_NAV_DATA
    "Pictures",                         // T_NAV_SHOTS
    "Update",                           // T_NAV_UPDATE
    "RCT Power Panel",                  // T_PAGE_ROOT
    "Data",                             // T_PAGE_DATA
    "Pictures",                         // T_PAGE_SHOTS
    "Update",                           // T_PAGE_UPDATE
    "Home page",                        // T_MSG_TO_HOME
    "lang=\"en\"",                       // T_HTML_LANG

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
    "Controller",                       // T_ROW_CONTROLLER
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
    "20 s, and stays on for at least 60 s after switching. Surplus means PV "
    "minus household load (including S0). If the inverter is unreachable for "
    "two minutes, the output switches off.",             // T_NOTE_OUTPUT
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
    "PV surplus above threshold",       // T_RELAY_LONG_SURPLUS
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

    // --- web: /update --------------------------------------------------------
    "Update firmware",                  // T_H_FIRMWARE
    "Write firmware",                   // T_BTN_FW_WRITE
    "Choose the <code>firmware.bin</code> file from the build folder. The "
    "panel stays usable while it is being written and restarts afterwards. If "
    "an update goes wrong, the panel carries on with the previous firmware.",
                                                      // T_NOTE_FIRMWARE
    "Firmware written. The panel is restarting now.",   // T_OK_FW_WRITTEN
    "The update was not carried out. Please try again.",  // T_ERR_FW_NOTRUN
    "The update could not be completed. The old firmware keeps running.",
                                                        // T_ERR_FW_END

    // --- web: the answers to an action ---------------------------------------
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
};

#endif // RCT_LANG_EN
