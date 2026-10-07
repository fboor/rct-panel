// The user-visible text of the panel, in one place per language.
//
// One language per firmware, chosen at build time: -DRCT_LANG_EN builds the
// English panel, without the flag the German one - which is what the standard
// build does, so the shipped firmware stays the way it has always been. Nothing
// here is switched at runtime: the display pages are built once in guiStartApp(),
// and a web page would need a second table in flash for a build that is a
// kilobyte of text. One device, one language.
//
// How a text is used:
//
//   const char *s = tr(T_ROW_UPTIME);          // a whole string
//   b += F("<td class=\"k\">"); b += s;        // into a web page
//   lv_label_set_text(label, tr(T_TITLE));     // onto the display
//
// Two encodings live in the tables, and which one a text needs depends on where
// it goes:
//
//   GUI texts are plain UTF-8, because the Montserrat font has the umlauts and
//   the display shows them directly ("Überschuss").
//   Web texts carry HTML entities ("&Uuml;berschuss"), because they end up
//   inside a page that is sent byte by byte from flash. That is how the pages
//   have always been written, and entities render identically.
//
// The tables are declared with their size, so a text that is missing from one
// language is a compile error and not a page with a hole in it. tools/i18n_test
// checks the other half of that bargain: that the two tables still have the
// same placeholders in the same strings.
//
// Serial log lines are not in here. The log is developer text, it is read in a
// terminal, and its wording is what the search in a bug report runs on - it
// stays as it is, in every build.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_I18N_LANG_H
#define RCT_I18N_LANG_H

// "de" or "en": the language of this build, for the version line and the
// <html lang> attribute.
#if defined(RCT_LANG_EN)
#define RCT_LANG_NAME "en"
#else
#define RCT_LANG_NAME "de"
#endif

// One ID per text. The names are what the code says the text is for, not the
// text itself, so a table stays readable. A new text gets an ID here first:
// leaving it out of a table then fails the build instead of the display.
enum LangId : int {
  // --- web: navigation and page titles -------------------------------------
  T_NAV_HOME = 0,
  T_NAV_VERLAUF,
  T_NAV_DATA,
  T_NAV_SHOTS,
  T_NAV_UPDATE,
  T_NAV_SETTINGS,
  // "RCT Power Panel" is the product name and is the same in both languages;
  // it is still an ID, so a build that wants another name has one place to
  // change it in.
  T_PAGE_ROOT,
  T_PAGE_VERLAUF,
  T_PAGE_DATA,
  T_PAGE_SHOTS,
  T_PAGE_UPDATE,
  T_PAGE_SETTINGS,
  T_MSG_TO_HOME,
  // The complete <html lang> attribute, including the attribute itself.
  T_HTML_LANG,

  // --- web: the heading over the four value cards -------------------------
  T_H_CURRENT,

  // --- web: the four value cards on the overview ---------------------------
  T_CARD_GRID,
  T_CARD_PV,
  T_CARD_BATTERY,
  T_CARD_LOAD,

  // --- web: the overview tables --------------------------------------------
  T_H_DEVICE,
  T_ROW_INVERTER,
  T_ROW_INVERTER_OK,
  T_ROW_INVERTER_NO,
  T_ROW_INVERTER_WAIT, // link up, had data, but nothing new for a while
  T_ROW_INVERTER_ASLEEP, // device off on purpose; hours of silence are normal
  T_ROW_INVERTER_NAME, // what the device calls itself
  T_ROW_CONTROLLER,
  T_ROW_FW_PANEL,
  T_ROW_UPTIME,
  T_ROW_FREEMEM,
  T_ROW_SDCARD,
  T_ROW_SDCLK,
  T_ROW_SDCLK_NONE,
  T_ROW_ADDR,
  T_ROW_ADDR_NAME,

