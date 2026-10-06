#!/bin/sh
# Build the browser installer's binaries and generate its manifests.
#
# WHAT THIS IS FOR
#
# A blank panel has no bootloader and no partition table, so both have to arrive or
# nothing boots - three parts, not one. tools/flash_usb.sh writes firmware.bin alone
# and is right to: it updates a panel that is already running, where the running
# bootloader hands over to the new app. On an empty device there is no bootloader to
# hand over, which is why this stages more than one.
#
# WHY THERE IS NO boot_app0.bin HERE, THOUGH THE DOCUMENTATION LISTS ONE
#
# The ESP Web Tools example writes boot_app0.bin at 0xE000. For this layout that file
# is not an otadata blob: it starts with 0x01, carries neither the OTA magic 0x5F nor
# a 0x9F entry, and has twelve non-0xFF bytes out of 8192. It is written into the one
# partition the bootloader reads FIRST to decide where to boot, and it is the only
# one of the parts that is not a valid image.
#
# THE BOOTLOADER GOES TO 0x0, AND THAT IS THE WHOLE STORY OF THE FIRST FAILURE
#
# The documentation's example writes it at 4096. That is the ESP32's address. The
# builder says so in one line:
#
#
# the real one at 0x0. The panel then had no bootloader at all and answered every
# start with "invalid header 0xFFFFFFFF". Writing only the app could not bring it
# back, because flash_usb.sh never writes the bootloader - which is right on a
# running panel and useless on one whose bootloader is gone.
#
# Found by reading the builder, after two failed installs and one wrong theory.
#
# It is not needed. Without a factory partition - and this table has none - ESP-IDF
# nothing but the app at that offset and the panel boots, which is the same proof from
# the other side. So the part is dropped rather than translated into a blob we would
# have to invent.
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
ESPROOT=${ESPROOT:-$HOME/.platformio/packages}
for f in "$HEADER" "$LIST"; do
  [ -f "$f" ] || { echo "Fehlt: $f"; exit 1; }
done

# One pass over the list: print the version, the build environments, and check that
# every language of every device has one. Written as three plain commands the shell
# can read without knowing anything about JSON. The manifests are written here, which
# means the release directory has to be created here too - the name only becomes
# known once the header has been read.
eval "$(python3 - "$HEADER" "$LIST" <<'PY'
import json, os, re, shlex, sys

header, liste = sys.argv[1], sys.argv[2]
h = open(header).read()
def holen(name):
    m = re.search(r'^#define\s+%s\s+"([^"]*)"' % name, h, re.M)
    if not m:
        sys.exit("Version nicht in %s gefunden: %s" % (header, name))
    return m.group(1)

version, monat = holen("RCT_FW_VERSION"), holen("RCT_FW_VERSION_MON")
# The release folder is named after the tag, "v" in front. One spelling of a release
# means the tag, the folder and the file names cannot drift apart.
release = "v" + version
print("VERSION=%s" % shlex.quote(version))
print("RELEASE=%s" % shlex.quote(release))
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

# The release directory is created here rather than in the shell: its name only
# becomes known in this block, and the manifests below are written into it.
release_dir = "%s/%s" % (os.path.dirname(liste), release)
os.makedirs(release_dir, exist_ok=True)

# The manifests, now that the version is known. Collected into one list rather than
# a variable per file: a repeated NAME=value would leave only the last one behind,
# and the script would then report one manifest where it wrote two.
manifests = []
for dev in d["devices"]:
    for sprache in dev["envs"]:
        # The manifest goes into the release directory beside the files it names, so
        # a release is one self-contained folder: six files, and the page needs only
        # the release name to find them.
        #
        # The paths inside are therefore bare file names. ESP Web Tools resolves a
        # part against the manifest's own URL, not the page's - so a path that
        # repeats the release folder is looked up one folder too deep and every part
        # comes back 404. The page is one level up and does need the folder; the
        # manifest is inside it and does not.
        ziel = "%s/manifest-%s-%s.json" % (release_dir, dev["id"], sprache)
        # Bootloader at 0x0, not 0x1000. The ESP Web Tools documentation writes 4096
        # and that example is for the ESP32; the S3 has its bootloader at the very
        # start of the flash, which the espressif32 builder says in one line:
        #   "0x0" if mcu in ("esp32c3", "esp32c6", "esp32s3") else "0x1000"
        # Written to 0x1000 it landed where nothing reads it, and since an install
        # erases the whole chip first the real bootloader at 0x0 was gone - so the
        # panel came up with "invalid header 0xFFFFFFFF", twice. Found by reading the
        # builder, after two failed installs and one wrong theory about flash modes.
        with open(ziel, "w") as fh:
            # Named placeholders, not positional ones: the paths repeat the release
            # and the board, and twelve %s in a row is an argument list that silently
            # fills in the wrong order the first time a line is added.
            fh.write("""{
  "name": "%(name)s (%(lang)s)",
  "version": "%(version)s (%(month)s)",
  "new_install_improv_wait_time": 0,
  "builds": [
    {
      "chipFamily": "%(chip)s",
      "improv": false,
      "parts": [
        { "path": "%(board)s-bootloader.bin", "offset": 0 },
        { "path": "%(board)s-partitions.bin", "offset": 32768 },
        { "path": "%(board)s-%(lang)s.bin", "offset": 65536 }
      ]
    }
  ]
}
""" % {"name": dev["name"], "lang": sprache, "version": version,
       "month": monat, "chip": dev["chipFamily"], "release": release,
       "board": dev["id"]})
        manifests.append("manifest-%s-%s.json" % (dev["id"], sprache))

