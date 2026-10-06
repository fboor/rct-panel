#!/bin/sh
# Stage the binaries that the browser installer flashes, into docs/install/.
#
# WHY FOUR PARTS AND NOT ONE
#
# This is a first-time installer, and that decides the answer. ESP Web Tools hands
# the parts to esptool.js as they are listed in the manifest, so a device that is
# completely blank can be brought up only if everything the bootloader needs
# arrives: the bootloader itself, the partition table and the otadata blob. A
# merged.bin would hold the same four, in one file - see the note at the end about
# why it is not used.
#
# tools/flash_usb.sh writes firmware.bin alone, and that is right there: it updates
# a panel that is already running, and the running bootloader is what hands over to
# the new app. On an empty device there is no bootloader to hand over, so the other
# three are not optional.
#
# WHY erase ON INSTALL
#
# What was on the device before is unknown, and a half-erased flash is worse than
# an empty one: a stale app image outside the partition table, an otadata blob
# pointing at a slot the new table does not have, or settings that only surface as
# a strange behaviour weeks later. The firmware has no Improv, so ESP Web Tools
# cannot recognise a panel that already runs this project and treats every install as
# a new one - which means it always erases. That is the intent, and it is also why
# this page must not be used to update a running panel: it would wipe the Wi-Fi
# credentials along with everything else. Updating a panel in place goes through its
# own update page, behind the maintenance code.
#
# SPDX-License-Identifier: MIT
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/docs/install"
PIO=${PIO:-pio}
# The one from PlatformIO, not the one on $PATH: esptool 2.8 on a desktop is
# older than the ESP32-S3 and has neither merge_bin nor that chip.
ESPROOT=${ESPROOT:-$HOME/.platformio/packages}
BOOT_APP0="$ESPROOT/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

# Two builds, because the panel's language is baked into the firmware - there is no
# switch on the device (src/i18n/Lang.h). The bootloader and the partition table are
# the same for both, so only the application is written twice.
ENVS="esp32-s3 esp32-s3-en"

if [ ! -f "$BOOT_APP0" ]; then
  echo "boot_app0.bin nicht gefunden: $BOOT_APP0"
  echo "ESPROOT auf den PlatformIO-Pfad zeigen lassen."
  exit 1
fi

mkdir -p "$OUT"

echo "=== 1/2 Firmware bauen ==="
for env in $ENVS; do
  echo "--- $env"
  "$PIO" run -e "$env"
done

echo
echo "=== 2/2 Teile nach docs/install/ ==="
# The bootloader and the partition table are identical across the two builds; the
# first one staged wins and the second is checked against it rather than silently
# overwriting, because a difference would mean the two builds disagree about the
# flash layout and only one of them could be right.
for part in bootloader.bin partitions.bin; do
  src="$ROOT/.pio/build/esp32-s3/$part"
  if [ -f "$OUT/$part" ] && ! cmp -s "$src" "$OUT/$part"; then
    echo "  $part weicht vom bereits abgelegten ab - die beiden Builds"
    echo "  benutzen verschiedene Flash-Layouts. Nur eines davon stimmt."
    exit 1
  fi
  cp "$src" "$OUT/$part"
  echo "  $part"
done

cp "$BOOT_APP0" "$OUT/boot_app0.bin"
echo "  boot_app0.bin (aus dem Framework, nicht aus dem Build)"

for env in $ENVS; do
  case "$env" in
    esp32-s3)    lang=de ;;
    esp32-s3-en) lang=en ;;
  esac
  cp "$ROOT/.pio/build/$env/firmware.bin" "$OUT/firmware-$lang.bin"
  echo "  firmware-$lang.bin"
done

echo
ls -l "$OUT" | tail -n +2 | awk '{printf "  %-22s %8.1f kB\n", $9, $5/1024}'

# The manifest carries no version of its own: it names the builds that exist, and
# the panel's own version string (kPanelVersion in src/web/WebServer.cpp) is what a
# running device reports. Two sources for one number is one too many.
cat <<'EOF'

Die Manifeste (manifest-de.json, manifest-en.json) und die Seite (index.html) sind
Text und liegen im Repository. Erzeugt wird nur, was der Build hergibt.

Wichtig, sonst haelt man die Seite fuer fertig: die vier .bin stehen in .gitignore
und sind NICHT im Repository. GitHub Pages liefert aus /docs genau das, was dort
liegt - ohne die Binaries zeigt die Seite zwar beide Knoepfe, installiert aber
nichts, und der Browser meldet einen Ladefehler statt einer Firmware.

Fuer eine scharfe Seite braucht es also zusaetzlich: die vier .bin auf denselben
Server legen. Zwei Wege dafuer - ein Workflow, der baut und ausliefert, oder ein
Upload von Hand. Beides ist nicht getroffen.

Sobald das erledigt ist: Settings -> Pages -> Source: "Deploy from a branch",
Branch "master", Ordner "/docs".
EOF