  // --- web: the switched output -------------------------------------------
  T_H_OUTPUT,
  T_NOTE_OUTPUT,   // what the output follows, in one sentence
  T_ROW_FUNCTION,
  T_ROW_STATE,
  T_STATE_TEST,
  T_STATE_ON,
  T_STATE_OFF,
  T_STATE_ON_NOW,   // "%s &middot; %d W jetzt"
  T_TITLE_THRESHOLD,
  T_BTN_APPLY,
  T_BTN_TEST,
  // The five functions, as the long text of the select and as the short text
  // the display shows. Both come from here, so the two cannot drift apart.
  T_RELAY_LONG_OFF,
  T_RELAY_LONG_GRID,
  T_RELAY_LONG_SURPLUS,
  T_RELAY_LONG_FAULT,
  T_RELAY_LONG_ISLAND,
  T_RELAY_SHORT_OFF,
  T_RELAY_SHORT_GRID,
  T_RELAY_SHORT_SURPLUS,
  T_RELAY_SHORT_FAULT,
  T_RELAY_SHORT_ISLAND,

  // --- web: maintenance ----------------------------------------------------
  T_H_MAINT,
  T_NOTE_MAINT,    // what needs the code
  T_BTN_RESTART,
  T_BTN_SETUP,

  // --- web: the two listing pages ------------------------------------------
  T_LINK_LOAD,
  T_LINK_VIEW,
  T_BTN_RELOAD,
  T_BTN_SHOT,
  T_H_CAPTURE,
  T_NOTE_EMPTY,     // with the directory in <code>
  T_NOTE_TAIL,      // with ?tail=0 in <code>
  T_NOTE_TRUNC,     // with the route in <code>
  T_NOTE_SHOT_RUN,
  T_NOTE_SHOT_IDLE,

  // --- web: the charts -----------------------------------------------------
  T_ERR_LOAD_FAILED,   // a JSON answer did not arrive
  T_NOTE_NO_DATA,      // the 24 h ring is still empty, or the period has none
  T_NOTE_UPDATED,      // %s = time of the newest sample, written by the browser
  T_NOTE_REFRESHED,    // how often the live chart asks again
  T_NOTE_LIVE,         // the name of the 24 h view
  T_NOTE_LOADING,      // while a month file is on its way
  T_NOTE_NO_FILE,      // no file on the card for this period at all
  T_NOTE_OLD_SUMS,     // a file written before the sums were in the format
  T_RANGE_LIVE,        // 24 h | Tag | Woche | Monat
  T_RANGE_DAY,
  T_RANGE_WEEK,
  T_RANGE_MONTH,
  T_BTN_PREV,
  T_BTN_NEXT,

  // --- web: /update --------------------------------------------------------
  T_H_FIRMWARE,
  T_BTN_FW_WRITE,
  T_NOTE_FIRMWARE,

  // --- web: the settings page (see handleSettingsPage) -----------------
  T_H_SET_DEVICE,
  T_H_SET_THEME,
  T_LBL_DEVICE_TYPE,
  T_LBL_DEVICE_HOST,
  T_LBL_DEVICE_PORT,
  T_LBL_THEME,
  T_OPT_TYPE_RCT,
  T_OPT_TYPE_OIG,
  // Only ever shown by a simulator build (see the settings page), so both languages
  // carry it and neither panel shows it.
  T_OPT_TYPE_SIM,
  T_BTN_SAVE,
  T_NOTE_RESTART,
  T_OK_SAVED,
  // The other half of the same save: only the theme changed, so nothing restarts.
  // A second string rather than a generic one, because a confirmation announcing a
  // restart that does not happen is worse than no confirmation.
  T_OK_SAVED_NO_RESTART,
  T_ERR_BAD_TYPE,
  T_ERR_BAD_HOST,
  T_ERR_BAD_PORT,

  T_OK_FW_WRITTEN,
  T_ERR_FW_NOTRUN,
  T_ERR_FW_END,

  // --- web: the answers to an action ---------------------------------------
  T_OK_RESTART,
  T_OK_SETUP,
  T_OK_APPLIED,
  T_OK_TEST_RUN,
  T_ERR_TEST_RUNNING,
  T_ERR_CODE,
  T_ERR_SHOT_RUNNING,
  T_ERR_FUNCTION,
  T_ERR_ACTION,
  T_ERR_PAGE,

