#!/bin/sh
# Host test for the flow diagram's layout (src/gui/FlowLayout.h).
#
# Free of LVGL, so this needs nothing but a host C++ compiler. It compiles the
# shipped headers and the shipped UiLayout.cpp - the board's numbers live in a
# source file now, and a test that stubbed them would be testing itself.
#
# The six cases are the six device families that can exist: everything, each of
# the three meters missing, and "nothing answered yet". What is checked is that
# a full device comes out pixel-identical to the fixed layout the panel has been
# drawing all along - a refactor like this is only worth doing if the common case
# does not move.
#
# SPDX-License-Identifier: MIT
set -e

cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/rct-flow-layout-test
CXX=${CXX:-g++}

"$CXX" -std=c++17 -Wall -Wextra -Isrc -o "$OUT" \
    tools/flow_layout_test/test_flow_layout.cpp src/ui/UiLayout.cpp
"$OUT"
