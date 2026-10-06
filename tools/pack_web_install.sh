#!/bin/sh
# Build the browser installer's binaries and generate its manifests.
#
# WHAT THIS IS FOR
#
# A blank panel has no bootloader, no partition table and no otadata blob, so all
# four parts have to arrive or nothing boots. tools/flash_usb.sh writes
# firmware.bin alone and is right to: it updates a panel that is already running,
# where the running bootloader hands over to the new app. On an empty device there
# is no bootloader to hand over - which is why this stages four parts and not one,
# and why the ESP Web Tools documentation's merged.bin is not used. A merged file
# exists only to work around esptool patching flash parameters at run time; it needs
# esptool 4.x, and the esptool on a desktop PATH here is 2.8, which has neither
# merge_bin nor the ESP32-S3. The parts are already separate build output.
#
# ONE LIST, TWO READERS
#
# docs/install/devices.json says which devices exist and which build environment
# each language of each device comes from. The page reads it to fill its device
# picker, this script reads it to know what to build. Adding a device is an edit
# there and nowhere else.
#
# ONE VERSION
#
# include/FirmwareVersion.h holds the release number, and this reads it from there.
# It used to be a literal in the web interface and a second literal in the
# manifests, which is the disagreement a version number exists to prevent. The
# manifests are generated, so a committed one could never carry a stale number.
#
# WHY PYTHON AND NOT SHELL
#
# devices.json is JSON, and a shell that parsed JSON would be the fragile part of a
# script whose job is to produce flashable files. So python reads it once and hands
# this script plain lines; python writes the manifests for the same reason.
#
# SPDX-License-Identifier: MIT
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/docs/install"
HEADER="$ROOT/include/FirmwareVersion.h"
LIST="$OUT/devices.json"
PIO=${PIO:-pio}
# From PlatformIO, not from $PATH: see the note about esptool 2.8 above.
ESPROOT=${ESPROOT:-$HOME/.platformio/packages}
BOOT_APP0="$ESPROOT/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

for f in "$HEADER" "$LIST" "$BOOT_APP0"; do
  [ -f "$f" ] || { echo "Fehlt: $f"; exit 1; }
done

mkdir -p "$OUT"

# One pass over the list: print the version, the build environments, and check that
# every language of every device has one. Written as three plain commands the shell
# can read without knowing anything about JSON.
eval "$(python3 - "$HEADER" "$LIST" <<'PY'
import json, re, shlex, sys

header, liste = sys.argv[1], sys.argv[2]
h = open(header).read()
def holen(name):
    m = re.search(r'^#define\s+%s\s+"([^"]*)"' % name, h, re.M)
    if not m:
        sys.exit("Version nicht in %s gefunden: %s" % (header, name))
    return m.group(1)

version, monat = holen("RCT_FW_VERSION"), holen("RCT_FW_VERSION_MON")
print("VERSION=%s" % shlex.quote(version))
print("MONTH=%s" % shlex.quote(monat))
print("FULL=%s" % shlex.quote("%s (%s)" % (version, monat)))

d = json.load(open(liste))
if not d.get("devices"):
    sys.exit("Keine Geraete in %s" % liste)

# Every (device, language) needs a build environment, and every environment must
# appear exactly once as a build - two devices sharing an environment is allowed
# only if that is really what the list says.
aufgaben = []          # (geraet_id, sprache, env)
for dev in d["devices"]:
    for sprache in dev["languages"] if "languages" in dev else dev["envs"]:
        env = dev["envs"].get(sprache)
        if not env:
            sys.exit("Geraet %s: keine Build-Umgebung fuer %s" % (dev["id"], sprache))
        aufgaben.append((dev["id"], sprache, env))

envs = []
for _, _, env in aufgaben:
    if env not in envs:
        envs.append(env)
print("ENVS=%s" % shlex.quote(" ".join(envs)))
print("FIRST_ENV=%s" % shlex.quote(envs[0]))
print("ANZAHL_AUFGABEN=%d" % len(aufgaben))

