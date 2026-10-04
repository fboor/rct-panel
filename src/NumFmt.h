// Number formatting for the display, in one place.
//
// One rule lives here, and it is the only reason this file exists: a value that
// rounds to zero is shown as zero. "%.1f" of -0.04 W prints "-0.0", which reads
// as "a little bit of consumption" where there is none - and the panel is full
// of values that hover around zero and change sign: grid power around a sunny
// midday, PV surplus, the daily yield at 00:00, a battery at 100 %. So the case
// is not exotic, it is the normal case on a good day.
//
// The sign is dropped from the *formatted text*, not from the number, which is
// what makes this work for any format string without knowing its unit:
// "-0.0 kW" -> "0.0 kW", "-0,00" -> "0,00", "-0" -> "0", while "-0.4 kW" and
// "-12.3" keep their sign. Snapping the number itself would need to know the
// last printed digit, and a caller that rounds a value before formatting it
// would silently lose small but real numbers.
//
// What must *not* be touched: a sign that does not start a number. "2026-09-30"
// keeps its dashes, and so does any text row - the callers are free to pass
// fault names and dates through the same function.
//
// The comma variant is for the pages and labels that use one (the Energie page,
// the setup portal, the web interface); snprintf always writes a point, so it is
// swapped afterwards. Never use it for text rows - a date like "29.09.2026"
// would come out as "29,09,2026".
//
// fmtNumLang is the same thing without naming the language: it follows the build
// (see src/i18n/Lang.h), which is what the web interface and the SD status line
// use, because both are shown to the user and both follow the language.
//
// No Arduino, no ESP, no card: this is the shipped header that the host test
// compiles (tools/numfmt_test), so the rule is checked without a panel.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_NUMFMT_H
#define RCT_NUMFMT_H

#include "i18n/Lang.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Drop a minus sign in front of a number that is all zeros. In place; returns
// the new length.
//
// The number is found by walking the text, because the display never prints a
// number on its own: it prints "%.1f kW", "Netz %.2f kW" or "%+.3f kW  %.1f A",
// and the unit and the second value follow the number that has to lose its
// sign. Every "-" that starts a token is looked at, so several numbers in one
// string are all covered.
static inline size_t numDropNegZero(char *s) {
  if (s == nullptr || s[0] == '\0') {
    return 0;
  }
  size_t i = 0;
  while (s[i] != '\0') {
    if (s[i] != '-') {
      i++;
      continue;
    }
    // A sign that follows a digit, a separator or another sign belongs to a
    // number or a date, not to a standalone value: "2026-09-30" stays.
    if (i > 0) {
      const char p = s[i - 1];
      if ((p >= '0' && p <= '9') || p == '.' || p == ',' || p == '-') {
        i++;
        continue;
      }
    }
    // The token behind the sign: digits with at most one decimal separator. All
    // of them zero is the only case where the sign says nothing - "-0.4" is a
    // real value, and so is "-0" from a value that rounds to zero (there the
    // sign is exactly what has to go).
    size_t j = i + 1;
    size_t digits = 0;
    size_t zeros = 0;
    bool seenSep = false;
    for (;;) {
      const char c = s[j];
      if (c >= '0' && c <= '9') {
        digits++;
        if (c == '0') {
          zeros++;
        }
        j++;
        continue;
      }
      if (!seenSep && (c == '.' || c == ',')) {
        seenSep = true;
        j++;
        continue;
      }
      break;
    }
    if (digits > 0 && zeros == digits) {
      memmove(s + i, s + i + 1, strlen(s + i + 1) + 1);
      continue; // same index, the next character moved into place
    }
    i++;
  }
  return strlen(s);
}

// snprintf, then the sign rule. Returns what snprintf returned, so a caller that
// checks for truncation still can.
static inline int fmtNum(char *out, size_t cap, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(out, cap, fmt, ap);
  va_end(ap);
  if (out != nullptr && cap > 0) {
    numDropNegZero(out);
  }
  return n;
}

// The part both comma variants share: format, write the decimal separator the
// caller wants, then the sign rule. A va_list in and not "..." on, so the three
// public functions below are one line each instead of three copies of the same
// loop.
static inline int fmtNumSepV(char *out, size_t cap, char sep, const char *fmt,
                             va_list ap) {
  const int n = vsnprintf(out, cap, fmt, ap);
  if (out != nullptr && cap > 0) {
    if (sep != '.') {
      for (char *q = out; *q; q++) {
        if (*q == '.') {
          *q = sep;
        }
      }
    }
    numDropNegZero(out);
  }
  return n;
}

// The same, with the decimal point written as a comma.
static inline int fmtNumComma(char *out, size_t cap, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const int n = fmtNumSepV(out, cap, ',', fmt, ap);
  va_end(ap);
  return n;
}

// The same, with the separator of the language this firmware was built for.
static inline int fmtNumLang(char *out, size_t cap, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const int n = fmtNumSepV(out, cap, langDecPoint(), fmt, ap);
  va_end(ap);
  return n;
}

// A power on the display: whole watts below 1 kW, kilowatts with two decimals
// from there on. "0,38 kW" is a number nobody compares at a glance - 380 W is,
// and it is about as wide as the values next to it, which is what a row of
// figures needs to stay readable. Above 1 kW the unit change earns its keep: two
// decimals of "5,75 kW" are 5 W, while "380 W" would throw away more than the
// rounding.
//
// The boundary is 1000 W and not 999: at 999.5 W the row would show "1000 W" and
// the next tick "1,00 kW", which is two different renderings of the same
// number. Above the boundary the sign is kept, because a negative power is
// information (the convention says + = draw from the grid), below it the sign is
// dropped by numDropNegZero() the way every other number on the panel drops it.
//
// The separator follows the language, which the overview's values did not: they
// went through setText() and came out with a dot in a German build, while the
// energy page next to it writes a comma.
static inline int fmtPower(char *out, size_t cap, float watts) {
  // The unit is decided from the *rounded* magnitude, not from the raw one: at
  // 999.5 W a comparison against 1000 would still choose watts, print "1000 W",
  // and the next tick print "1,00 kW" - two renderings of one number within a
  // second. Rounding first makes the switch happen exactly where the text does.
  const float w = watts < 0.0f ? -watts : watts;
  const long gerundet = (long)(w + 0.5f);
  if (gerundet < 1000L) {
    return fmtNumLang(out, cap, "%.0f W", (double)watts);
  }
  return fmtNumLang(out, cap, "%.2f kW", (double)(watts / 1000.0f));
}

#endif // RCT_NUMFMT_H
