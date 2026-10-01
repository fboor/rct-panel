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
       [--quiet-after SECONDS]
Then point the panel's RCT host at this machine (http://<panel-ip> WiFi
config or NVS: host = <dev-ip>, port = 8899).

Quiet mode (--quiet-after, or SIGUSR1 to toggle at any time) keeps the
connection open and stops answering. That combination is the one the panel
cannot get otherwise: a device that is silent but reachable makes it hold the
values it has and say so, after 60 s. Killing the simulator instead drops the
TCP link, and the panel then reports a lost connection - a different state.
"""

import argparse
import logging
import math
import select
import signal
import socket
import threading
import time

from rctclient.exceptions import FrameCRCMismatch, InvalidCommand
from rctclient.frame import ReceiveFrame, SendFrame
from rctclient.registry import REGISTRY as R
from rctclient.types import Command
from rctclient.utils import encode_value

log = logging.getLogger("rct_sim")


# --- accumulated energies (Wh), one row per period shown on the Energie page
# The four drivers per period are PV (split over the two generators), feed-in
# and grid draw. The household counter (e_load_*) is deliberately NOT one of
# them - it is derived from the balance:
#
#     Verbrauch = (PV - Einspeisung) + Bezug
#
# Feed-in is the PV surplus, it is produced but not consumed, so it must never
# end up inside the Verbrauch figure. Deriving the load counter instead of
# integrating it makes that identity true by construction for every period, and
# the five bars on the page can no longer contradict each other.
#
# The seeds are what a real system would already have banked when the sim
# starts; _drift() integrates the instantaneous balance on top.
_ENERGY = {
    #             PV A      PV B     Einspeisung   Bezug
    "day":   (3_180.0,     420.0,   2_140.0,    3_010.0),
    "month": (31_800.0,  2_100.0,  18_400.0,   88_200.0),
    "year":  (662_000.0, 44_000.0, 402_000.0,  561_000.0),
    "total": (4_180_000.0, 268_000.0, 3_124_000.0, 8_455_000.0),
}

# period -> (PV gen A OID, PV gen B OID, load OID, feed-in OID, grid draw OID)
_ENERGY_OIDS = {
    "day": ("energy.e_dc_day[0]", "energy.e_dc_day[1]", "energy.e_load_day",
            "energy.e_grid_feed_day", "energy.e_grid_load_day"),
    "month": ("energy.e_dc_month[0]", "energy.e_dc_month[1]",
              "energy.e_load_month", "energy.e_grid_feed_month",
              "energy.e_grid_load_month"),
    "year": ("energy.e_dc_year[0]", "energy.e_dc_year[1]", "energy.e_load_year",
             "energy.e_grid_feed_year", "energy.e_grid_load_year"),
    "total": ("energy.e_dc_total[0]", "energy.e_dc_total[1]",
              "energy.e_load_total", "energy.e_grid_feed_total",
              "energy.e_grid_load_total"),
}


# Grid outage cycle for prim_sm.island_flag (see _drift). Short first outage so
# the overview warning icon can be checked without waiting long, then one every
# 5 min - short enough to catch on the screen, rare enough to stay believable.
_ISLAND_FIRST = 40.0    # s until the first outage starts
_ISLAND_PERIOD = 300.0  # s between outages
_ISLAND_ON = 90.0       # s per outage

# Household base load in W. Only ~1/4 of the load/pv mismatch reaches the grid
# meter (see _drift), so the default only produces about 270 W of grid draw -
# below the relay's 500 W default threshold. --lastung raises the household
# when the Netzbezug function is to be tested.
_LOAD_BASE = 654.0


def _energy_rows():
    """[(oid name, value)] for every energy counter, derived from _ENERGY."""
    rows = []
    for period, (pv_a, pv_b, feed, grid) in _ENERGY.items():
        o_a, o_b, o_load, o_feed, o_grid = _ENERGY_OIDS[period]
        load = (pv_a + pv_b) - feed + grid
        rows.append((o_a, pv_a))
        rows.append((o_b, pv_b))
        rows.append((o_feed, feed))
        rows.append((o_grid, grid))
        rows.append((o_load, load))
    return rows


# The device also keeps counters for an external generator (the S0 meter). Both
# the plain and the _sum variant of every period exist, and the firmware polls
# six of the eight (e_ext_day_sum, e_ext_month_sum, e_ext_year_sum,
# e_ext_total_sum, e_ext_day, e_ext_month - see rctOids[] in RctClient.cpp).
# All eight are answered so a variant cannot be missed later: the cost of a
# wrong id here is a silently missing row in the CSV, not a visible error.
_EXT_NAMES = ("energy.e_ext_day", "energy.e_ext_day_sum",
              "energy.e_ext_month", "energy.e_ext_month_sum",
              "energy.e_ext_year", "energy.e_ext_year_sum",
              "energy.e_ext_total", "energy.e_ext_total_sum")


def _ext_rows():
    """[(oid name, value)] for the external-generator counters (all 0, see the
    io_board.s0_external_power comment in build_values)."""
    return [(name, 0.0) for name in _EXT_NAMES]


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
        # S0 (the house's own grid meter) stays 0 in the sim: any non-zero value
        # would have to be woven into _drift's balance identity to keep "PV
        # gesamt = A + B + S0" and "Hausverbrauch = Last + S0" true, and a
        # constant that only pretends to be an external generator teaches the
        # wrong thing. The energy counters the device keeps for it follow: they
        # are answered (the firmware asks for all six, see _ext_rows), because
        # an unanswered read shows up on the panel as a gap in the S0 bar.
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
    # Energy counters come from _ENERGY so the seeds and the drift share one
    # source of truth (and the household counter stays derived).
    for name, val in _energy_rows():
        oi = R.get_by_name(name)
        out[oi.object_id] = (name, val)
    for name, val in _ext_rows():
        oi = R.get_by_name(name)
        out[oi.object_id] = (name, val)
    return out


VALUES = build_values()
_NO_VALUE_WARNED = set()       # ids already complained about, see respond()
STATE_LOCK = threading.RLock()  # RLock: _drift() -> set_value() may re-enter
START = time.time()
_LAST_DRIFT = START  # dt base for the energy integration in _drift()


def get_value(oid):
    with STATE_LOCK:
        return VALUES[oid][1]


def set_value(name, val):
    oi = R.get_by_name(name)
    with STATE_LOCK:
        VALUES[oi.object_id] = (name, val)


def _drift():
    """Slow, believable changes so the 1 Hz UI and the 5 min graph move."""
    global _LAST_DRIFT
    now = time.time()
    t = now - START
    dt = now - _LAST_DRIFT  # seconds since the last drift tick
    _LAST_DRIFT = now
    # A soft 40 minute day/night for PV, plus cloud wiggles.
    pv = max(0.0, 1400.0 * math.sin(math.pi * (t % 2400.0) / 2400.0) ** 6)
    pv += 90.0 * math.sin(t / 17.0)
    pv = max(0.0, pv)
    set_value("dc_conv.dc_conv_struct[0].p_dc_lp", pv)

    load = _LOAD_BASE + 180.0 * math.sin(t / 60.0) + 40.0 * math.sin(t / 7.0)
    set_value("g_sync.p_ac_load[0]", max(40.0, load * 0.28))
    set_value("g_sync.p_ac_load[1]", max(40.0, load * 0.58))
    set_value("g_sync.p_ac_load[2]", max(20.0, load * 0.14))

    # Battery charges from the PV surplus, discharges at night.
    soc = 0.6886 + 0.020 * math.sin(t / 1500.0)
    set_value("battery.soc", max(0.05, min(0.99, soc)))

    # Grid outage ("Inselbetrieb"): the inverter is cut off from the grid, so
    # the grid meters read zero and PV plus battery have to cover the house on
    # their own. Cycles every _ISLAND_PERIOD s for _ISLAND_ON s, starting
    # _ISLAND_FIRST s in, so the warning icon shows up within a minute of
    # connecting.
    phase = (t - _ISLAND_FIRST) % _ISLAND_PERIOD
    island = phase < _ISLAND_ON
    set_value("prim_sm.island_flag", 1 if island else 0)

    if island:
        # No grid at all: the battery covers whatever PV does not. Same sign
        # convention as above (negative = discharging), so the balance
        # grid = load - pv + bat_p stays at exactly 0.
        bat_p = pv - load
        grid = 0.0
    else:
        bat_p = 0.75 * (pv - load) + 90.0 * math.sin(t / 23.0)
        # Grid exchange = the residual of the balance (house load - PV -
        # battery), with the battery absorbing 75 % of the surplus/deficit.
        # Positive = Bezug (import), negative = Einspeisung (export).
        grid = load - pv + bat_p
    set_value("g_sync.p_acc_lp", max(-2000.0, min(2000.0, bat_p)))
    set_value("battery.current", bat_p / 393.12)
    set_value("g_sync.p_ac_grid_sum_lp", grid)
    # p_ac_sc is the same total split across phases so the overview and the
    # Netz detail page agree.
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

    # Bank the last dt of the instantaneous balance into all four periods.
    # grid > 0 is Bezug (import), grid < 0 is Einspeisung (export), and only
    # the sign decides which counter moves - that is what makes Einspeisung the
    # surplus. The household counter is not touched here; _energy_rows()
    # derives it from the balance.
    dt_h = dt / 3600.0
    pv_gain = max(0.0, pv) * dt_h
    grid_gain = grid * dt_h
    for period, (pv_a, pv_b, feed, grid_draw) in list(_ENERGY.items()):
        _ENERGY[period] = (pv_a + pv_gain, pv_b,
                           feed + max(0.0, -grid_gain),
                           grid_draw + max(0.0, grid_gain))
    for name, val in _energy_rows():
        set_value(name, val)



def _drift_thread():
    while True:
        time.sleep(2.0)
        try:
            with STATE_LOCK:
                _drift()
        except Exception as exc:  # pylint: disable=broad-except
            log.warning("drift failed: %s", exc)


# Quiet: the socket stays open, nothing is answered. A plain global, because a
# signal handler must not take a lock - Event.set() would not be safe here.
_QUIET = False


def _set_quiet(on, why):
    global _QUIET  # pylint: disable=global-statement
    _QUIET = on
    log.info("quiet %s (%s)", "on" if on else "off", why)


def _toggle_quiet(_signum, _frame):
    _set_quiet(not _QUIET, "SIGUSR1")


def _quiet_after(seconds):
    time.sleep(seconds)
    _set_quiet(True, f"--quiet-after {seconds:g} s")


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
                    if _QUIET:
                        # Read and discarded, no answer, socket untouched: the
                        # panel keeps its link and stops getting new values.
                        log.debug("quiet: request dropped, link stays open")
                    else:
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
    if frame.id not in VALUES:
        # Known to the registry, but the sim has no value for it. Say so once
        # per id instead of raising: raising here kills the answer for every
        # later read in this burst (the firmware asks for ~55 values back to
        # back), and the missing value should show up as one clear complaint
        # rather than a traceback that hides which ids are missing.
        if frame.id not in _NO_VALUE_WARNED:
            _NO_VALUE_WARNED.add(frame.id)
            log.error("read of 0x%08X (%s): no value in the sim's table",
                      frame.id, oi.name)
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
    ap.add_argument(
        "--lastung",
        type=float,
        default=1.0,
        help="household base load as a multiple of 654 W (default 1.0). "
        "Only about a quarter of the load/pv mismatch reaches the grid "
        "meter, so --lastung 4 is what makes the grid draw exceed the "
        "relay's 500 W default threshold.",
    )
    ap.add_argument(
        "--quiet-after",
        type=float,
        default=None,
        metavar="SECONDS",
        help="stop answering after SECONDS while keeping every connection "
        "open, so the panel holds its last values and reports them as old. "
        "SIGUSR1 toggles the same thing while it runs.",
    )
    args = ap.parse_args()

    global _LOAD_BASE
    _LOAD_BASE = 654.0 * max(0.0, args.lastung)

    if args.faults:
        parts = [int(x, 16) for x in args.faults.split(",")]
        for i, val in enumerate(parts[:4]):
            set_value(f"fault[{i}].flt", val)
            log.info("fault[%d].flt = 0x%08X", i, val)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(message)s",
    )

    signal.signal(signal.SIGUSR1, _toggle_quiet)
    if args.quiet_after is not None:
        threading.Thread(target=_quiet_after, args=(args.quiet_after,),
                         daemon=True).start()

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