#!/bin/sh
# Host test for the OpenInverterGateway field tables (src/oig/OigFields.h).
#
# Header-only and free of Arduino, so this needs nothing but a host C++
# compiler and compiles the shipped header.
#
# The expected field names were extracted from the OpenInverterGateway source -
# all seven Growatt protocols - so every list is checked against what the
# devices really publish. It exists because the grid frequency was missing on a
# real Growatt305: the driver asked for GridFrequency, the device says
# AcFrequency, and the value stayed 0, which the display then showed as if it
# had been measured.
#
# SPDX-License-Identifier: MIT
set -e

cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/rct-oig-field-test
CXX=${CXX:-g++}

"$CXX" -std=c++17 -Wall -Wextra -Isrc -o "$OUT" tools/oig_field_test/test_oig_fields.cpp
"$OUT"
