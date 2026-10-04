#!/bin/sh
# Host test for the theme walk's decision (src/gui/Theme.h).
#
# Header-only and free of LVGL, so this needs nothing but a host C++ compiler
# and compiles the shipped header, not a copy.
#
# Why it exists: the decision was two if-statements inside the LVGL walk, so it
# could not be checked without a display - and one of its comparisons was wrong
# for as long as the light theme existed. The node fill was pure white, also the
# light page background, so a white node read as something sitting on the page
# and switching to dark repainted it: right after every boot, wrong after every
# tap of the theme button. This is that case, kept as a test.
#
# SPDX-License-Identifier: MIT
set -e

cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/rct-theme-test
CXX=${CXX:-g++}

"$CXX" -std=c++17 -Wall -Wextra -Isrc -o "$OUT" tools/theme_test/test_theme.cpp
"$OUT"