  // --- web: the answers to a download --------------------------------------
  T_ERR_DOWNLOAD_RUNNING,
  T_ERR_FW_WRITING,
  T_ERR_NO_SDCARD,
  T_ERR_NO_FILE,
  T_ERR_NO_FILE_DIR,
  T_ERR_READ_FAILED,
  T_ERR_SD_UNREADABLE,

  // --- the SD status line, shown on the display and in the web overview ----
  T_SD_QUEUED_NCARD,
  T_SD_LOST,
  T_SD_ROW,
  T_SD_ROWS,
  T_SD_QUEUED,
  T_SD_FREE,
  T_SD_OK,

  // --- Anzeige: die Seitenüberschrift --------------------------------------
  T_D_HEAD_OVERVIEW,
  T_D_HEAD_ENERGY,
  T_D_HEAD_HEUTE,
  T_D_HEAD_GRAPH,
  T_D_HEAD_INFO,
  T_D_HEAD_BATTERY,
  T_D_HEAD_SERVICE,

  // --- Anzeige: Übersicht, die vier Zeilen unter dem Flussdiagramm ---------
  T_D_ROW_PRODUCTION,
  T_D_ROW_CONSUMPTION,
  T_D_ROW_BATTERY,

  // --- Anzeige: was diese Zeilen gerade sagen (Portal-Tendenzwörter) -------
  T_D_TEND_MAINS,   // the house draws from the grid
  T_D_TEND_SELF,    // the house runs on its own
  T_D_TEND_INACTIVE, // PV below 20 W: the panels are not producing
  T_D_TEND_NOLOAD,  // below 10 W: no household draw to name
  T_D_TEND_DISCHARGE,
  T_D_TEND_CHARGE,
  T_D_TEND_STANDBY,
  T_D_TEND_NOBAT,

  // --- Anzeige: Energie, die fünf Zeilen und die vier Zeiträume ------------
  T_D_EN_PV,
  T_D_EN_SELFUSE,
  T_D_EN_EXPORT,
  T_D_EN_IMPORT,
  T_D_EN_LOAD,
  T_D_PER_DAY,
  T_D_PER_MONTH,
  T_D_PER_YEAR,
  T_D_PER_TOTAL,

  // --- Anzeige: Heute, die sieben Karten -----------------------------------
  T_D_CARD_PRODUCED,
  T_D_CARD_SELFUSE,
  T_D_CARD_FEDIN,
  T_D_CARD_CONSUMED,
  T_D_CARD_IMPORTED,
  T_D_CARD_SELF,
  T_D_CARD_SELFRATE,

  // --- Anzeige: 24 h Verlauf -----------------------------------------------
  T_D_SER_GRID,
  T_D_SER_CONSUMPTION,
  T_D_SER_PV,
  T_D_SER_EXT,
  T_D_SER_BATTERY,
  T_D_SER_SOC,
  T_D_GAP_ONE,
  T_D_GAP_MANY,

  // --- Anzeige: Info -------------------------------------------------------
  T_D_IF_NAME,
  T_D_IF_SOFTWARE,
  T_D_IF_HOST,
  T_D_IF_PORT,
  T_D_IF_LINK,
  T_D_IF_LASTDATA,
  T_D_IF_UPTIME,
  T_D_IF_L1,
  T_D_IF_L2,
  T_D_IF_L3,
  T_D_IF_PV,
  T_D_IF_CORE,
  T_D_IF_HEATSINK,
  T_D_IF_FREQ,
  T_D_LINK_UP,
  T_D_LINK_DOWN,
  T_D_LASTDATA_AGO,

  // --- Anzeige: Akku -------------------------------------------------------
  T_D_BA_SOC,
  T_D_BA_POWER,
  T_D_BA_TEMP,
  T_D_BA_CALIB,
  T_D_BA_CYCLES,
  T_D_BA_SOH,
  T_D_BA_ISLAND,
  T_D_YES,
  T_D_NO,
  T_D_CALIB_OVERDUE,
  T_D_CALIB_TODAY,
  T_D_CALIB_DAYS,

