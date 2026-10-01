#!/bin/sh
# Host test for the frame checksum (src/rct/RctCrc.h).
#
# A wrong checksum does not crash: the device and the simulator drop the frame
# and the panel shows "keine Daten" against a healthy inverter. So the check
# values are computed on the build machine, from the polynomial and with a
# table-driven implementation that is not the one under test - plus two frames
# that a second, foreign implementation has already accepted (the simulator and
# the device).
set -e
cd "$(dirname "$0")/../.."
g++ -std=c++17 -Wall -Isrc -o /tmp/crc_test tools/crc_test/test_crc.cpp
exec /tmp/crc_test