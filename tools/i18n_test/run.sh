#!/bin/sh
# Host test for the language tables (src/i18n/).
#
# The shipped tables are compiled as they are, once per language, and the C++
# part asks each text of them what a text has to be: there, one line, in this
# language. check.py then compares the two tables with each other and with the
# enum, which is the half no compiler can do.
#
# Last: a scan of the directories that have been moved to the tables. A German
# character outside src/i18n/ is a text that was left behind - the web pages are
# through that, the display pages are the next stage.
set -e
cd "$(dirname "$0")/../.."

for lang in de en; do
  printf '  -- %s --\n' "$lang"
  flag=""
  if [ "$lang" = "en" ]; then
    flag="-DRCT_LANG_EN=1"
  fi
  # shellcheck disable=SC2086
  g++ -std=c++17 -Wall -Wextra -Isrc $flag \
      -o "/tmp/i18n_test_$lang" \
      tools/i18n_test/test_i18n.cpp "src/i18n/Lang$([ "$lang" = de ] && \
      echo De || echo En).cpp"
  "/tmp/i18n_test_$lang"
done

printf '  -- Tabellen gegeneinander --\n'
python3 tools/i18n_test/check.py

printf '  -- Deutscher Text ausserhalb der Tabellen --\n'
# Umlaute and a sharp s are what German looks like in the source; the tables
# are the one place where they belong. -r for the whole tree, and the two
# directories that have been converted so far.
for d in src/web src/storage; do
  hits=$(grep -rn '[äöüÄÖÜß]' "$d" --include='*.cpp' --include='*.h' || true)
  if [ -n "$hits" ]; then
    echo "  FEHLER $hits"
    exit 1
  fi
done
echo "  keiner"
