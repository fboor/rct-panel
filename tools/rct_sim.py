#!/usr/bin/env python3
"""
Minimal RCT Power TCP simulator for GUI development.

Serves the same object IDs the rct_host firmware polls (see
src/rct/RctClient.cpp rctOids[]) from a table of values that can drift over
time, so the panel's UI (grid/load/PV/battery, graph, totals) can be
developed and verified without touching the real inverter.

Differences from rctclient's stock simulator:
  * Tolerates the 3-byte extension frame (0x2b 0x3c 0xe1) the official app
    and this firmware send first - the stock ReceiveFrame raises
    InvalidCommand on command 0x3c and the connection thread dies.
  * Values are dynamic: battery SOC slowly charges, PV ramps like a day,
    household load wanders. All parked near the values measured on the real
    device so the GUI looks realistic.

Usage:  python3 tools/rct_sim.py [--host 0.0.0.0] [--port 8899]
Then point the panel's RCT host at this machine (http://<panel-ip> WiFi
config or NVS: host = <dev-ip>, port = 8899).
"""

import argparse
import logging
import math
import select
import socket
import threading
import time

from rctclient.exceptions import FrameCRCMismatch, InvalidCommand
from rctclient.frame import ReceiveFrame, SendFrame
from rctclient.registry import REGISTRY as R
from rctclient.types import Command
from rctclient.utils import encode_value

log = logging.getLogger("rct_sim")


# --- value table: OID -> (object name, python value) -----------------------
# Mirrors src/rct/RctClient.cpp slot list. battery.soc / battery.soh are
# 0..1 fractions on the wire (this cost us a wrong "1 %" display already).
def build_values():
    now = int(time.time())
    v = {
        # Grid exchange (sum low-pass) + a coherent per-phase split. The panel
        # shows g_sync.p_ac_grid_sum_lp on the overview; p_ac_sc per phase on
        # the Netz detail page. _drift() recomputes all four every 2 s.
        "g_sync.p_ac_grid_sum_lp": 164.0,  # + = Bezug (import from grid)
        "g_sync.p_ac_sc[0]": 57.4,         # grid L1 (W)
        "g_sync.p_ac_sc[1]": 73.8,         # grid L2 (W)
        "g_sync.p_ac_sc[2]": 32.8,         # grid L3 (W)
        "energy.e_grid_feed_total": 3_124_000.0,
        "energy.e_grid_load_total": 8_455_000.0,
        "rb485.u_l_grid[0]": 231.9,
        "rb485.u_l_grid[1]": 230.4,
        "rb485.u_l_grid[2]": 232.6,
        "rb485.f_grid[0]": 50.01,
        "rb485.f_grid[1]": 49.99,
        "rb485.f_grid[2]": 50.02,
        "g_sync.p_ac_load[0]": 162.0,
        "g_sync.p_ac_load[1]": 431.0,
        "g_sync.p_ac_load[2]": 61.0,
        "dc_conv.dc_conv_struct[0].p_dc_lp": 640.0,  # PV generator A
        "dc_conv.dc_conv_struct[1].p_dc_lp": 0.0,    # PV generator B
        "io_board.s0_external_power": 0.0,
        "battery.soc": 0.6886,             # 68.86 %
        "battery.current": 1.71,
        "battery.voltage": 393.12,
        "g_sync.p_acc_lp": 638.8,          # + = charging
        # battery.bat_status: bit 3 = charging, bit 10 = discharging
        #   (docs: bits 3+10 clear => calibration, bit 11 clear => balancing)
        "battery.bat_status": 0,
        # fault[0..3].flt: 128 fault bits, cleared by default; --faults sets
        # them (comma-separated hex, see rctclient "Faults").
        "fault[0].flt": 0,
        "fault[1].flt": 0,
        "fault[2].flt": 0,
        "fault[3].flt": 0,
        "energy.e_dc_day[0]": 3_180.0,
        "energy.e_dc_day[1]": 420.0,
        "energy.e_load_day": 8_940.0,
        "energy.e_grid_feed_day": 2_140.0,
        "energy.e_grid_load_day": 3_010.0,
        "android_description": "PS 10.0 32WB",
        "svnversion": "2.3.5689",
        "db.core_temp": 31.2,
        "battery.temperature": 21.7,
        "db.temp1": 24.8,
        "power_mng.bat_next_calib_date": 1_794_577_820,
        "battery.cycles": 398,
        "battery.soh": 1.0,                # 100 %
        "prim_sm.island_flag": 1,
    }
    out = {}
    for name, val in v.items():
        oi = R.get_by_name(name)
        out[oi.object_id] = (name, val)
    return out


VALUES = build_values()
STATE_LOCK = threading.RLock()  # RLock: _drift() -> set_value() may re-enter
START = time.time()


def get_value(oid):
    with STATE_LOCK:
        return VALUES[oid][1]


def set_value(name, val):
    oi = R.get_by_name(name)
    with STATE_LOCK:
        VALUES[oi.object_id] = (name, val)


