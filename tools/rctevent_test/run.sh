#!/bin/sh
# Host test for the event log decoder (src/rct/RctEvents.h).
#
# The wire format was taken from the official app (decompiled) and the rctclient
# documentation, which describe the same frame. What is tested here is what that
# format makes easy to get wrong without the device complaining: the zero
# timestamp that ends the table, a payload cut in the middle of an entry, the
# same page arriving again, and a ring that has to drop the oldest and not turn
# over.
set -e
cd "$(dirname "$0")/../.."
g++ -std=c++17 -Wall -Isrc -o /tmp/rctevent_test tools/rctevent_test/test_rct_events.cpp
exec /tmp/rctevent_test
