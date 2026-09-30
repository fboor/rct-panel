#!/bin/sh
# Host test for the number formatting of the display.
#
# src/NumFmt.h is the shipped header, compiled as-is with no stubs: it must stay
# free of Arduino, because the rule it holds (a value that rounds to zero shows
# no minus) is otherwise only ever checked by looking at a panel.
set -e
cd "$(dirname "$0")/../.."
g++ -std=c++17 -Wall -Isrc -o /tmp/numfmt_test tools/numfmt_test/test_numfmt.cpp
exec /tmp/numfmt_test
