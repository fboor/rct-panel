// Reading a flat JSON object of numbers, without a JSON library.
//
// The second device family is an HTTP device that answers with a flat JSON
// object, and the panel has to read maybe twenty numbers out of it every ten
// seconds. That does not need a parser: it needs a scanner that finds one
// top-level key and hands back its value, and refuses cleanly when the key is
// not there.
//
// Why not ArduinoJson or anything else: the firmware ships with one dependency
// set, and a dependency that exists for twenty lookups would also have to be
// trusted with the values that end up on the display. This header is 100 lines,
// has no state, allocates nothing and is host-tested (tools/json_scan_test),
// which is what the library would have given us anyway.
//
// What it deliberately does not do: it does not understand nesting. The
// endpoint it reads is a flat object, and a document that contains an array or
// a nested object is not one - so the scanner steps over any value it does not
// recognise rather than mis-reading it. A value that is not a number (a string,
// true, null) is reported as "key not there", because a caller asking for
// EnergyToday wants a number and anything else is a surprise it should not act
// on.
//
// Numbers arrive in the three shapes a JSON producer uses: an integer, a
// fraction, or an exponent. All three are accepted, and the result is a double
// because the fields include powers and temperatures and the panel would lose
// the fraction of a watt if it used a float end to end.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_DEVICE_JSON_H
#define RCT_DEVICE_JSON_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

namespace json {

// Skip whitespace. JSON allows space, tab, CR, LF between everything.
static inline const char *skipSpace(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
    p++;
  }
  return p;
}

// Step over one value of any kind, without interpreting it. Used to reach the
// next key when the key being looked for is not the one at hand: a string with
// braces or commas in it, an array, a nested object - all of them are skipped
// whole, so a value can never be mistaken for the start of the next key.
static inline const char *skipValue(const char *p, const char *end) {
  p = skipSpace(p, end);
  if (p >= end) {
    return p;
  }
  if (*p == '"') {
    p++;
    while (p < end && *p != '"') {
      p += (*p == '\\' && p + 1 < end) ? 2 : 1;
    }
    return p < end ? p + 1 : p;
  }
  if (*p == '{' || *p == '[') {
    // Depth counter, and strings inside it are skipped whole: a brace inside a
    // string must not count.
    const char open = *p;
    const char close = (open == '{') ? '}' : ']';
    int depth = 0;
    while (p < end) {
      if (*p == '"') {
        p++;
        while (p < end && *p != '"') {
          p += (*p == '\\' && p + 1 < end) ? 2 : 1;
        }
        if (p < end) {
          p++;
        }
        continue;
      }
      if (*p == open) {
        depth++;
      } else if (*p == close) {
        depth--;
        if (depth == 0) {
          return p + 1;
        }
      }
      p++;
    }
    return end;
  }
  // A bare token: number, true, false, null. Runs to the next comma or the end
  // of the object.
  while (p < end && *p != ',' && *p != '}') {
    p++;
  }
  return p;
}

