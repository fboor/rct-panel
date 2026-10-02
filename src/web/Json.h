// Number formatting for the JSON endpoints of the web interface.
//
// The panel sends numbers, the browser draws them. Nothing here is about the
// drawing - only about how a number has to look so that every JSON parser in
// every browser reads it the same way.
//
// Two rules, and both of them are things that only go wrong on a device:
//
// 1. Fixed-point, never an exponent. A payload of %g would write a small value
//    as 1e-05, which every parser accepts but which nobody wants to read while
//    debugging. The decimals are asked for, so the answer is that short.
//
// 2. No trailing zeros in the fraction. "12.0" is an amount and "12" is the
//    same amount, and the second one is three bytes smaller - with 1700 numbers
//    in one answer that is worth having. The value is unchanged.
//
// A value that is not a number (a NaN from a division that had no denominator,
// a value that ran away) becomes null. That is the only JSON spelling of "no
// value"; "nan" is not valid JSON, and a parser that meets it stops reading the
// rest of the document.
//
// Host-tested: tools/json_test. This header stays free of Arduino, so it can be
// compiled and checked on the build machine.
//
// SPDX-License-Identifier: MIT
#ifndef WEB_JSON_H
#define WEB_JSON_H

#include <stdio.h>
#include <string.h>

#include <stddef.h>

// Writes one number into out and returns its length, or 0 when out is too small.
// Returns false-ish only through a short return; the caller reserves room once
// for the whole answer, so a truncated number cannot happen in practice.
static inline int jsonNum(char *out, size_t cap, double v, int decimals) {
  // The comparison is false for NaN, which is the cheapest test of it there is.
  if (!(v == v) || v > 1e9 || v < -1e9) {
    if (cap < 5) {
      return 0;
    }
    memcpy(out, "null", 5);
    return 4;
  }
  if (decimals < 0) {
    decimals = 0;
  }
  if (decimals > 9) {
    decimals = 9;
  }
  int n = snprintf(out, cap, "%.*f", decimals, v);
  if (n < 0 || (size_t)n >= cap) {
    return 0;
  }
  char *dot = strchr(out, '.');
  if (dot != nullptr) {
    char *end = out + n - 1;
    while (end > dot && *end == '0') {
      *end-- = '\0';
    }
    if (end == dot) {
      *dot = '\0'; // the fraction was all zeros: the point goes too
    }
  }
  // -0.04 with one decimal prints as "-0.0", and cut down it is "-0" - which
  // reads as a negative zero and shows up in a chart as a hair below zero.
  if (out[0] == '-') {
    const char *p = out + 1;
    while (*p == '0' || *p == '.') {
      p++;
    }
    if (*p == '\0') {
      memmove(out, out + 1, strlen(out)); // leaves the terminating NUL in place
    }
  }
  return (int)strlen(out);
}

#endif // WEB_JSON_H