  // --- Anzeige: der ausgelesene Batteriezustand auf der Service-Seite ------
  T_D_ST_READY,
  T_D_ST_CALCHARGE,
  T_D_ST_CALDISCHARGE,
  T_D_ST_BALANCING,
  T_D_ST_UNDERVOLT,
  T_D_ST_OFF,
  T_D_ST_RAW,
  T_D_ST_BALPLUS,
  T_D_ST_DISCHARGING,
  T_D_ST_CHARGING,

  // --- Anzeige: Service, die Abschnitte ------------------------------------
  T_D_SV_HEAD_BAT,
  T_D_SV_HEAD_SD,
  T_D_SV_HEAD_FAULTS,
  T_D_SV_HEAD_WEB,
  T_D_SV_HEAD_OUTPUT,
  T_D_NO_FAULTS,
  T_D_MORE_FAULTS,
  T_D_FAULT_LINE,
  T_D_BTN_SETUP,
  T_D_BTN_SHOT,
  T_D_BTN_TEST,
  T_D_CODE_EMPTY,
  T_D_CODE,
  T_D_CODE_HINT,
  T_D_IP,
  T_D_NO_NET,

  // --- Anzeige: der geschaltete Ausgang ------------------------------------
  T_D_OUT_THRESHOLD,
  T_D_OUT_TEST,
  T_D_OUT_MODE,
  T_D_OUT_NOW,
  T_D_OUT_NOW_STALE, // same, but the value is older than a minute
  T_D_OUT_ON,
  T_D_OUT_OFF,
  T_D_OUT_NOTHING,

  // --- Anzeige: die Screenshot-Ausgabe -------------------------------------
  T_D_SHOT_BUSY,
  T_D_SHOT_NOCARD,
  T_D_SHOT_COUNT,
  T_D_SHOT_WRITING,
  T_D_SHOT_SAVED,
  T_D_SHOT_FAILED,
  T_D_SHOT_NOMEM,

  // --- Anzeige: die WLAN-Einrichtung als Vollbild --------------------------
  T_D_AP_TITLE,
  T_D_AP_HINT,

  // --- Anzeige: das Statusabzeichen in der Kopfzeile -----------------------
  T_D_BADGE_CONNECTING,
  T_D_BADGE_NODATA,
  T_D_BADGE_LIVE,
  T_D_BADGE_RECONNECT,
  T_D_BADGE_WAITING,
  T_D_BADGE_ASLEEP, // the device is off on purpose; hours of silence are normal

