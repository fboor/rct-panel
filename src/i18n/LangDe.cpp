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
    "Verlauf",                          // T_NAV_VERLAUF
    "Daten",                            // T_NAV_DATA
    "Bilder",                           // T_NAV_SHOTS
    "Update",                           // T_NAV_UPDATE
    "RCT Power Panel",                  // T_PAGE_ROOT
    "Verlauf",                          // T_PAGE_VERLAUF
    "Daten",                            // T_PAGE_DATA
    "Bilder",                           // T_PAGE_SHOTS
    "Update",                           // T_PAGE_UPDATE
    "Zur Startseite",                   // T_MSG_TO_HOME
    "lang=\"de\"",                       // T_HTML_LANG

    // --- web: die Ueberschrift ueber den vier Kacheln ----------------------
    "Aktuell",                         // T_H_CURRENT

    // --- web: the four value cards on the overview -------------------------
    "Netz",                             // T_CARD_GRID
    "PV",                               // T_CARD_PV
    "Akku",                             // T_CARD_BATTERY
    "Verbrauch",                        // T_CARD_LOAD

    // --- web: the overview tables ------------------------------------------
    "Ger&auml;t",                       // T_H_DEVICE
    "Wechselrichter",                   // T_ROW_INVERTER
    "verbunden",                        // T_ROW_INVERTER_OK
    "nicht erreichbar",                 // T_ROW_INVERTER_NO
    "wartet",                           // T_ROW_INVERTER_WAIT
    "Name",                             // T_ROW_INVERTER_NAME
    "Firmware Steuerger&auml;t",        // T_ROW_CONTROLLER
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
    "&Uuml;berschuss hei&szlig;t: es wird eingespeist (die Netzleistung "
    "liegt unter null). Ist der "
    "Wechselrichter zehn Minuten lang nicht erreichbar, schaltet der Ausgang "
    "aus; bis dahin arbeitet er mit dem letzten empfangenen Wert.",
                                                       // T_NOTE_OUTPUT
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

    // --- web: die Diagramme --------------------------------------------
    "Daten konnten nicht geladen werden.",           // T_ERR_LOAD_FAILED
    "Noch keine Messwerte.",                         // T_NOTE_NO_DATA
    "Stand %s.",                                     // T_NOTE_UPDATED
    "Aktualisiert sich alle 5 Sekunden.",            // T_NOTE_REFRESHED
    "Die letzten 24 Stunden",                        // T_NOTE_LIVE
    "Lade die Aufzeichnung …",                       // T_NOTE_LOADING
    "F&uuml;r diesen Zeitraum liegt keine Datei auf der Karte.", // T_NOTE_NO_FILE
    "Teile der Zeilen haben keine Summen (von vor dem Update): Tage ganz "
    "davor zeigen 0, ein Zeitraum über den Wechsel beginnt mit der ersten "
    "Zeile, die Summen hat.",                           // T_NOTE_OLD_SUMS
    "24 h",                                          // T_RANGE_LIVE
    "Tag",                                           // T_RANGE_DAY
    "Woche",                                         // T_RANGE_WEEK
    "Monat",                                         // T_RANGE_MONTH
    "Zur&uuml;ck",                                  // T_BTN_PREV
    "Weiter",                                        // T_BTN_NEXT

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

    // --- Anzeige: die Seitenüberschrift ----------------------------
    "Übersicht",     // T_D_HEAD_OVERVIEW
    "Energie",       // T_D_HEAD_ENERGY
    "Heute",         // T_D_HEAD_HEUTE
    "24 h Verlauf",  // T_D_HEAD_GRAPH
    "Info",          // T_D_HEAD_INFO
    "Akku",          // T_D_HEAD_BATTERY
    "Service",       // T_D_HEAD_SERVICE

    // --- Anzeige: Übersicht, die vier Zeilen unter dem Flussdiagramm ---
    "Erzeugung",  // T_D_ROW_PRODUCTION
    "Verbrauch",  // T_D_ROW_CONSUMPTION
    "Batterie",   // T_D_ROW_BATTERY

    // --- Anzeige: was diese Zeilen gerade sagen (Portal-Tendenzwörter) ---
    "Netzbezug",       // T_D_TEND_MAINS
    "Unabhängig",      // T_D_TEND_SELF
    "Inaktiv",         // T_D_TEND_INACTIVE
    "kein Verbrauch",  // T_D_TEND_NOLOAD
    "Entladen",        // T_D_TEND_DISCHARGE
    "Laden",           // T_D_TEND_CHARGE
    "Standby",         // T_D_TEND_STANDBY
    "keine Batterie",  // T_D_TEND_NOBAT

    // --- Anzeige: Energie, die fünf Zeilen und die vier Zeiträume ---
    "PV Erzeugung",     // T_D_EN_PV
    "Eigenverbrauch",   // T_D_EN_SELFUSE
    "Netzeinspeisung",  // T_D_EN_EXPORT
    "Netzbezug",        // T_D_EN_IMPORT
    "Verbrauch",        // T_D_EN_LOAD
    "Tag",              // T_D_PER_DAY
    "Monat",            // T_D_PER_MONTH
    "Jahr",             // T_D_PER_YEAR
    "Gesamt",           // T_D_PER_TOTAL

    // --- Anzeige: Heute, die sieben Karten -------------------------
    "Erzeugt",         // T_D_CARD_PRODUCED
    "Eigenverbrauch",  // T_D_CARD_SELFUSE
    "Eingespeist",     // T_D_CARD_FEDIN
    "Verbrauch",       // T_D_CARD_CONSUMED
    "Bezug",           // T_D_CARD_IMPORTED
    "Autarkie",        // T_D_CARD_SELF
    "Eigenverbrauchsquote", // T_D_CARD_SELFRATE

    // --- Anzeige: 24 h Verlauf -------------------------------------
    "Netz",                               // T_D_SER_GRID
    "Verbrauch",                          // T_D_SER_CONSUMPTION
    "PV",                                 // T_D_SER_PV
    "EXT",                                // T_D_SER_EXT
    "Akku",                                // T_D_SER_BATTERY
    "SOC",                                // T_D_SER_SOC
    "1 Lücke, %lu min ohne Messwerte",    // T_D_GAP_ONE
    "%d Lücken, %lu min ohne Messwerte",  // T_D_GAP_MANY

    // --- Anzeige: Info ---------------------------------------------
    "Name:",          // T_D_IF_NAME
    "Software:",      // T_D_IF_SOFTWARE
    "RCT host:",      // T_D_IF_HOST
    "RCT port:",      // T_D_IF_PORT
    "Link:",          // T_D_IF_LINK
    "Last data:",     // T_D_IF_LASTDATA
    "Uptime:",        // T_D_IF_UPTIME
    "Netz L1:",       // T_D_IF_L1
    "Netz L2:",       // T_D_IF_L2
    "Netz L3:",       // T_D_IF_L3
    "PV:",            // T_D_IF_PV
    "Kern:",          // T_D_IF_CORE
    "Kühlkörper:",    // T_D_IF_HEATSINK
    "Netzfrequenz:",  // T_D_IF_FREQ
    "verbunden",      // T_D_LINK_UP
    "getrennt",       // T_D_LINK_DOWN
    "vor %lu s",      // T_D_LASTDATA_AGO

    // --- Anzeige: Akku ---------------------------------------------
    "Batterie-SOC:",      // T_D_BA_SOC
    "Batterie:",          // T_D_BA_POWER
    "Batterie-Temp:",     // T_D_BA_TEMP
    "Kalibrierung:",      // T_D_BA_CALIB
    "Zyklen:",            // T_D_BA_CYCLES
    "SOH:",               // T_D_BA_SOH
    "Inselbetrieb:",      // T_D_BA_ISLAND
    "ja",                 // T_D_YES
    "nein",               // T_D_NO
    "%s (überfällig)",    // T_D_CALIB_OVERDUE
    "%s (heute)",         // T_D_CALIB_TODAY
    "%s (in %ld Tagen)",  // T_D_CALIB_DAYS

    // --- Anzeige: der ausgelesene Batteriezustand auf der Service-Seite ---
    "Bereit",                       // T_D_ST_READY
    "Kalibrierung (Ladephase)",     // T_D_ST_CALCHARGE
    "Kalibrierung (Entladephase)",  // T_D_ST_CALDISCHARGE
    "Balancing",                    // T_D_ST_BALANCING
    "Unterspannung",                // T_D_ST_UNDERVOLT
    "Getrennt",                     // T_D_ST_OFF
    "Status %lu",                   // T_D_ST_RAW
    " + Balancing",                 // T_D_ST_BALPLUS
    "  (entlaedt)",                 // T_D_ST_DISCHARGING
    "  (laedt)",                    // T_D_ST_CHARGING

    // --- Anzeige: Service, die Abschnitte --------------------------
    "Batterie-Status",        // T_D_SV_HEAD_BAT
    "SD-Log",                 // T_D_SV_HEAD_SD
    "Störungen",              // T_D_SV_HEAD_FAULTS
    "Web-Oberfläche",         // T_D_SV_HEAD_WEB
    "Ausgang",                // T_D_SV_HEAD_OUTPUT
    "Keine Störungen",        // T_D_NO_FAULTS
    " ... und %d weitere",    // T_D_MORE_FAULTS
    "F%d %s\n",               // T_D_FAULT_LINE
    " Setup starten",         // T_D_BTN_SETUP
    " Screenshot",            // T_D_BTN_SHOT
    "Test: 5 s an, 5 s aus",  // T_D_BTN_TEST
    "Code: ----",             // T_D_CODE_EMPTY
    "Code: %s",               // T_D_CODE
    "antippen = neu",         // T_D_CODE_HINT
    "IP: %s",                 // T_D_IP
    "kein Netz",              // T_D_NO_NET

    // --- Anzeige: der geschaltete Ausgang --------------------------
    "%s > %d W",          // T_D_OUT_THRESHOLD
    "Test: %s",           // T_D_OUT_TEST
    "%s · %s",            // T_D_OUT_MODE
    "%s · %d W jetzt",    // T_D_OUT_NOW
    "%s · %d W (letzte Messung)", // T_D_OUT_NOW_STALE
    "AN",                 // T_D_OUT_ON
    "AUS",                // T_D_OUT_OFF
    "nichts geschaltet",  // T_D_OUT_NOTHING

    // --- Anzeige: die Screenshot-Ausgabe ---------------------------
    "Aufnahme laeuft bereits",  // T_D_SHOT_BUSY
    "keine SD-Karte",           // T_D_SHOT_NOCARD
    "Aufnahme in %d s ...",     // T_D_SHOT_COUNT
    "wird geschrieben",         // T_D_SHOT_WRITING
    "auf /shot gespeichert",    // T_D_SHOT_SAVED
    "fehlgeschlagen",           // T_D_SHOT_FAILED
    "kein Speicher",            // T_D_SHOT_NOMEM

    // --- Anzeige: die WLAN-Einrichtung als Vollbild ----------------
    "Setup: WLAN konfigurieren",  // T_D_AP_TITLE
    "Mit dem Netzwerk verbinden und im Browser\n"
    "http://192.168.4.1 öffnen", // T_D_AP_HINT

    // --- Anzeige: das Statusabzeichen in der Kopfzeile -------------
    "verbinde",      // T_D_BADGE_CONNECTING
    "keine Daten",   // T_D_BADGE_NODATA
    "aktiv",         // T_D_BADGE_LIVE
    "verbinde neu",  // T_D_BADGE_RECONNECT
    "wartet",        // T_D_BADGE_WAITING

    // --- Anzeige: die 128 Fehlertexte (Bit n aus fault[0..3].flt)
    "TRAP ausgelöst",                               // T_FAULT_0
    "RTC nicht konfigurierbar",                     // T_FAULT_1
    "RTC-1-Hz-Signal-Timeout",                      // T_FAULT_2
    "Hardware-Stopp durch 3,3-V-Fehler",            // T_FAULT_3
    "Hardware-Stopp durch PWM-Logik",               // T_FAULT_4
    "Hardware-Stopp durch Uzk-Überspannung",        // T_FAULT_5
    "Uzk+ über Grenzwert",                          // T_FAULT_6
    "Uzk- über Grenzwert",                          // T_FAULT_7
    "Überstrom Drossel Phase L1",                   // T_FAULT_8
    "Überstrom Drossel Phase L2",                   // T_FAULT_9
    "Überstrom Drossel Phase L3",                   // T_FAULT_10
    "Pufferkondensator-Spannung",                   // T_FAULT_11
    "Quarzfehler",                                  // T_FAULT_12
    "Netzunterspannung Phase 1",                    // T_FAULT_13
    "Netzunterspannung Phase 2",                    // T_FAULT_14
    "Netzunterspannung Phase 3",                    // T_FAULT_15
    "Batterieüberstrom",                            // T_FAULT_16
    "Relais-Test fehlgeschlagen",                   // T_FAULT_17
    "Platinen-Übertemperatur",                      // T_FAULT_18
    "Kern-Übertemperatur",                          // T_FAULT_19
    "Übertemperatur Kühlkörper 1",                  // T_FAULT_20
    "Übertemperatur Kühlkörper 2",                  // T_FAULT_21
    "I2C-Fehler mit Power-Board",                   // T_FAULT_22
    "Power-Board-Fehler",                           // T_FAULT_23
    "PWM-Ausgänge defekt",                          // T_FAULT_24
    "Isolation zu gering oder unplausibel",         // T_FAULT_25
    "I-Gleichanteil max (1 A)",                     // T_FAULT_26
    "I-Gleichanteil max langsam (47 mA)",           // T_FAULT_27
    "Möglicher Defekt DSD-Kanal (Offset zu groß)",  // T_FAULT_28
    "RS485-Fehler Relaisbox",                       // T_FAULT_29
    "Überspannung zwischen Phasen",                 // T_FAULT_30
    "IGBT L1 BH defekt",                            // T_FAULT_31
    "IGBT L1 BL defekt",                            // T_FAULT_32
    "IGBT L2 BH defekt",                            // T_FAULT_33
    "IGBT L2 BL defekt",                            // T_FAULT_34
    "IGBT L3 BH defekt",                            // T_FAULT_35
    "IGBT L3 BL defekt",                            // T_FAULT_36
    "Langzeit-Überspannung Phase 1",                // T_FAULT_37
    "Langzeit-Überspannung Phase 2",                // T_FAULT_38
    "Langzeit-Überspannung Phase 3",                // T_FAULT_39
    "Überspannung Phase 1, Stufe 1",                // T_FAULT_40
    "Überspannung Phase 1, Stufe 2",                // T_FAULT_41
    "Überspannung Phase 2, Stufe 1",                // T_FAULT_42
    "Überspannung Phase 2, Stufe 2",                // T_FAULT_43
    "Überspannung Phase 3, Stufe 1",                // T_FAULT_44
    "Überspannung Phase 3, Stufe 2",                // T_FAULT_45
    "Überfrequenz, Stufe 1",                        // T_FAULT_46
    "Überfrequenz, Stufe 2",                        // T_FAULT_47
    "Unterspannung Phase 1, Stufe 1",               // T_FAULT_48
    "Unterspannung Phase 1, Stufe 2",               // T_FAULT_49
    "Unterspannung Phase 2, Stufe 1",               // T_FAULT_50
    "Unterspannung Phase 2, Stufe 2",               // T_FAULT_51
    "Unterspannung Phase 3, Stufe 1",               // T_FAULT_52
    "Unterspannung Phase 3, Stufe 2",               // T_FAULT_53
    "Unterfrequenz, Stufe 1",                       // T_FAULT_54
    "Unterfrequenz, Stufe 2",                       // T_FAULT_55
    "CPU-Ausnahme NMI",                             // T_FAULT_56
    "CPU-Ausnahme HardFault",                       // T_FAULT_57
    "CPU-Ausnahme MemManage",                       // T_FAULT_58
    "CPU-Ausnahme BusFault",                        // T_FAULT_59
    "CPU-Ausnahme UsageFault",                      // T_FAULT_60
    "RTC Power-on-Reset",                           // T_FAULT_61
    "RTC-Oszillator gestoppt",                      // T_FAULT_62
    "RTC-Versorgungsspannung eingebrochen",         // T_FAULT_63
    "RCD-Sprung DC + AC > 30 mA",                   // T_FAULT_64
    "RCD-Sprung DC > 60 mA",                        // T_FAULT_65
    "RCD-Sprung AC > 150 mA",                       // T_FAULT_66
    "RCD-Strom > 300 mA",                           // T_FAULT_67
    "+5 V fehlerhaft",                              // T_FAULT_68
    "-9 V fehlerhaft",                              // T_FAULT_69
    "+9 V fehlerhaft",                              // T_FAULT_70
    "+3,3 V fehlerhaft",                            // T_FAULT_71
    "RDC-Kalibrierung fehlgeschlagen",              // T_FAULT_72
    "I2C-Fehler",                                   // T_FAULT_73
    "AFI-Frequenzgenerator-Fehler",                 // T_FAULT_74
    "Kühlkörpertemperatur zu hoch",                 // T_FAULT_75
    "Uzk über Grenzwert",                           // T_FAULT_76
    "Usg A über Grenzwert",                         // T_FAULT_77
    "Usg B über Grenzwert",                         // T_FAULT_78
    "Einschaltbedingung Umin Phase 1",              // T_FAULT_79
    "Einschaltbedingung Umax Phase 1",              // T_FAULT_80
    "Einschaltbedingung Fmin Phase 1",              // T_FAULT_81
    "Einschaltbedingung Fmax Phase 1",              // T_FAULT_82
    "Einschaltbedingung Umin Phase 2",              // T_FAULT_83
    "Einschaltbedingung Umax Phase 2",              // T_FAULT_84
    "Batteriestromsensor defekt",                   // T_FAULT_85
    "Batterie-Booster defekt",                      // T_FAULT_86
    "Einschaltbedingung Umin Phase 3",              // T_FAULT_87
    "Einschaltbedingung Umax Phase 3",              // T_FAULT_88
    "Spannungssprung/Offset an AC-Klemmen zu groß"
    "(Phasenausfall)", // T_FAULT_89
    "Wechselrichter vom Hausnetz getrennt",                  // T_FAULT_90
    "+9-V-Differenz DSP/PIC zu groß",                        // T_FAULT_91
    "1,5-V-Fehler",                                          // T_FAULT_92
    "2,5-V-Fehler",                                          // T_FAULT_93
    "1,5-V-Messdifferenz",                                   // T_FAULT_94
    "2,5-V-Messdifferenz",                                   // T_FAULT_95
    "Batteriespannung außerhalb des erwarteten Bereichs",    // T_FAULT_96
    "PIC-Software nicht startbar",                           // T_FAULT_97
    "PIC-Bootloader unerwartet erkannt",                     // T_FAULT_98
    "Phasenlagefehler (nicht 120°)",                         // T_FAULT_99
    "Batterieüberspannung",                                  // T_FAULT_100
    "Drosselstrom instabil",                                 // T_FAULT_101
    "Netzspannungsdifferenz intern/extern zu groß Phase 1",  // T_FAULT_102
    "Netzspannungsdifferenz intern/extern zu groß Phase 2",  // T_FAULT_103
    "Netzspannungsdifferenz intern/extern zu groß Phase 3",  // T_FAULT_104
    "Externer Not-Aus aktiv",                                // T_FAULT_105
    "Batterie leer: keine Energie für Standby",              // T_FAULT_106
    "CAN-Timeout mit Batterie",                              // T_FAULT_107
    "Timing-Problem",                                        // T_FAULT_108
    "Übertemperatur Kühlkörper Batterie-IGBT",               // T_FAULT_109
    "Batterie-Kühlkörpertemperatur zu hoch",                 // T_FAULT_110
    "Interner Relaisbox-Fehler",                             // T_FAULT_111
    "Relaisbox PE-Aus-Fehler",                               // T_FAULT_112
    "Relaisbox PE-Ein-Fehler",                               // T_FAULT_113
    "Interner Batteriefehler",                               // T_FAULT_114
    "Parameter geändert",                                    // T_FAULT_115
    "3 Inselbildungsversuche fehlgeschlagen",                // T_FAULT_116
    "Unterspannung zwischen Phasen",                         // T_FAULT_117
    "System-Reset erkannt",                                  // T_FAULT_118
    "Update erkannt",                                        // T_FAULT_119
    "FRT-Überspannung",                                      // T_FAULT_120
    "FRT-Unterspannung",                                     // T_FAULT_121
    "IGBT-L1-Freilaufdiode defekt",                          // T_FAULT_122
    "IGBT-L2-Freilaufdiode defekt",                          // T_FAULT_123
    "IGBT-L3-Freilaufdiode defekt",                          // T_FAULT_124
    "Einphasenmodus aktiv, für Geräteklasse nicht erlaubt",  // T_FAULT_125
    "Inselbetrieb erkannt",                                  // T_FAULT_126
    "Neutralleiterfehler",                                   // T_FAULT_127

    // --- Anzeige: der Hintergrund -----------------------------
    "Helles Theme",      // T_D_BTN_THEME_LIGHT
    "Dunkles Theme",     // T_D_BTN_THEME_DARK
};

#endif // !RCT_LANG_EN
