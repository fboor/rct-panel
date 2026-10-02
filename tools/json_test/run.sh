#!/bin/sh
# Host test for the JSON number formatting of the web endpoints.
#
# src/web/Json.h is the shipped header, compiled as-is with no stubs: it must
# stay free of Arduino, because the rule it holds (a value that is not a number
# must not reach the parser as "nan") is otherwise only ever checked by opening
# a browser on a phone.
set -e
cd "$(dirname "$0")/../.."

printf '\n---- Build ----\n'
g++ -std=c++17 -Wall -Isrc -o /tmp/json_test tools/json_test/test_json.cpp
/tmp/json_test
