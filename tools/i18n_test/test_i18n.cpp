// Host test for the language tables (src/i18n/).
//
// The tables are two hand-written lists of the same length, and the compiler
// already refuses a list that is too short. What it cannot see is the rest:
//   * a text that is there but empty - a label with nothing in it, and nothing
//     in the serial log to notice it by,
//   * a text that carries a line break, which would take a web page apart,
//   * a German character in the English table, which is what a text that was
//     copied instead of translated looks like,
//   * an ID that is not a valid one (tr() has to answer with an empty string,
//     not with whatever happens to be behind the table).
//
// The companion check.py reads the same files and compares them against each
// other - same IDs, same order, same placeholders.
//
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>

#include "i18n/Lang.h"

static int g_failed = 0;
static int g_checks = 0;

static void fail(const char *what, const char *detail) {
  g_checks++;
  g_failed++;
  std::printf("  FEHLER %-28s %s\n", what, detail);
}

static void ok() { g_checks++; }

int main() {
  std::printf("== Sprachtabelle %s: %d Texte ==\n", RCT_LANG_NAME,
              (int)T_COUNT);

  int longest = 0;
  int longestId = 0;
  for (int i = 0; i < (int)T_COUNT; i++) {
    const char *s = kLang[i];
    if (s == nullptr || s[0] == '\0') {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "ID %d ist leer", i);
      fail("leerer Text", buf);
      continue;
    }
    ok();
    const int n = (int)std::strlen(s);
    if (n > longest) {
      longest = n;
      longestId = i;
    }
    if (std::strchr(s, '\n') != nullptr || std::strchr(s, '\r') != nullptr) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "ID %d enthaelt einen Zeilenumbruch", i);
      fail("Zeilenumbruch", buf);
    }
#if defined(RCT_LANG_EN)
    // The English table is plain ASCII. A umlaut or a sharp s in here is a
    // German text that was not translated, and it is the one mistake this file
    // cannot be trusted to show on a panel.
    for (const char *q = s; *q != '\0'; q++) {
      if ((unsigned char)*q >= 0x80) {
        char buf[64];
        std::snprintf(buf, sizeof(buf),
                      "ID %d enthaelt Byte 0x%02x", i, (unsigned char)*q);
        fail("kein ASCII", buf);
        break;
      }
    }
#endif
  }

  // An ID outside the table: tr() has to answer with nothing rather than read
  // past the end of the table.
  if (tr((LangId)T_COUNT) == nullptr || tr((LangId)T_COUNT)[0] != '\0') {
    fail("tr() ausserhalb", "T_COUNT liefert Text");
  } else {
    ok();
  }
  if (tr((LangId)-1) == nullptr || tr((LangId)-1)[0] != '\0') {
    fail("tr() ausserhalb", "-1 liefert Text");
  } else {
    ok();
  }

  const char sep = langDecPoint();
  if (sep != ',' && sep != '.') {
    fail("Dezimaltrennzeichen", "weder Komma noch Punkt");
  } else {
    ok();
  }

  std::printf("  laengster Text: ID %d, %d Zeichen\n", longestId, longest);
  std::printf("%s (%d Pruefungen)\n", g_failed == 0 ? "alle ok" : "FEHLER",
              g_checks);
  return g_failed == 0 ? 0 : 1;
}