  // --- Anzeige: die 128 Fehlertexte (Bit n aus fault[0..3].flt)
  // Der Index ist das Bit, nach dem der Wechselrichter meldet:
  // serviceFaultText() holt T_FAULT_0 + n, die Reihenfolge hier ist
  // also die Reihenfolge der Bits.
  T_FAULT_0,
  T_FAULT_1,
  T_FAULT_2,
  T_FAULT_3,
  T_FAULT_4,
  T_FAULT_5,
  T_FAULT_6,
  T_FAULT_7,
  T_FAULT_8,
  T_FAULT_9,
  T_FAULT_10,
  T_FAULT_11,
  T_FAULT_12,
  T_FAULT_13,
  T_FAULT_14,
  T_FAULT_15,
  T_FAULT_16,
  T_FAULT_17,
  T_FAULT_18,
  T_FAULT_19,
  T_FAULT_20,
  T_FAULT_21,
  T_FAULT_22,
  T_FAULT_23,
  T_FAULT_24,
  T_FAULT_25,
  T_FAULT_26,
  T_FAULT_27,
  T_FAULT_28,
  T_FAULT_29,
  T_FAULT_30,
  T_FAULT_31,
  T_FAULT_32,
  T_FAULT_33,
  T_FAULT_34,
  T_FAULT_35,
  T_FAULT_36,
  T_FAULT_37,
  T_FAULT_38,
  T_FAULT_39,
  T_FAULT_40,
  T_FAULT_41,
  T_FAULT_42,
  T_FAULT_43,
  T_FAULT_44,
  T_FAULT_45,
  T_FAULT_46,
  T_FAULT_47,
  T_FAULT_48,
  T_FAULT_49,
  T_FAULT_50,
  T_FAULT_51,
  T_FAULT_52,
  T_FAULT_53,
  T_FAULT_54,
  T_FAULT_55,
  T_FAULT_56,
  T_FAULT_57,
  T_FAULT_58,
  T_FAULT_59,
  T_FAULT_60,
  T_FAULT_61,
  T_FAULT_62,
  T_FAULT_63,
  T_FAULT_64,
  T_FAULT_65,
  T_FAULT_66,
  T_FAULT_67,
  T_FAULT_68,
  T_FAULT_69,
  T_FAULT_70,
  T_FAULT_71,
  T_FAULT_72,
  T_FAULT_73,
  T_FAULT_74,
  T_FAULT_75,
  T_FAULT_76,
  T_FAULT_77,
  T_FAULT_78,
  T_FAULT_79,
  T_FAULT_80,
  T_FAULT_81,
  T_FAULT_82,
  T_FAULT_83,
  T_FAULT_84,
  T_FAULT_85,
  T_FAULT_86,
  T_FAULT_87,
  T_FAULT_88,
  T_FAULT_89,
  T_FAULT_90,
  T_FAULT_91,
  T_FAULT_92,
  T_FAULT_93,
  T_FAULT_94,
  T_FAULT_95,
  T_FAULT_96,
  T_FAULT_97,
  T_FAULT_98,
  T_FAULT_99,
  T_FAULT_100,
  T_FAULT_101,
  T_FAULT_102,
  T_FAULT_103,
  T_FAULT_104,
  T_FAULT_105,
  T_FAULT_106,
  T_FAULT_107,
  T_FAULT_108,
  T_FAULT_109,
  T_FAULT_110,
  T_FAULT_111,
  T_FAULT_112,
  T_FAULT_113,
  T_FAULT_114,
  T_FAULT_115,
  T_FAULT_116,
  T_FAULT_117,
  T_FAULT_118,
  T_FAULT_119,
  T_FAULT_120,
  T_FAULT_121,
  T_FAULT_122,
  T_FAULT_123,
  T_FAULT_124,
  T_FAULT_125,
  T_FAULT_126,
  T_FAULT_127,

  // --- Anzeige: der Wechsel zwischen dunklem und hellem Hintergrund --------
  // Beide sagen, was das Antippen bewirkt, nicht was gerade da ist - so wie
  // die Zeile mit dem Code darueber.
  T_D_BTN_THEME_LIGHT,
  T_D_BTN_THEME_DARK,

  T_COUNT
};

// Ab hier in der Reihenfolge des enum kommen die Anzeigetexte. Zwei Dinge
// unterscheiden sie von den Web-Texten: sie enthalten einen Zeilenumbruch, wo
// es einer noetig ist (die Fehlerliste bricht selbst um), und sie sind in
// beiden Sprachen UTF-8, weil die Schrift auf dem Panel die Umlaute hat und
// zeigt. tools/i18n_test benutzt diese Grenze, um beides zu pruefen.
static constexpr int T_DISPLAY_FROM = (int)T_D_HEAD_OVERVIEW;

// Defined once: LangDe.cpp without the flag, LangEn.cpp with it.
extern const char *const kLang[T_COUNT];

// The text for one ID. The range check is a safety net, not a translation
// mechanism: an ID that is out of range is a mistake in the code, and showing
// an empty label beats showing a random other text.
inline const char *tr(LangId id) {
  const int i = (int)id;
  return (i >= 0 && i < (int)T_COUNT) ? kLang[i] : "";
}

// How this language writes a decimal fraction: German has a comma, English a
// point. Used by fmtNumLang() in NumFmt.h, which is why it is here and not in
// the tables - it is not text, it is punctuation.
inline char langDecPoint() {
#if defined(RCT_LANG_EN)
  return '.';
#else
  return ',';
#endif
}

// How this language writes a date, as a strftime format. German puts the day
// first (24.03.2026), English the ISO order (2026-03-24) - one format for both
// would put the month where the year stands in the English one, so the format
// follows the language.
inline const char *langDateFmt() {
#if defined(RCT_LANG_EN)
  return "%Y-%m-%d";
#else
  return "%d.%m.%Y";
#endif
}

#endif // RCT_I18N_LANG_H
