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
# The application image goes to the offset the partition table asks for - 0x10000
# here, because partitions/16mb_app.csv puts the first app slot there - and not
# to an address invented by this script. The bootloader and the partition table
# are left alone; the running bootloader is what hands over to the new app.
#
# The current app is read back into BACKUP first. Not because a botched flash is
# expected, but because "the panel is bricked" is a much worse sentence than
# "put this file back", and the read costs fifteen seconds.
#
# SPDX-License-Identifier: MIT
set -e

PORT=${PORT:-/dev/ttyUSB0}
ENV=${ENV:-esp32-s3}
OFFSET=${OFFSET:-0x10000}
BACKUP=${BACKUP:-/tmp/rct-panel-backup.bin}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/.pio/build/$ENV/firmware.bin"
ELF="$ROOT/.pio/build/$ENV/firmware.elf"

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

echo "=== 1/3 Chip lesen ==="
"$ESP" --chip "$CHIP" --port "$PORT" chip_id
echo

echo "=== 2/3 Aktuellen Stand sichern ==="
if [ -f "$BACKUP" ]; then
  echo "Es gibt schon eine Sicherung: $BACKUP ($(wc -c < "$BACKUP") bytes)"
  echo "Unverueberschreiben mit BACKUP=pfad oder BACKUP=leer"
  exit 1
fi
# read_flash braucht die Groesse; die des Images ist die des alten Standes
# ebenfalls, solange niemand etwas anderes gebaut hat.
"$ESP" --chip "$CHIP" --port "$PORT" read_flash "$OFFSET" "$BACKUP" \
        $(wc -c < "$BIN")
echo "Sicherung: $BACKUP"
echo

echo "=== 3/3 Schreiben ==="
"$ESP" --chip "$CHIP" --port "$PORT" --baud 921600 \
        write_flash "$OFFSET" "$BIN" "$ELF"
echo

echo "--- Boot verfolgen (20 s) ---"
timeout 20 python3 "$ROOT/tools/watch_boot.py" "$PORT" 115200 || true