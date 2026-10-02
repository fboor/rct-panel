#!/bin/sh
# All host tests: the pure logic of the firmware, compiled and run on the
# build machine. No hardware, no ESP-IDF, no card - just the shipped headers.
#
#   tools/run_host_tests.sh
#
# What is in here and what is not:
#   relay_test     the relay state machine (timings, hysteresis, five
#                  functions, the boot state)
#   sd_queue_test  the CSV row and the parked-rows ring of the SD logger
#   numfmt_test    the display's number formatting: a value that rounds to zero
#                  shows no minus - once per language, because the decimal
#                  separator follows the build
#   i18n_test      the language tables: every text there and in this language,
#                  the same IDs and placeholders on both sides, and no German
#                  character left in the code that has been moved out
#   backlight_test the backlight timers: dim after 3 min, off after 5, a touch
#                  wakes it, and a panel without touch never switches itself off
#   badge_test     the data-status decision behind the badge, the switching
#                  output line and the web page: five states, their order, and
#                  the one-minute boundary
#   crc_test       the frame checksum: check values computed independently of
#                  the implementation, plus the padding rule for odd lengths
#                  that the three-byte extension frame depends on
#   json_test      the numbers in the web interface's JSON answers: fixed-point
#                  without an exponent, no trailing zeros, and null instead of
#                  a NaN - the case a parser stops reading at
#
# The RCT simulator test used to be here. It imports rctclient (GPL-3.0), so the
# simulator lives outside this repository now - see its own README. It is run
# when it is next to the project, and skipped with a note when it is not.
#
# What cannot be in here: everything that needs the card, the display, the
# inverter or the Wi-Fi. The list of those is in docs/web-interface.md
# ("Was noch offen ist") - it is short on purpose, and everything on it needs a
# panel on the wall.
#
# SPDX-License-Identifier: MIT
set -e

cd "$(dirname "$0")/.."

fail=0
for t in relay_test sd_queue_test numfmt_test i18n_test backlight_test badge_test crc_test json_test; do
  printf '\n=== %s ===\n' "$t"
  if sh "tools/$t/run.sh"; then
    :
  else
    fail=1
  fi
done

# The simulator test is Python and drives the sim in-process on a loopback
# port, so it needs no compiler and no build directory. It is not part of this
# repository (GPL-3.0 dependency, see the header), so it only runs when someone
# kept it next to the project.
for simdir in ../rct-panel-simulator ../rct_sim "$PWD"; do
  if [ -f "$simdir/rct_sim_test.py" ]; then
    printf '\n=== rct_sim_test (%s) ===\n' "$simdir"
    if python3 "$simdir/rct_sim_test.py"; then
      :
    else
      fail=1
    fi
    break
  fi
done

printf '\n'
if [ "$fail" -eq 0 ]; then
  echo "alle Host-Tests gruen"
else
  echo "FEHLER in mindestens einem Host-Test"
fi
exit "$fail"