def _drift():
    """Slow, believable changes so the 1 Hz UI and the 5 min graph move."""
    t = time.time() - START
    # A soft 40 minute day/night for PV, plus cloud wiggles.
    pv = max(0.0, 1400.0 * math.sin(math.pi * (t % 2400.0) / 2400.0) ** 6)
    pv += 90.0 * math.sin(t / 17.0)
    pv = max(0.0, pv)
    set_value("dc_conv.dc_conv_struct[0].p_dc_lp", pv)

    load = 654.0 + 180.0 * math.sin(t / 60.0) + 40.0 * math.sin(t / 7.0)
    set_value("g_sync.p_ac_load[0]", max(40.0, load * 0.28))
    set_value("g_sync.p_ac_load[1]", max(40.0, load * 0.58))
    set_value("g_sync.p_ac_load[2]", max(20.0, load * 0.14))

    # Battery charges from the PV surplus, discharges at night.
    soc = 0.6886 + 0.020 * math.sin(t / 1500.0)
    set_value("battery.soc", max(0.05, min(0.99, soc)))
    bat_p = 0.75 * (pv - load) + 90.0 * math.sin(t / 23.0)
    set_value("g_sync.p_acc_lp", max(-2000.0, min(2000.0, bat_p)))
    set_value("battery.current", bat_p / 393.12)
    # Grid exchange = the residual of the balance (house load - PV - battery),
    # with the battery absorbing 75 % of the surplus/deficit. Positive = Bezug
    # (import), negative = Einspeisung (export). p_ac_sc is the same total
    # split across phases so the overview and the Netz detail page agree.
    grid = load - pv + bat_p
    set_value("g_sync.p_ac_grid_sum_lp", grid)
    set_value("g_sync.p_ac_sc[0]", grid * 0.35)
    set_value("g_sync.p_ac_sc[1]", grid * 0.45)
    set_value("g_sync.p_ac_sc[2]", grid * 0.20)
    # Drive the battery status bitfield along with the power sign.
    if bat_p > 100.0:
        set_value("battery.bat_status", 1 << 3)   # charging
    elif bat_p < -100.0:
        set_value("battery.bat_status", 1 << 10)  # discharging
    else:
        set_value("battery.bat_status", 0)        # standby

    set_value("energy.e_load_day", 8_940.0 + t * 1.4)
    set_value("energy.e_dc_day[0]", 3_180.0 + max(0.0, pv) * t / 3600.0)


def _drift_thread():
    while True:
        time.sleep(2.0)
        try:
            with STATE_LOCK:
                _drift()
        except Exception as exc:  # pylint: disable=broad-except
            log.warning("drift failed: %s", exc)


def handle_connection(conn, addr):
    log.info("client connected: %s", addr)
    frame = ReceiveFrame(ignore_crc_mismatch=True)
    try:
        while True:
            ready, _, _ = select.select([conn], [], [], 1.0)
            if not ready:
                continue
            buf = conn.recv(4096)
            if not buf:
                break
            log.debug("recv %d bytes: %s", len(buf), buf.hex())
            consumed = 0
            while consumed < len(buf):
                chunk = buf[consumed:]
                try:
                    n = frame.consume(chunk)
                except InvalidCommand as exc:
                    # 0x2b 0x3c 0xe1 extension frame (and any other garbage):
                    # drop the offending bytes, the next 0x2b resyncs.
                    n = exc.consumed_bytes
                    frame = ReceiveFrame(ignore_crc_mismatch=True)
                    log.debug("discarded %d bytes (extension/garbage)", n)
                except FrameCRCMismatch as exc:
                    n = exc.consumed_bytes
                    frame = ReceiveFrame(ignore_crc_mismatch=True)
                    log.warning("crc mismatch, resynced after %d bytes", n)
                except Exception:  # pylint: disable=broad-except
                    log.exception("consume/parse failed")
                    break
                consumed += n
                log.debug("consumed=%d complete=%s id=%s", consumed,
                          frame.complete(),
                          hex(frame.id) if frame.command.name != "_NONE" else None)
                if frame.complete():
                    try:
                        respond(conn, frame)
                    except Exception:  # pylint: disable=broad-except
                        log.exception("respond failed")
                    frame = ReceiveFrame(ignore_crc_mismatch=True)
    except OSError as exc:
        log.debug("connection error: %s", exc)
    finally:
        conn.close()
        log.info("client disconnected: %s", addr)


def respond(conn, frame):
    try:
        oi = R.get_by_id(frame.id)
    except KeyError:
        log.warning("read of unknown id 0x%08X ignored", frame.id)
        return
    if frame.command != Command.READ:
        return
    dt = oi.response_data_type
    value = get_value(frame.id)
    payload = encode_value(dt, value)
    sframe = SendFrame(command=Command.RESPONSE, id=frame.id,
                       address=frame.address, payload=payload)
    conn.send(sframe.data)
    log.info("READ 0x%08X %-34s %s", frame.id, oi.name, value)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8899)
    ap.add_argument("--verbose", "-v", action="store_true")
    ap.add_argument(
        "--faults",
        default=None,
        help="comma-separated fault[0..3].flt hex values to simulate, "
        "e.g. --faults 0x00000040,0,0,0 (bit 6 = Uzk+ over limit)",
    )
    args = ap.parse_args()

    if args.faults:
        parts = [int(x, 16) for x in args.faults.split(",")]
        for i, val in enumerate(parts[:4]):
            set_value(f"fault[{i}].flt", val)
            log.info("fault[%d].flt = 0x%08X", i, val)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(message)s",
    )

    threading.Thread(target=_drift_thread, daemon=True).start()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(5)
    log.info("RCT simulator on %s:%d - %d objects", args.host, args.port,
             len(VALUES))
    try:
        while True:
            conn, addr = srv.accept()
            threading.Thread(target=handle_connection, args=(conn, addr),
                             daemon=True).start()
    except KeyboardInterrupt:
        log.info("shutting down")
    finally:
        srv.close()


if __name__ == "__main__":
    main()