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
# The application image goes to the offset the running app sits at, not to the
# first slot the partition table names: this panel boots 0x710000, so 0x10000 -
# which is where app0 starts in partitions/16mb_app.csv - takes the image,
# verifies byte for byte, and changes nothing that anyone sees. Its otadata is
# written (measured, not inferred: two sectors with sequence numbers), so the
# bootloader follows it instead of falling back to the factory slot. An earlier
# version of this script flashed app0 and reported success through three updates
# while the panel kept running what app1 held. An image with a build marker
# printed in the web interface (RCT_BUILD_SHA) is what turned that into a reading
# rather than an argument.
#
# The bootloader and the partition table are left alone; the running bootloader is
# what hands over to the new app.
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
# 0x710000 ist app1 aus partitions/16mb_app.csv und der Slot, den dieses Panel
# bootet. 0x10000 (app0) nimmt das Image zwar an und veraendert nichts - siehe
# den Kopf dieser Datei.
OFFSET=${OFFSET:-0x710000}
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