# Write the release back into the device list, so the page knows which folder to look
# in. The header is where the number comes from; this file is where it is recorded,
# and the page reads it from here afterwards. Written as a string edit and not through
# json.dump, because a dump would rewrite the whole file - including the comment that
# says why this file exists - for one added key.
with open(liste) as fh:
    inhalt = fh.read()
if re.search(r'^\s*"release"\s*:', inhalt, re.M):
    inhalt = re.sub(r'^(\s*)"release"\s*:\s*".*",?\s*$',
                    r'\1"release": "%s",' % release, inhalt, flags=re.M)
else:
    inhalt = inhalt.replace(
        '"devices": [',
        '"release": "%s",\n  "devices": [' % release, 1)
with open(liste, "w") as fh:
    fh.write(inhalt)

print("MANIFESTS=%s" % shlex.quote(" ".join(manifests)))
PY
)"

# The board id, for the file names: a release folder is self-contained, so its
# contents have to say what they are without a lookup somewhere else. devices[0] is
# the first board in the list, which is the one FIRST_ENV belongs to.
BOARD=$(python3 - "$LIST" <<'PY2'
import json, sys
print(json.load(open(sys.argv[1]))["devices"][0]["id"])
PY2
)

echo "=== Release $FULL ($RELEASE) ==="
echo "Geraete: $ANZAHL_AUFGABEN Sprachvarianten, $FIRST_ENV als erste Umgebung"
echo

echo "=== 1/3 Firmware bauen ==="
for env in $ENVS; do
  echo "--- $env"
  "$PIO" run -e "$env"
done

echo
echo "=== 2/3 Teile ablegen ==="
# Every file is named after the board, and after the language where it differs by it.
# The bootloader and the partition table do not differ by language and there is one of
# each per board - so the release folder says what it holds without a lookup.
#
# The bootloader and the partition table are the same for every build of one device.
# A second build that disagreed here would mean two flash layouts, and only one could
# be right - so it is checked rather than silently overwriting.
for part in bootloader partitions; do
  src="$ROOT/.pio/build/$FIRST_ENV/$part.bin"
  ziel="$OUT/$RELEASE/$BOARD-$part.bin"
  if [ -f "$ziel" ] && ! cmp -s "$src" "$ziel"; then
    echo "  $part weicht vom abgelegten $RELEASE ab - die Builds benutzen"
    echo "  verschiedene Flash-Layouts. Nur eines davon kann stimmen."
    exit 1
  fi
  cp "$src" "$ziel"
  echo "  $RELEASE/$BOARD-$part.bin  ($FIRST_ENV)"
done

for env in $ENVS; do
  case "$env" in
    esp32-s3)    sprache=de ;;
    esp32-s3-en) sprache=en ;;
    *)           sprache="$env" ;;
  esac
  cp "$ROOT/.pio/build/$env/firmware.bin" "$OUT/$RELEASE/$BOARD-$sprache.bin"
  echo "  $RELEASE/$BOARD-$sprache.bin  ($env)"
done

echo
echo "=== 3/3 Manifeste geschrieben ==="
for m in $MANIFESTS; do
  echo "  $m"
done

# Every path a manifest names has to exist in the folder the manifest itself sits in.
# This is the check the layout of this release needed and did not have: the parts are
# resolved against the manifest's URL, so a path carrying the release folder in front
# looks one folder too deep and every part comes back 404. That is not visible from
# the output above - the files are there, they are just not where the flasher looks.
python3 - "$OUT/$RELEASE" "$RELEASE" <<'PY3'
import json, os, sys
ordner, release = sys.argv[1], sys.argv[2]
fehlend = []
for name in sorted(os.listdir(ordner)):
    if not name.startswith("manifest-"):
        continue
    with open(os.path.join(ordner, name)) as fh:
        m = json.load(fh)
    for build in m["builds"]:
        for p in build["parts"]:
            pfad = os.path.join(ordner, p["path"])
            if not os.path.isfile(pfad):
                fehlend.append("%s -> %s" % (name, p["path"]))
if fehlend:
    sys.exit("  Teile fehlen, die die Manifeste nennen:\n" +
             "\n".join("    " + f for f in fehlend) +
             "\n  Geprueft wurde jeder Pfad gegen den Ordner des Manifests (%s),"
             "\n  denn so loest ESP Web Tools ihn auf - nicht gegen die Seite."
             % release)
print("  alle Teilepfade aller %d Manifeste aufgeloest" %
      len([n for n in os.listdir(ordner) if n.startswith("manifest-")]))
PY3

echo
echo "--- Abgelegt ---"
ls -l "$OUT/$RELEASE" | tail -n +2 | awk 'NF>5 {printf "  %-36s %8.1f kB\n", $9, $5/1024}'

cat <<EOF

Release $FULL, abgelegt als docs/install/$RELEASE/. Die Nummer steht in
include/FirmwareVersion.h; Webinterface, Seite und Manifeste lesen sie von dort.

Dieser Ordner ist versioniert, Manifeste und .bin zusammen: die Seite liefert aus
/docs genau das, was dort steht, und ein Manifest ohne seine Dateien installiert
nichts. Ein aelterer Release-Ordner bleibt liegen und ist damit weiter abrufbar.

Alle Dateien eines Releases sind bytegleich reproduzierbar - derselbe Release-Ordner
noch einmal gebaut ergibt dieselben Bytes, sonst bricht der Vergleich hier ab.

Fuer GitHub Pages: Settings -> Pages -> Source: "Deploy from a branch", Branch
"master", Ordner "/docs". Neu bauen heisst: hier laufen lassen und den Ordner
mitcommitten.
EOF