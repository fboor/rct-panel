#!/bin/sh
# Flash the panel over its USB serial line, and watch it boot.
#
# The panel is reachable over the network and the web interface has an update
# page, but the maintenance code for that page is drawn on the panel's own
# screen and only ever logged to the serial line - so somebody has to stand in
# front of the panel and read four digits off a wall display before an update
# can start. The serial line is already open to this machine and needs none of
# that, so the update goes the way the hardware allows.
#
# The USB-serial adapter sits on the panel's UART with the usual auto-reset
# circuit: pulling the handshake lines resets the chip, and the chip answers with
# its boot log on the same line. esptool.py does both, which is why this is a few
# lines and not a protocol.
#
# The application image goes to app0 at 0x10000, and the otadata is erased first.
#
# Which of the two app slots boots is not a fixed fact, it is a consequence of how the
# firmware was last installed, and that is the whole reason this script once flashed a
# panel three times without changing anything it could see:
#
#   otadata valid   -> the bootloader follows it, that slot boots. The panel's own OTA
#                      writes the second slot and selects it, so an OTA-ed panel runs
#                      app1 at 0x710000.
#   otadata erased  -> the bootloader falls back to the first app slot. The browser
#                      installer writes app0 at 0x10000 and leaves no valid selection,
#                      so an installed panel runs app0.
#
# Both are measured on this panel, not inferred (2026-10-11: the release image in app0
# ran after an install through the website; before that, with a valid selection, the
# panel ran app1 while three writes to 0x10000 had verified byte for byte).
#
# So this script does what both other ways do and makes no assumption: erase the
# selection, write app0, and the panel runs what was just written. A panel that had
# been OTA-ed comes back to the factory slot on the next USB update - which is the
# price of not having to ask anybody where their panel is booting from.
#
# The bootloader and the partition table are left alone, on purpose: this is an update
# of a panel that is already running, and its bootloader is what hands over to the new
# app. A panel whose partition table still comes from a release before the first one
# (app0, otadata, the two app slots) is not a panel to update this way - that is what
# the browser installer is for, and it writes all three parts.
#
# No backup of the running firmware, because there is nothing to fall back to that
# is not in the tree anyway: every image here is built from it, and a panel that
# does not come up is flashed again rather than restored. BACKUP=pfad reads the
# slot out first, for the one case where that is false - an image that came from
# somewhere else and cannot be rebuilt.
#
# SPDX-License-Identifier: MIT
set -e

PORT=${PORT:-/dev/ttyUSB0}
ENV=${ENV:-esp32-s3}
# app0, und das otadata wird vorher geloescht - siehe den Kopf dieser Datei. Das ist
# derselbe Weg, den der Installer nimmt, und derselbe, den die eigene OTA nimmt, wenn
# es app1 besetzt: danach bootet der Bootloader die Adresse, in die gerade geschrieben
# wurde.
OFFSET=${OFFSET:-0x10000}
OTADATA=${OTADATA:-0xE000}
OTADATA_LEN=${OTADATA_LEN:-0x2000}
# Eine Taktstufe fuer beide Richtungen, und zwar die schnelle: das Lesen lief mit der
# esptool-Vorgabe 115200 und brauchte fuer die 16 MB einer vollen Sicherung 148
# Sekunden (88,8 kbit/s). Dieselbe Leitung schafft im Lesen 598,4 kbit/s, also 22
# Sekunden fuer den App-Slot.
BAUD=${BAUD:-921600}
# Leer heisst: keine Sicherung. Gesetzt ist es ein Ziel, in das gelesen wird.
BACKUP=${BACKUP:-}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/.pio/build/$ENV/firmware.bin"

ESP=${ESP:-$HOME/.platformio/packages/tool-esptoolpy/esptool.py}
if [ ! -f "$ESP" ]; then
  ESP=$(command -v esptool.py 2>/dev/null || true)
fi
if [ -z "$ESP" ] || [ ! -f "$ESP" ]; then
  echo "esptool.py nicht gefunden - Pfad mit ESP= uebergeben."
  exit 1
fi

if [ ! -f "$BIN" ]; then
  echo "Kein Build gefunden: $BIN"
  echo "Zuerst bauen: pio run -e $ENV"
  exit 1
fi

# ESP32-S3 for /dev/ttyUSB*, plain ESP32 for the ACM ports of older boards.
case "$PORT" in
  /dev/ttyUSB*) CHIP=${CHIP:-esp32s3} ;;
  *) CHIP=${CHIP:-esp32} ;;
esac

echo "Port        $PORT"
echo "Chip        $CHIP"
echo "Image       $BIN ($(wc -c < "$BIN") bytes)"
echo "Offset      $OFFSET"
echo

echo "=== Boot-Auswahl zuruecksetzen ==="
# 8 KB loeschen: danach bootet der Bootloader den ersten App-Slot, in den
# geschrieben wird - siehe den Kopf dieser Datei.
"$ESP" --chip "$CHIP" --port "$PORT" --baud "$BAUD" erase_region "$OTADATA" "$OTADATA_LEN"
echo

echo "=== Chip lesen ==="
"$ESP" --chip "$CHIP" --port "$PORT" chip_id
echo

# read_flash braucht die Groesse; die des Images ist die des alten Standes
# ebenfalls, solange niemand etwas anderes gebaut hat.
#
# Reihenfolge: Adresse, Groesse, Datei. Sie stand hier zwei Argumente andersherum,
# was esptool 2.x mitgeschluckt hat und 4.x mit einem argparse-Fehler abweist - und
# weil `set -e` gesetzt ist, brach der Flash danach ab. Die Sicherung war also nie
# kaputt, sie hat nur nie jemanden erreicht, seit esptool 4 auf dieser Maschine liegt.
if [ -n "$BACKUP" ]; then
  if [ -f "$BACKUP" ]; then
    echo "Es gibt schon eine Sicherung: $BACKUP ($(wc -c < "$BACKUP") bytes)"
    echo "Anderen Zielpfad mit BACKUP=pfad setzen"
    exit 1
  fi
  echo "=== Aktuellen Stand sichern ==="
  "$ESP" --chip "$CHIP" --port "$PORT" --baud "$BAUD" \
          read_flash "$OFFSET" $(wc -c < "$BIN") "$BACKUP"
  echo "Sicherung: $BACKUP"
  echo
fi

echo "=== Schreiben ==="
# --verify, and no ELF. esptool 4.x reads every second argument as an address, so
# handing it the .elf after the .bin aborted the write with "Address ... must be a
# number" - the flash never happened, and `set -e` turned that into a silent no-op
# rather than a failed flash. The ELF was never a thing write_flash accepts; what it
# was reaching for is --verify, which actually checks what landed.
"$ESP" --chip "$CHIP" --port "$PORT" --baud "$BAUD" \
        write_flash --verify "$OFFSET" "$BIN"
echo

echo "--- Boot verfolgen (20 s) ---"
timeout 20 python3 "$ROOT/tools/watch_boot.py" "$PORT" 115200 || true