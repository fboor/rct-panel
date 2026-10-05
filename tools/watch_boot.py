#!/usr/bin/env python3
# Read a serial line and print what arrives, read-only.
#
# Used to follow a panel boot over USB and to see what a running panel says when
# something is wrong with it. The web interface carries the values; the serial
# line carries the reasons, because that is where the driver puts them.
#
# Nothing is written. DTR and RTS are cleared after opening, so opening the
# port does not reset an ESP32 that is wired to it with the usual auto-reset
# circuit - a reset in the middle of reading a boot log loses the first lines,
# which are the ones that say what went wrong.
#
# SPDX-License-Identifier: MIT
import fcntl
import os
import select
import struct
import sys
import termios
import time

DEFAULT_PORT = "/dev/ttyUSB0"
DEFAULT_BAUD = 115200
DEFAULT_SECONDS = 15.0


def open_readonly(port, baud):
    """Open the port for reading, with both handshake lines released."""
    fd = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    _iflag, oflag, cflag, lflag, ispeed, ospeed, cc = attrs
    speed = getattr(termios, "B%d" % baud)
    iflag = termios.IGNPAR
    oflag = 0
    cflag |= termios.CLOCAL | termios.CREAD | speed
    cflag &= ~(termios.CSIZE | termios.PARENB)
    cflag |= termios.CS8
    lflag = 0
    cc = list(cc)
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW,
                       [iflag, oflag, cflag, lflag, speed, speed, cc])
    bits = struct.unpack("i", fcntl.ioctl(fd, termios.TIOCMGET, b"\0\0\0\0"))[0]
    fcntl.ioctl(fd, termios.TIOCMBIC,
                struct.pack("i", bits & ~(termios.TIOCM_DTR | termios.TIOCM_RTS)))
    return fd


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PORT
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_BAUD
    seconds = float(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_SECONDS

    try:
        fd = open_readonly(port, baud)
    except OSError as exc:
        print("Port %s nicht lesbar: %s" % (port, exc))
        return 1
    try:
        sys.stdout.write("--- %s @ %d baud, %.0f s, nur lesend ---\n"
                         % (port, baud, seconds))
        sys.stdout.flush()
        buf = bytearray()
        end = time.time() + seconds
        while time.time() < end:
            ready, _, _ = select.select([fd], [], [], 0.2)
            if fd not in ready:
                continue
            try:
                chunk = os.read(fd, 4096)
            except BlockingIOError:
                continue
            if chunk:
                buf += chunk
                sys.stdout.write(chunk.decode("utf-8", "replace"))
                sys.stdout.flush()
        if not buf:
            print("(nichts empfangen)")
            print("Ein Panel, das im Betrieb nichts spricht, ist kein Fehler: "
                  "es redet beim Start und bei Aktionen.")
    finally:
        os.close(fd)
    return 0


if __name__ == "__main__":
    sys.exit(main())