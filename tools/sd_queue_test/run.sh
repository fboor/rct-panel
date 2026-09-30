#!/bin/sh
# Host test for the SD logger's pure logic: the CSV row and the parked-rows
# ring (src/storage/CsvRow.h, src/storage/RowQueue.h).
#
# Both are header-only and free of Arduino, so this needs nothing but a host
# C++ compiler and compiles the shipped headers, not a copy of them.
#
# The card itself cannot be tested here. Everything that decides whether a day
# of samples survives a card outage - the buffer depth, the FIFO order, the
# month split, the row format - is pure bookkeeping and is checked here.
#
# SPDX-License-Identifier: MIT
set -e

cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/rct-sd-queue-test
CXX=${CXX:-g++}

"$CXX" -std=c++17 -Wall -Wextra -Isrc -o "$OUT" tools/sd_queue_test/test_rowqueue.cpp
"$OUT"
