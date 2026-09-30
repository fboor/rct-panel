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
  T_NAV_DATA,
  T_NAV_SHOTS,
  T_NAV_UPDATE,
  // "RCT Power Panel" is the product name and is the same in both languages;
  // it is still an ID, so a build that wants another name has one place to
  // change it in.
  T_PAGE_ROOT,
  T_PAGE_DATA,
  T_PAGE_SHOTS,
  T_PAGE_UPDATE,
  T_MSG_TO_HOME,
  // The complete <html lang> attribute, including the attribute itself.
  T_HTML_LANG,

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

  // --- web: /update --------------------------------------------------------
  T_H_FIRMWARE,
  T_BTN_FW_WRITE,
  T_NOTE_FIRMWARE,
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

  T_COUNT
};

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

#endif // RCT_I18N_LANG_H
