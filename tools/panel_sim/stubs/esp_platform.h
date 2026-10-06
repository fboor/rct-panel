// The ESP-IDF pieces the shipped GUI headers use, on the build machine.
//
// Three things, all of them the platform rather than the design:
//
//   strlcpy              a BSD function the firmware gets from newlib; glibc has
//                        no strlcpy until 2.38 and this system has none, so it is
//                        supplied here with the same contract: always NUL-
//                        terminated, returns the length it wanted
//   heap_caps_malloc     the screenshot buffer, which on the panel is PSRAM; on
//                        the host it is ordinary malloc, and the size stays the
//                        same so that a "not enough memory" refusal still happens
//                        at the same point
//
// SPDX-License-Identifier: MIT
#ifndef RCT_PANEL_SIM_ESP_H
#define RCT_PANEL_SIM_ESP_H

#include <cstdlib>
#include <cstring>

#ifndef RCT_HAVE_STRLCPY
inline size_t strlcpy(char *dst, const char *src, size_t cap) {
  const size_t n = strlen(src);
  if (cap > 0) {
    const size_t k = (n >= cap) ? cap - 1 : n;
    memcpy(dst, src, k);
    dst[k] = '\0';
  }
  return n;
}
#endif

// The capability bits are accepted and ignored: on the host there is one heap and
// asking for a particular one is a question with a single answer.
#define MALLOC_CAP_EXEC  (1 << 0)
#define MALLOC_CAP_32BIT (1 << 1)
#define MALLOC_CAP_8BIT  (1 << 2)
#define MALLOC_CAP_DMA   (1 << 3)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)

inline void *heap_caps_malloc(size_t size, uint32_t) { return malloc(size); }
inline void *heap_caps_calloc(size_t n, size_t size, uint32_t) {
  return calloc(n, size);
}
inline void heap_caps_free(void *p) { free(p); }
inline size_t heap_caps_get_free_size(uint32_t) { return 64u * 1024u; }

#endif // RCT_PANEL_SIM_ESP_H