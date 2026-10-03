#!/bin/sh
# Host test for the device abstraction's rules (src/device/Rules.h,
# src/device/DeviceState.h).
#
# Both are header-only and free of Arduino, so this needs nothing but a host
# C++ compiler and compiles the shipped headers, not a copy of them.
#
# Why it exists: the household was computed in five places in C++ and once in
# the browser, always as meter plus external generation, because that is what
# the RCT's load meter needs. A device whose meter sees the generator itself
# would have needed all six corrected, and the one that was forgotten would
# have been a silently wrong number. The rules now live in one header and are
# checked here for both kinds of device.
#
# SPDX-License-Identifier: MIT
set -e

cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/rct-device-test
CXX=${CXX:-g++}

"$CXX" -std=c++17 -Wall -Wextra -Isrc -o "$OUT" tools/device_test/test_device_rules.cpp
"$OUT"