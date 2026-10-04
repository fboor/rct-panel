#!/bin/sh
# Host test for the JSON scanner (src/device/Json.h).
#
# Header-only and free of Arduino, so this needs nothing but a host C++
# compiler and compiles the shipped header, not a copy. The test data is a
# captured answer of the OpenInverterGateway simulator, byte for byte as it came
# off the wire.
#
# SPDX-License-Identifier: MIT
set -e

cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/rct-json-scan-test
CXX=${CXX:-g++}

"$CXX" -std=c++17 -Wall -Wextra -Isrc -fsanitize=address,undefined -o "$OUT" \
  tools/json_scan_test/test_json_scan.cpp
"$OUT"