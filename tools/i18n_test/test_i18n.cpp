// Host test for the language tables (src/i18n/).
//
// The tables are two hand-written lists of the same length, and the compiler
// already refuses a list that is too short. What it cannot see is the rest:
//   * a text that is there but empty - a label with nothing in it, and nothing
//     in the serial log to notice it by,
//   * a line break in a web text, which would take a web page apart (a display
//     text may have one - the fault list breaks itself, see T_DISPLAY_FROM),
//   * a German character in the English table, which is what a text that was
//     copied instead of translated looks like,
//   * an ID that is not a valid one (tr() has to answer with an empty string,
//     not with whatever happens to be behind the table),
//   * a fault line without its line break, which would run the fault texts into
//     each other on the panel.
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
    // A line break belongs in a display text (the fault list, the Wi-Fi hint)
    // and nowhere else: a web page would be taken apart by one.
    if (i < T_DISPLAY_FROM &&
        (std::strchr(s, '\n') != nullptr || std::strchr(s, '\r') != nullptr)) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "ID %d enthaelt einen Zeilenumbruch", i);
      fail("Zeilenumbruch", buf);
    } else {
      ok();
    }
#if defined(RCT_LANG_EN)
    // The English table carries no German text. Two characters are not German
    // and are in it anyway: the degree sign (a phase angle) and the middle dot
    // between the two halves of a relay line. Everything else above U+007F is a
    // character that did not get translated.
    for (const char *q = s; *q != '\0';) {
      unsigned cp = 0;
      if ((unsigned char)*q < 0x80) {
        q++;
        continue;
      }
      if ((unsigned char)*q == 0xC2 && (unsigned char)q[1] != '\0') {
        cp = (unsigned char)q[1];
        q += 2;
      } else if ((unsigned char)*q == 0xC3 && (unsigned char)q[1] != '\0') {
        cp = 0xC0u + ((unsigned char)q[1] & 0x3Fu);
        q += 2;
      } else {
        cp = 0xFFFD;
        q++;
      }
      if (cp != 0xB0 && cp != 0xB7) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "ID %d enthaelt U+%04X", i, cp);
        fail("deutsches Zeichen", buf);
        break;
      }
    }
#endif
  }

  // Die Fehlerliste braucht ihren Zeilenumbruch am Ende: ohne ihn laeuft der
  // naechste Text an den vorherigen ("F1 textF2 text").
  {
    const char *line = tr(T_D_FAULT_LINE);
    const std::size_t n = std::strlen(line);
    if (n == 0 || line[n - 1] != '\n') {
      fail("Fehlerliste", "T_D_FAULT_LINE ohne Umbruch am Ende");
    } else {
      ok();
    }
  }

  // Die 128 Fehlertexte werden als T_FAULT_0 + bit angesprochen. Das geht nur,
  // solange sie ohne Luecke aufeinander folgen - ein eingeschobener ID
  // verschiebt sonst jedes Bit auf einen anderen Text.
  {
    if ((int)T_FAULT_127 - (int)T_FAULT_0 != 127) {
      fail("Fehlertexte", "Luecke zwischen T_FAULT_0 und T_FAULT_127");
    } else {
      ok();
    }
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

  // The date format is punctuation, not text: German starts with the day,
  // English with the year (see langDateFmt). Both have to be there, and the
  // English one has to be the unambiguous order.
  {
    const char *fmt = langDateFmt();
    if (fmt == nullptr || fmt[0] == '\0') {
      fail("Datumsformat", "leer");
    } else if (std::strchr(fmt, '%') == nullptr) {
      fail("Datumsformat", fmt);
    } else {
      ok();
    }
  }

  std::printf("  laengster Text: ID %d, %d Zeichen\n", longestId, longest);
  std::printf("%s (%d Pruefungen)\n", g_failed == 0 ? "alle ok" : "FEHLER",
              g_checks);
  return g_failed == 0 ? 0 : 1;
}
