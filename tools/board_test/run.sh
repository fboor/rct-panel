#!/bin/sh
# Host test for the board abstraction (src/board/).
#
# Every board is compiled and checked with its own flag, so a profile that nobody
# builds cannot rot unnoticed - which is the failure a second board brings, since
# the first one is built by whoever flashes it and the second one is built by
# nobody until the hardware is on the desk.
#
# The check is that a board hangs together with itself: routable pins, no pin in two
# roles, sixteen RGB data lines for RGB565, and a geometry that fits the resolution
# it claims. It cannot tell whether a pin number is correct - that needs the board.
#
# SPDX-License-Identifier: MIT
set -e

cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/rct-board-test
CXX=${CXX:-g++}

# The Waveshare profile ends in an #error, because its pins are mapped but its
# driver is not written. It is compiled here anyway - with the error turned into a
# note - so that its numbers are checked today rather than on the day the hardware
# arrives.
fehler=0

for board in RCT_BOARD_GUITION_4848S040 RCT_BOARD_WAVESHARE_LCD7; do
  printf '\n--- %s ---\n' "$board"
  if "$CXX" -std=c++17 -Wall -Wextra -Isrc -D"$board" -o "$OUT" \
      tools/board_test/test_board.cpp src/ui/UiLayout.cpp 2>"$OUT.log"; then
    "$OUT" || fehler=1
  else
    # Erwartet ist nur der #error aus dem noch unvollstaendigen Profil. Alles andere
    # ist ein Fehler.
    if grep -q "does not run yet" "$OUT.log" && \
       [ "$(grep -c 'error:' "$OUT.log")" -eq 1 ]; then
      printf '  erwartet: das Waveshare-Profil ist noch nicht lauffaehig\n'
      printf '  Zahlen werden heute schon geprueft:\n'
      "$CXX" -std=c++17 -Wall -Wextra -Isrc -D"$board" \
             -DBOARD_TEST_IGNORE_INCOMPLETE=1 -o "$OUT" \
          tools/board_test/test_board.cpp src/ui/UiLayout.cpp
      "$OUT" || fehler=1
    else
      cat "$OUT.log"
      fehler=1
    fi
  fi
done

exit $fehler