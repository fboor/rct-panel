#!/bin/sh
# Host test for the chart palette (src/Charts.h).
#
# Six colours that the panel's 24 h chart and the browser's copy of it share.
# The point of the test is not that they look nice but that they stay separable:
# a purple and a blue that are 3/255 apart for a deuteranope are one line, and
# nobody notices on a wall. Compiles the shipped header, not a copy of the six
# values - a palette that is only tested where it is defined does not hold.
#
# SPDX-License-Identifier: MIT
set -e

cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/rct-chart-color-test
CXX=${CXX:-g++}

"$CXX" -std=c++17 -Wall -Wextra -Isrc -fsanitize=address,undefined -o "$OUT" \
  tools/chart_color_test/test_chart_color.cpp -lm
"$OUT"