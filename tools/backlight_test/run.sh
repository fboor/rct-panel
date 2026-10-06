#!/bin/sh
# Host test for the backlight.
#
# src/display/Backlight.cpp is compiled as-is against the stub in stubs/, so what
# is tested is the shipped code, not a copy of it: millis() comes from a global,
# the LEDC calls are recorded, and ledcSetup() can be made to fail.
#
# What cannot be checked on the bench without sitting in front of the panel for
# ten minutes: the three-minute dimming, the five-minute switch-off, that a
# touch brings the light back and restarts both timers, that the millis() wrap
# after 49 days is harmless, that a panel without a touch controller never
# switches itself off, and that a refused LEDC setup leaves the panel lit.
set -e
cd "$(dirname "$0")/../.."
g++ -std=c++17 -Wall -DRCT_BOARD_GUITION_4848S040 -Itools/backlight_test/stubs -Isrc \
    -o /tmp/backlight_test tools/backlight_test/test_backlight.cpp
exec /tmp/backlight_test