// UTF-8 from a \u escape, for a JSON producer that writes one. A producer that
// writes UTF-8 directly (which ArduinoJSON does) never gets here, but a name
// like "Wechselrichter \u00fc" would otherwise reach the display as the six
// characters "u00fc" - a wrong name is worse than a truncated one.
//
// Only the three cases a device name can contain are done: below 0x80 as itself,
// and 0x80..0x7FF and 0x800..0xFFFF as two and three UTF-8 bytes. A lone
// surrogate is written as U+FFFD, because a half a character is not a name.
static inline size_t utf8FromEscape(const char *hex4, char *dst, size_t cap) {
  unsigned code = 0;
  for (int i = 0; i < 4; i++) {
    const char c = hex4[i];
    unsigned v;
    if (c >= '0' && c <= '9') {
      v = (unsigned)(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      v = (unsigned)(c - 'a') + 10u;
    } else if (c >= 'A' && c <= 'F') {
      v = (unsigned)(c - 'A') + 10u;
    } else {
      return 0; // not a \u escape after all
    }
    code = (code << 4) | v;
  }
  if (code >= 0xD800u && code <= 0xDFFFu) {
    code = 0xFFFDu; // lone surrogate
  }
  char tmp[4];
  size_t n = 0;
  if (code < 0x80u) {
    tmp[n++] = (char)code;
  } else if (code < 0x800u) {
    tmp[n++] = (char)(0xC0u | (code >> 6));
    tmp[n++] = (char)(0x80u | (code & 0x3Fu));
  } else {
    tmp[n++] = (char)(0xE0u | (code >> 12));
    tmp[n++] = (char)(0x80u | ((code >> 6) & 0x3Fu));
    tmp[n++] = (char)(0x80u | (code & 0x3Fu));
  }
  if (n > cap) {
    return 0;
  }
  memcpy(dst, tmp, n);
  return n;
}

// Read one string value into dst (always NUL-terminated, truncated rather than
// overflowing). Returns false when the key is not there or is not a string.
static inline bool getString(const char *json, size_t len, const char *key,
                             char *dst, size_t dstCap) {
  const char *end = json + len;
  const char *p = skipSpace(json, end);
  const size_t keyLen = strlen(key);
  if (p < end && *p == '{') {
    p++;
  }
  while (p < end) {
    p = skipSpace(p, end);
    if (p >= end || *p == '}') {
      break;
    }
    if (*p != '"') {
      p = skipValue(p, end);
      continue;
    }
    // The key.
    const char *ks = ++p;
    while (p < end && *p != '"') {
      p += (*p == '\\' && p + 1 < end) ? 2 : 1;
    }
    if (p >= end) {
      break;
    }
    const size_t thisKeyLen = (size_t)(p - ks);
    const bool match = thisKeyLen == keyLen && memcmp(ks, key, keyLen) == 0;
    p++;
    p = skipSpace(p, end);
    if (p < end && *p == ':') {
      p++;
    }
    p = skipSpace(p, end);
    if (match && p < end && *p == '"' && dstCap > 0) {
      // Copy until the closing quote, honouring escapes. An escaped quote is
      // written as a plain quote, which is what every caller wants here
      // (Hostname, Mac) and what a reader of the value would expect.
      p++;
      size_t n = 0;
      while (p < end && *p != '"') {
        if (*p == '\\' && p + 1 < end) {
          p++;
          if (*p == 'u' && p + 4 < end) {
            const size_t wrote =
                utf8FromEscape(p + 1, dst + n, dstCap > n + 1 ? dstCap - n - 1 : 0);
            if (wrote > 0) {
              n += wrote;
              p += 5; // u + 4 hex digits
              continue;
            }
            // Not an escape after all (or no room left): the backslash's own
            // partner is a character, exactly as for the other escapes.
          }
        }
        if (n + 1 >= dstCap) {
          break; // full: truncated, but NUL-terminated below
        }
        dst[n++] = *p++;
      }
      dst[n] = '\0';
      return true;
    }
    p = skipValue(p, end);
    p = skipSpace(p, end);
    if (p < end && *p == ',') {
      p++;
    }
  }
  if (dstCap > 0) {
    dst[0] = '\0';
  }
  return false;
}

// Read one numeric value. Returns false when the key is not there, or is there
// but holds something that is not a number - a string, true, null - because a
// caller asking for a power wants a power and should not have to check which of
// the two it got.
static inline bool getNumber(const char *json, size_t len, const char *key,
                             double *out) {
  const char *end = json + len;
  const char *p = skipSpace(json, end);
  const size_t keyLen = strlen(key);
  if (p < end && *p == '{') {
    p++;
  }
  while (p < end) {
    p = skipSpace(p, end);
    if (p >= end || *p == '}') {
      break;
    }
    if (*p != '"') {
      p = skipValue(p, end);
      continue;
    }
    const char *ks = ++p;
    while (p < end && *p != '"') {
      p += (*p == '\\' && p + 1 < end) ? 2 : 1;
    }
    if (p >= end) {
      break;
    }
    const size_t thisKeyLen = (size_t)(p - ks);
    const bool match = thisKeyLen == keyLen && memcmp(ks, key, keyLen) == 0;
    p++;
    p = skipSpace(p, end);
    if (p < end && *p == ':') {
      p++;
    }
    p = skipSpace(p, end);
    // A number starts with a digit or a minus; anything else is a value of a
    // different kind and gets skipped like any other.
    if (match && p < end && (*p == '-' || (*p >= '0' && *p <= '9'))) {
      char buf[40];
      size_t n = 0;
      while (p < end && n + 1 < sizeof(buf) &&
             (strchr("+-.eE", *p) != nullptr || (*p >= '0' && *p <= '9'))) {
        buf[n++] = *p++;
      }
      buf[n] = '\0';
      *out = strtod(buf, nullptr);
      return true;
    }
    p = skipValue(p, end);
    p = skipSpace(p, end);
    if (p < end && *p == ',') {
      p++;
    }
  }
  return false;
}

// Is the key there at all, whatever it holds? For a device whose fields depend
// on its model, this is how the driver learns what this one has: a key that is
// absent means the register does not exist in its protocol, which is the only
// honest way to find out.
static inline bool has(const char *json, size_t len, const char *key) {
  const char *end = json + len;
  const char *p = skipSpace(json, end);
  const size_t keyLen = strlen(key);
  if (p < end && *p == '{') {
    p++;
  }
  while (p < end) {
    p = skipSpace(p, end);
    if (p >= end || *p == '}') {
      break;
    }
    if (*p != '"') {
      p = skipValue(p, end);
      continue;
    }
    const char *ks = ++p;
    while (p < end && *p != '"') {
      p += (*p == '\\' && p + 1 < end) ? 2 : 1;
    }
    if (p >= end) {
      break;
    }
    const bool match = (size_t)(p - ks) == keyLen && memcmp(ks, key, keyLen) == 0;
    p = skipSpace(p + 1, end);
    if (p < end && *p == ':') {
      p++;
    }
    if (match) {
      return true;
    }
    p = skipValue(p, end);
    p = skipSpace(p, end);
    if (p < end && *p == ',') {
      p++;
    }
  }
  return false;
}

} // namespace json

#endif // RCT_DEVICE_JSON_H