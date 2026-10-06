#!/bin/sh
# Host test for the switched output.
#
# src/output/Relay.cpp is compiled as-is against the stubs in stubs/, so what
# is tested is the shipped code, not a copy of it. The stub is deliberately
# small: millis() comes from a global, digitalWrite() writes to a variable the
# test can read, and Preferences keeps the last value per key in RAM.
#
# Checks the things that cannot be checked on the bench: the 20 s on-delay, the
# 60 s minimum hold, the 20 % hysteresis band, "no data means off", the S0
# handling of the surplus rule, the test sequence, the mode cycle and the NVS
# round-trip including out-of-range values.
#
# The German table comes along because Relay.cpp takes the name of a function
# from it (the display shows it) - a missing table is a link error, not a wrong
# name.
set -e
cd "$(dirname "$0")/../.."
g++ -std=c++17 -Wall -DRCT_BOARD_GUITION_4848S040 -Itools/relay_test/stubs -Isrc \
    -o /tmp/relay_test tools/relay_test/test_relay.cpp src/i18n/LangDe.cpp
exec /tmp/relay_test
