#!/bin/sh
# Host test for the language tables (src/i18n/).
#
# The shipped tables are compiled as they are, once per language, and the C++
# part asks each text of them what a text has to be: there, one line, in this
# language. check.py then compares the two tables with each other and with the
# enum, which is the half no compiler can do, and looks for texts that are in a
# table and in the code at the same time.
#
# Last: a scan of the directories that hold no German text outside the tables
# any more.
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
# Two checks for the same thing: the umlauts in a string, and the texts that
# have been moved into the tables but are still in the code as literals. The
# second one is check.py's job because it knows the texts; this one catches the
# umlauts anywhere, including ones nobody put in a table yet.
#
# src/gui is not in the umlaut scan: its comments are German, like the rest of
# the source, and only the string literals are a text for the reader. check.py
# reads them with the comments removed.
for d in src/web src/storage; do
  hits=$(grep -rn '[äöüÄÖÜß]' "$d" --include='*.cpp' --include='*.h' || true)
  if [ -n "$hits" ]; then
    echo "  FEHLER $hits"
    exit 1
  fi
done
echo "  keiner"
