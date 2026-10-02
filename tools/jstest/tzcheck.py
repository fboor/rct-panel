#!/usr/bin/env python3
"""Write the check values for tools/jstest from Python's zoneinfo.

The browser has to group the CSV rows into the same days the panel does, and it
does that from the POSIX time zone rule the panel sends (src/config
Configuration.h). zoneinfo is a completely separate implementation of that rule,
so comparing the two is worth something - a rule read half right costs an hour
for half the year, and the day boundaries move with it.

    python3 tools/jstest/tzcheck.py

Writes tools/jstest/tzcheck.json. The values belong in the repository: the test
must run without Python, and a test whose check values are regenerated on the
run cannot catch a change in the rule itself.
"""
import datetime
import json
import pathlib
import zoneinfo

ZONE = "Europe/Berlin"
YEAR = 2026
OUT = pathlib.Path(__file__).with_name("tzcheck.json")


def main() -> None:
    tz = zoneinfo.ZoneInfo(ZONE)
    stamps = set()
    # Every six hours of the whole year: the offset has to be right at all of
    # them, not only near the change.
    start = datetime.datetime(YEAR, 1, 1, tzinfo=datetime.timezone.utc)
    for step in range(0, 366 * 4):
        stamps.add(int((start + datetime.timedelta(hours=6 * step)).timestamp()))
    # Every hour around the two switch-over dates.
    for day in ("03-28", "03-29", "03-30", "10-24", "10-25", "10-26"):
        month, dom = (int(x) for x in day.split("-"))
        for hour in range(24):
            stamps.add(int(datetime.datetime(
                YEAR, month, dom, hour, tzinfo=datetime.timezone.utc).timestamp()))

    out = []
    for t in sorted(stamps):
        local = datetime.datetime.fromtimestamp(t, tz)
        out.append({
            "t": t,
            "off": local.utcoffset().total_seconds() / 3600,
            "day": f"{local.year:04d}-{local.month:02d}-{local.day:02d}",
            "hm": f"{local.hour:02d}:{local.minute:02d}",
        })
    OUT.write_text(json.dumps(out), encoding="utf-8")
    print(f"{len(out)} Stichproben aus {ZONE} nach {OUT.name} geschrieben")


if __name__ == "__main__":
    main()
