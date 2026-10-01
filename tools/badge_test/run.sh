#!/bin/sh
# Host test for the data-status decision behind the status badge, the line
# under the switching output and the inverter row of the web interface.
#
# src/DataStatus.h is the shipped header, compiled as-is with no stubs: it has
# to stay free of Arduino and LVGL, because three places decide with it and
# none of them may come out differently.
#
# Checks the five states, the order of the cases (a case moved up would show a
# device that never answered as merely "waiting"), the one-minute boundary
# exactly, and the millis() wrap.
set -e
cd "$(dirname "$0")/../.."
g++ -std=c++17 -Wall -Isrc -o /tmp/badge_test tools/badge_test/test_badge.cpp
exec /tmp/badge_test