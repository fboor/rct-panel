#!/bin/sh
# Host test for the number formatting of the display.
#
# src/NumFmt.h is the shipped header, compiled as-is with no stubs: it must stay
# free of Arduino, because the rule it holds (a value that rounds to zero shows
# no minus) is otherwise only ever checked by looking at a panel.
#
# Compiled twice, once per language: fmtNumLang() follows the build, and the
# comma and the point are the same rule on top of a different separator.
set -e
cd "$(dirname "$0")/../.."

for lang in de en; do
  printf '\n---- Build %s ----\n' "$lang"
  flag=""
  if [ "$lang" = "en" ]; then
    flag="-DRCT_LANG_EN=1"
  fi
  # shellcheck disable=SC2086
  g++ -std=c++17 -Wall -Isrc $flag -o "/tmp/numfmt_test_$lang" \
      tools/numfmt_test/test_numfmt.cpp
  "/tmp/numfmt_test_$lang"
done
