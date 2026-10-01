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

Two more things are looked for, in the code around the tables:

  An ID that is in the enum but used nowhere is reported: it is either a text
  nobody shows any more or a leftover of a change half done. The 128 fault
  texts are the exception - they are addressed as T_FAULT_0 + bit, because the
  bit is what the inverter reports.

  A German text that is in a table *and* still in the code as a string literal
  is the same mistake the other way round: the table would not be what the
  display shows. The serial log and the Wi-Fi portal are exempt, see
  sources_without_comments().

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


def strip_comments(src):
    """The source without its comments - what is left is code and text.

    A comment-stripper has to know about string literals first: a "//" inside
    "http://192.168.4.1" is text, not the start of a comment.
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c in '"\'':
            quote = c
            out.append(c)
            i += 1
            while i < n:
                if src[i] == "\\" and i + 1 < n:
                    out.append(src[i:i + 2])
                    i += 2
                    continue
                out.append(src[i])
                if src[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if src.startswith("//", i):
            while i < n and src[i] != "\n":
                i += 1
            continue
        if src.startswith("/*", i):
            i += 2
            while i < n and not src.startswith("*/", i):
                i += 1
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


# The directories whose texts a reader of this project sees: the display
# pages, the web pages, the SD status line. A table text has to come from here.
DISPLAY_DIRS = ("gui", "web", "storage")


def sources_without_comments():
    """[(path, code)] for every source file of the project, sans comments."""
    out = []
    for p in sorted((ROOT / "src").rglob("*")):
        if p.suffix in (".cpp", ".h") and p.parent.name != "i18n":
            code = strip_comments(p.read_text(encoding="utf-8"))
            kept, logging = [], False
            for ln in code.splitlines():
                if logging:
                    # Bis zum Semikolon gehoert die Zeile noch zum Aufruf.
                    logging = not ln.rstrip().endswith(";")
                    continue
                if "Serial." in ln:
                    logging = not ln.rstrip().endswith(";")
                    continue
                kept.append(ln)
            out.append((p, "\n".join(kept)))
    return out


def literal_pattern(text):
    """A regex for one text as it would stand in the source.

    The conversion specs stay what they are, so a format string is only found
    where it is really there; the letters around them are matched as words, so
    "Netz" is not found inside "Netzfrequenz".
    """
    parts = re.split(r"(%[-+ #0-9.*]*[a-zA-Z])", text)
    out = []
    for i, part in enumerate(parts):
        if i % 2:
            out.append(re.escape(part))
            continue
        out.append(r"(?<![\wäöüÄÖÜß])" + re.escape(part) +
                   r"(?![\wäöüÄÖÜß])")
    return "".join(out)


def hardcoded_texts(de_by_id, files):
    """The German texts that are in a table and in the code at the same time.

    Only the three display directories are searched, and two kinds of text are
    left out on purpose:

      The serial log is developer text and stays German in every build, so a
      text there is no proof that a table entry is unused. A log line is skipped
      as a whole, because a long one continues over several lines and "Serial."
      is only on the first of them.

      src/config holds the texts of the Wi-Fi provisioning portal. The portal
      is a library page with a language of its own, kept in English; see the
      note in src/i18n/Lang.h.
    """
    hits = 0
    for tid, text in de_by_id.items():
        if not tid.startswith(("T_D_", "T_FAULT_")):
            continue  # a web text cannot turn up on the display
        if len(text.strip()) < 2:
            continue
        pat = literal_pattern(text)
        for path, code in files:
            if path.parent.name not in DISPLAY_DIRS:
                continue
            m = re.search(pat, code)
            if m:
                line = code[:m.start()].count("\n") + 1
                bad("%s steht noch im Text von %s (Zeile %d): %r" %
                    (tid, path.name, line, text))
                hits += 1
    return hits


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

    # An ID nobody asks for any more. The 128 fault texts are addressed as
    # T_FAULT_0 + bit, so the one use of T_FAULT_0 is the use of all of them.
    files = sources_without_comments()
    code = "\n".join(c for _, c in files)
    for tid in ids:
        if not re.search(r"\b%s\b" % tid, code) and not re.match(
                r"T_FAULT_[1-9]\d*$", tid):
            note("%s wird im Code nicht benutzt" % tid)

    # A German text that is both in a table and in the code.
    hardcoded_texts(de_by_id, files)

    print("  %d Texte, %d mit Platzhaltern, %d Hinweise" %
          (len(ids),
           sum(1 for t in ids if placeholders(de_by_id.get(t, ""))),
           notes))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
