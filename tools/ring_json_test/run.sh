#!/bin/sh
# Host test for the ring JSON writer (src/web/RingJson.h).
#
# The writer is what /api/verlauf.json consists of, and the page answers "Daten
# konnten nicht geladen werden." for every answer it cannot parse. The comma
# between two entries once went missing for a ring whose first slots were still
# empty, which is what a panel produces in the first hours after a restart. The
# header is header-only and free of Arduino, so this compiles the shipped header
# rather than a copy.
#
# SPDX-License-Identifier: MIT
set -e

cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/rct-ring-json-test
CXX=${CXX:-g++}

"$CXX" -std=c++17 -Wall -Wextra -Isrc -fsanitize=address,undefined -o "$OUT" \
  tools/ring_json_test/test_ring_json.cpp
"$OUT"