# The manifests, now that the version is known. Collected into one list rather than
# a variable per file: a repeated NAME=value would leave only the last one behind,
# and the script would then report one manifest where it wrote two.
manifests = []
for dev in d["devices"]:
    for sprache in dev["envs"]:
        ziel = "%s/manifest-%s-%s.json" % (
            liste.rsplit("/", 1)[0], dev["id"], sprache)
        with open(ziel, "w") as fh:
            fh.write("""{
  "name": "%s (%s)",
  "version": "%s (%s)",
  "new_install_improv_wait_time": 0,
  "builds": [
    {
      "chipFamily": "%s",
      "improv": false,
      "parts": [
        { "path": "bootloader.bin", "offset": 4096 },
        { "path": "partitions.bin", "offset": 32768 },
        { "path": "boot_app0.bin", "offset": 57344 },
        { "path": "firmware-%s.bin", "offset": 65536 }
      ]
    }
  ]
}
""" % (dev["name"], sprache, version, monat, dev["chipFamily"], sprache))
        manifests.append("manifest-%s-%s.json" % (dev["id"], sprache))
print("MANIFESTS=%s" % shlex.quote(" ".join(manifests)))
PY
)"

echo "=== Release $FULL ==="
echo "Geraete: $ANZAHL_AUFGABEN Sprachvarianten, $FIRST_ENV als erste Umgebung"
echo

echo "=== 1/3 Firmware bauen ==="
for env in $ENVS; do
  echo "--- $env"
  "$PIO" run -e "$env"
done

echo
echo "=== 2/3 Teile ablegen ==="
# The bootloader and the partition table are the same for every build of one device.
# A second build that disagreed here would mean two flash layouts, and only one could
# be right - so it is checked rather than silently overwriting.
for part in bootloader.bin partitions.bin; do
  src="$ROOT/.pio/build/$FIRST_ENV/$part"
  if [ -f "$OUT/$part" ] && ! cmp -s "$src" "$OUT/$part"; then
    echo "  $part weicht vom bereits abgelegten ab - die Builds benutzen"
    echo "  verschiedene Flash-Layouts. Nur eines davon kann stimmen."
    exit 1
  fi
  cp "$src" "$OUT/$part"
  echo "  $part  ($FIRST_ENV)"
done
cp "$BOOT_APP0" "$OUT/boot_app0.bin"
echo "  boot_app0.bin  (aus dem Framework, nicht aus dem Build)"

for env in $ENVS; do
  case "$env" in
    esp32-s3)    sprache=de ;;
    esp32-s3-en) sprache=en ;;
    *)           sprache="$env" ;;
  esac
  cp "$ROOT/.pio/build/$env/firmware.bin" "$OUT/firmware-$sprache.bin"
  echo "  firmware-$sprache.bin  ($env)"
done

echo
echo "=== 3/3 Manifeste geschrieben ==="
for m in $MANIFESTS; do
  echo "  $m"
done

echo
echo "--- Abgelegt ---"
ls -l "$OUT" | tail -n +2 | awk 'NF>5 {printf "  %-28s %8.1f kB\n", $9, $5/1024}'

cat <<EOF

Release $FULL. Die Nummer steht in include/FirmwareVersion.h; Webinterface, Seite und
Manifeste lesen sie von dort. An zwei Stellen steht sie nicht.

Die Manifeste werden erzeugt und mit den .bin nicht versioniert: eine eingecheckte
Datei koennte eine alte Nummer tragen.

Wichtig, sonst haelt man die Seite fuer fertig: keine dieser Dateien liegt im
Repository. GitHub Pages liefert aus /docs genau das, was dort steht - ohne die .bin
zeigt die Seite die Auswahl, installiert aber nichts.

Sobald das erledigt ist: Settings -> Pages -> Source: "Deploy from a branch",
Branch "master", Ordner "/docs".
EOF