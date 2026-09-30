// The German texts - the language a build without -DRCT_LANG_EN gets.
//
// This file is the reference: every entry is the wording the panel and the web
// interface have been showing. LangEn.cpp holds the English side; the two are
// kept in the same order, one ID per line, with the ID named at the end of the
// line - tools/i18n_test/check.py reads those names and fails if a line is
// missing, doubled or on the wrong side of an ID.
//
// The web texts carry HTML entities, the ones the display shows are plain
// UTF-8; see the note in Lang.h.
//
// SPDX-License-Identifier: MIT
#include "i18n/Lang.h"

#if !defined(RCT_LANG_EN)

const char *const kLang[T_COUNT] = {
    // --- web: navigation and page titles -----------------------------------
    "Start",                            // T_NAV_HOME
    "Daten",                            // T_NAV_DATA
    "Bilder",                           // T_NAV_SHOTS
    "Update",                           // T_NAV_UPDATE
    "RCT Power Panel",                  // T_PAGE_ROOT
    "Daten",                            // T_PAGE_DATA
    "Bilder",                           // T_PAGE_SHOTS
    "Update",                           // T_PAGE_UPDATE
    "Zur Startseite",                   // T_MSG_TO_HOME
    "lang=\"de\"",                       // T_HTML_LANG

    // --- web: the four value cards on the overview -------------------------
    "Netz",                             // T_CARD_GRID
    "PV",                               // T_CARD_PV
    "Batterie",                         // T_CARD_BATTERY
    "Verbrauch",                        // T_CARD_LOAD

    // --- web: the overview tables ------------------------------------------
    "Ger&auml;t",                       // T_H_DEVICE
    "Wechselrichter",                   // T_ROW_INVERTER
    "verbunden",                        // T_ROW_INVERTER_OK
    "nicht erreichbar",                 // T_ROW_INVERTER_NO
    "Steuerger&auml;t",                 // T_ROW_CONTROLLER
    "Firmware Panel",                   // T_ROW_FW_PANEL
    "Laufzeit",                         // T_ROW_UPTIME
    "Speicher frei",                    // T_ROW_FREEMEM
    "SD-Karte",                         // T_ROW_SDCARD
    "SD-Takt",                          // T_ROW_SDCLK
    "keine Karte",                      // T_ROW_SDCLK_NONE
    "Adresse hier",                     // T_ROW_ADDR
    "Als Name",                         // T_ROW_ADDR_NAME

    // --- web: the switched output ------------------------------------------
    "Ausgang",                          // T_H_OUTPUT
    "Der Ausgang schaltet ein, wenn der Wert 20 s lang &uuml;ber der "
    "Schwelle liegt, und bleibt nach dem Einschalten mindestens 60 s an. "
    "&Uuml;berschuss hei&szlig;t PV minus Hausverbrauch (mit S0). Ist der "
    "Wechselrichter zwei Minuten lang nicht erreichbar, schaltet der Ausgang "
    "aus.",                                                // T_NOTE_OUTPUT
    "Funktion",                         // T_ROW_FUNCTION
    "Zustand",                          // T_ROW_STATE
    "Test l&auml;uft",                  // T_STATE_TEST
    "ein",                              // T_STATE_ON
    "aus",                              // T_STATE_OFF
    "%s &middot; %d W jetzt",           // T_STATE_ON_NOW
    "Schwelle in Watt",                 // T_TITLE_THRESHOLD
    "&Uuml;bernehmen",                  // T_BTN_APPLY
    "Test: 5 s an, 5 s aus",             // T_BTN_TEST
    "Aus (schaltet nie)",               // T_RELAY_LONG_OFF
    "Netzbezug &uuml;ber Schwelle",     // T_RELAY_LONG_GRID
    "&Uuml;berschuss &uuml;ber Schwelle",// T_RELAY_LONG_SURPLUS
    "St&ouml;rung am Wechselrichter",   // T_RELAY_LONG_FAULT
    "Inselbetrieb (Netz getrennt)",     // T_RELAY_LONG_ISLAND
    "Aus",                              // T_RELAY_SHORT_OFF
    "Netzbezug",                        // T_RELAY_SHORT_GRID
    "Überschuss",                       // T_RELAY_SHORT_SURPLUS
    "Störung",                          // T_RELAY_SHORT_FAULT
    "Inselbetrieb",                     // T_RELAY_SHORT_ISLAND

    // --- web: maintenance ----------------------------------------------------
    "Wartung",                          // T_H_MAINT
    "Update, Neustart und WLAN-Einrichtung verlangen den 4-stelligen Code. "
    "Er steht auf der Panel-Seite <em>Service</em>.",     // T_NOTE_MAINT
    "Panel neu starten",                // T_BTN_RESTART
    "WLAN neu einrichten",              // T_BTN_SETUP

    // --- web: the two listing pages ------------------------------------------
    "laden",                            // T_LINK_LOAD
    "anzeigen",                         // T_LINK_VIEW
    "Seite neu laden",                  // T_BTN_RELOAD
    "Screenshot ausl&ouml;sen",         // T_BTN_SHOT
    "Aufnahme",                         // T_H_CAPTURE
    "Auf der SD-Karte liegt nichts in <code>%s</code>.",       // T_NOTE_EMPTY
    "Der Knopf &bdquo;laden&ldquo; holt die letzten 64 kB (etwa zwei "
    "Tage). Mit <code>?tail=0</code> im Link kommt die ganze Datei. "
    "W&auml;hrend ein Download l&auml;uft, bedient das Panel keine weiteren "
    "Anfragen.",                                                   // T_NOTE_TAIL
    "Mehr Dateien auf der Karte, als hier platzieren. &Uuml;brige "
    "Dateien lassen sich direkt &uuml;ber ihren Namen aufrufen: "
    "<code>%s/Dateiname</code>.",                                // T_NOTE_TRUNC
    "Eine Aufnahme wird gerade geschrieben. Die Seite l&auml;dt sich "
    "in ein paar Sekunden einmal neu, dann steht die neue Datei oben.",
                                                       // T_NOTE_SHOT_RUN
    "Die Aufnahme zeigt die aktuelle Seite. Das Bild landet als "
    "<code>shot...bmp</code> auf der Karte; die Seite l&auml;dt sich "
    "danach einmal neu.",                                       // T_NOTE_SHOT_IDLE

    // --- web: /update --------------------------------------------------------
    "Firmware aktualisieren",            // T_H_FIRMWARE
    "Firmware schreiben",                // T_BTN_FW_WRITE
    "Datei <code>firmware.bin</code> aus dem Build-Ordner w&auml;hlen. "
    "Das Panel bleibt w&auml;hrend des Schreibens bedienbar und startet "
    "danach neu. L&auml;uft ein Update schief, startet das Panel mit der "
    "bisherigen Firmware weiter.",                              // T_NOTE_FIRMWARE
    "Firmware geschrieben. Das Panel startet jetzt neu.",  // T_OK_FW_WRITTEN
    "Das Update wurde nicht ausgef&uuml;hrt. Bitte erneut versuchen.",
                                                      // T_ERR_FW_NOTRUN
    "Das Update liess sich nicht abschliessen. Die alte Firmware "
    "l&auml;uft weiter.",                                       // T_ERR_FW_END

    // --- web: the answers to an action ---------------------------------------
    "Das Panel startet neu.",            // T_OK_RESTART
    "Das Panel startet das WLAN-Setup (Zugang: RCT-Panel).",    // T_OK_SETUP
    "&Uuml;bernommen. Das Panel zeigt die neue Funktion auf der Seite Service.",
                                                        // T_OK_APPLIED
    "Test laeuft: 5 s ein, 5 s aus, zweimal.",             // T_OK_TEST_RUN
    "Ein Test laeuft bereits.",                           // T_ERR_TEST_RUNNING
    "Der Code stimmt nicht. Er steht auf der Panel-Seite Service.", // T_ERR_CODE
    "Es l&auml;uft schon eine Aufnahme, oder es steckt keine SD-Karte im "
    "Panel.",                                           // T_ERR_SHOT_RUNNING
    "Unbekannte Funktion.",               // T_ERR_FUNCTION
    "Unbekannte Aktion.",                 // T_ERR_ACTION
    "Diese Seite gibt es nicht.",         // T_ERR_PAGE

    // --- web: the answers to a download --------------------------------------
    "Es l&auml;uft bereits ein Download.",   // T_ERR_DOWNLOAD_RUNNING
    "Gerade wird eine Firmware geschrieben.",// T_ERR_FW_WRITING
    "Keine SD-Karte.",                    // T_ERR_NO_SDCARD
    "Die Datei gibt es nicht, ist 0 Bytes lang (bei Bildern: eine Aufnahme, "
    "die nicht fertig wurde) oder der Stick ist weg.",      // T_ERR_NO_FILE
    "Unbekannte Datei.",                   // T_ERR_NO_FILE_DIR
    "Die Datei liess sich nicht vom Stick lesen.",  // T_ERR_READ_FAILED
    "Die SD-Karte liess sich nicht lesen.",       // T_ERR_SD_UNREADABLE

    // --- the SD status line, shown on the display and in the web overview ----
    "SD: -- | %d gepuffert (%s)",          // T_SD_QUEUED_NCARD
    "SD: OK | %u %s verloren",             // T_SD_LOST
    "Zeile",                               // T_SD_ROW
    "Zeilen",                              // T_SD_ROWS
    "SD: OK | %d gepuffert (%s) | %.1f GB frei",  // T_SD_QUEUED
    "SD: OK | %.1f GB frei",               // T_SD_FREE
    "SD: --",                              // T_SD_OK
};

#endif // !RCT_LANG_EN
