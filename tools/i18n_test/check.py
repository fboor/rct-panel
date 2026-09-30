#!/usr/bin/env python3
"""Cross-check of the language tables in src/i18n/.

The compiler sees that a table has T_COUNT entries. It does not see that the
entry for T_ROW_UPTIME in the English table is the *uptime* and not the free
memory, and it does not see a %s that is in the German sentence and missing in
the English one - a value would simply not appear on the page.

So the three files are read as text and compared:

  src/i18n/Lang.h     the IDs, in order
  src/i18n/LangDe.cpp one entry per ID, the ID named at the end of the entry
  src/i18n/LangEn.cpp the same

An ID that is in the enum but used nowhere in the code is also reported: it is
either a text nobody shows any more or a leftover of a change half done.

Run from tools/i18n_test/run.sh, from the project root.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
I18N = ROOT / "src" / "i18n"

fail = 0
notes = 0


def bad(msg):
    global fail
    fail += 1
    print("  FEHLER %s" % msg)


def note(msg):
    global notes
    notes += 1
    print("  Hinweis %s" % msg)


def enum_ids(path):
    """The IDs of the enum, in order. T_COUNT is the end marker, not a text."""
    ids = []
    for line in path.read_text(encoding="utf-8").splitlines():
        m = re.match(r"\s*(T_[A-Z0-9_]+)\s*(?:=|,|$)", line)
        if m and m.group(1) != "T_COUNT":
            ids.append(m.group(1))
    return ids


def table(path):
    """The entries of one language file: [(id, text)], in the order written.

    An entry ends at the line that names it, so a text that is spread over
    several source lines is collected first and closed by its name.
    """
    out = []
    parts = []
    for line in path.read_text(encoding="utf-8").splitlines():
        m = re.search(r"//\s*(T_[A-Z0-9_]+)\s*$", line)
        if m is None:
            if line.lstrip().startswith('"'):
                parts.extend(re.findall(r'"((?:[^"\\]|\\.)*)"', line))
            continue
        parts.extend(re.findall(r'"((?:[^"\\]|\\.)*)"', line))
        out.append((m.group(1), unescape("".join(parts))))
        parts = []
    if parts:
        bad("%s: Text ohne ID am Ende (%r...)" %
            (path.name, "".join(parts)[:40]))
    return out


def unescape(s):
    return (s.replace('\\"', '"').replace("\\\\", "\\")
             .replace("\\n", "\n").replace("\\t", "\t"))


def placeholders(text):
    """The conversion specs of a format string, in order.

    '%%' is a literal percent and does not count - it takes no argument.
    """
    return re.findall(r"%(?!%)[-+ #0]*[0-9*]*(?:\.[0-9*]+)?(?:hh?|ll?|[Lj])?[a-zA-Z]",
                      text)


def main():
    ids = enum_ids(I18N / "Lang.h")
    de = table(I18N / "LangDe.cpp")
    en = table(I18N / "LangEn.cpp")

    for name, entries in (("LangDe.cpp", de), ("LangEn.cpp", en)):
        if len(entries) != len(ids):
            bad("%s: %d Eintraege, der enum hat %d" %
                (name, len(entries), len(ids)))
        seen = set()
        for i, (tid, _) in enumerate(entries):
            if tid in seen:
                bad("%s: %s steht zweimal drin" % (name, tid))
            seen.add(tid)
            if i < len(ids) and tid != ids[i]:
                bad("%s: Position %d ist %s, erwartet %s" %
                    (name, i, tid, ids[i]))

    # The same placeholders in the same order, or a value would go missing or
    # land in the wrong hole.
    de_by_id = dict(de)
    en_by_id = dict(en)
    for tid in ids:
        if tid not in de_by_id or tid not in en_by_id:
            continue
        pd = placeholders(de_by_id[tid])
        pe = placeholders(en_by_id[tid])
        if pd != pe:
            bad("%s: Platzhalter %s auf Deutsch, %s auf Englisch" %
                (tid, pd or "keine", pe or "keine"))

    # An ID nobody asks for any more.
    src = []
    for p in sorted((ROOT / "src").rglob("*")):
        if p.suffix in (".cpp", ".h") and p.parent.name != "i18n":
            src.append(p.read_text(encoding="utf-8"))
    code = "\n".join(src)
    for tid in ids:
        if not re.search(r"\b%s\b" % tid, code):
            note("%s wird im Code nicht benutzt" % tid)

    print("  %d Texte, %d mit Platzhaltern, %d Hinweise" %
          (len(ids),
           sum(1 for t in ids if placeholders(de_by_id.get(t, ""))),
           notes))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
