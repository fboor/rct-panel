#!/bin/sh
# Host test for the receive state machine (src/rct/RctFrame.h).
#
# A frame cut at the wrong length is dropped by the device's own checksum and
# the panel then says "keine Daten" against a healthy inverter. Nothing
# crashes, so the check values have to live here: the frames are built with the
# same checksum code the device uses (src/rct/RctCrc.h, itself covered by
# tools/crc_test) and the arithmetic under test is where the length field sits,
# how long the frame gets, and which span the checksum covers.
set -e
cd "$(dirname "$0")/../.."
g++ -std=c++17 -Wall -Isrc -o /tmp/rctframe_test tools/rctframe_test/test_rct_frame.cpp
exec /tmp/rctframe_test