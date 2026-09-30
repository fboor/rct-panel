#!/usr/bin/env python3
"""
Host test for tools/rct_sim.py - the simulator is the tool the relay modes are
meant to be tested with later, so it has to be known-good before the panel ever
sees it.

Runs the simulator in-process, talks to it over TCP with the same framing the
firmware uses, and checks:

  1. every object id the firmware polls (src/rct/RctClient.cpp rctOids[]) is
     answered with the right type - a missing id in the table would look like a
     dead inverter on the panel, not like a broken simulator
  2. the numbers are self-consistent: load, PV, battery and grid obey the
     balance grid = load - pv + bat, per phase and in total
  3. the island cycle actually happens, and during an outage the grid meters
     read zero
  4. a PV surplus large enough to trip the relay's Ueberschuss mode occurs, and
     a grid draw large enough to trip Netzbezug occurs - the two modes that do
     not need a fault
  5. --faults reaches the fault words unchanged

Run:  python3 tools/rct_sim_test.py [-v]
"""

import argparse
import importlib.util
import os
import re
import socket
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from rctclient.frame import ReceiveFrame, SendFrame          # noqa: E402
from rctclient.registry import REGISTRY as R                 # noqa: E402
from rctclient.types import Command                          # noqa: E402
from rctclient.utils import encode_value, decode_value       # noqa: E402

# The object ids the firmware polls are listed in src/rct/RctClient.cpp as
# rctOids[]. The test reads that array instead of keeping a copy of it here: a
# copy would rot the first time someone adds a value to the firmware, and
# catching exactly that is the point of check 1.
FIRMWARE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src",
                        "rct", "RctClient.cpp")


def firmware_oids():
    """The object ids from rctOids[] in the firmware, in array order.

    Not all of them are written with eight digits - 0x3A39CA2 (load L1) and
    0xCB5D21B (PV generator B) among them - so the pattern takes 1..8 digits.
    Requiring eight silently dropped those, which then looked like ids the
    firmware does not poll at all."""
    with open(FIRMWARE, encoding="utf-8") as fh:
        src = fh.read()
    start = src.find("rctOids[RCT_NUM_SLOTS] = {")
    if start < 0:
        raise SystemExit("rctOids[] in RctClient.cpp nicht gefunden")
    body = src[start:src.find("};", start)]
    return [int(t, 16) for t in re.findall(r"0x[0-9A-Fa-f]{1,8}\b", body)]


def firmware_num_slots():
    """RCT_NUM_SLOTS from the slot enum, i.e. the array's declared length."""
    with open(FIRMWARE, encoding="utf-8") as fh:
        src = fh.read()
    m = re.search(r"^\s*RCT_NUM_SLOTS\s*$", src, re.M)
    if m is None:
        return None
    slots = re.findall(r"^\s*RCT_SLOT_[A-Z0-9_]+", src[:m.start()], re.M)
    return len(slots)


def firmware_names():
    """(oid, name) for every polled id, in firmware order, without duplicates.

    An id the registry does not know gets a placeholder name so it still shows
    up in the missing-value report instead of vanishing into a KeyError."""
    out = []
    for oid in firmware_oids():
        if oid in [o for o, _ in out]:
            continue
        try:
            out.append((oid, R.get_by_id(oid).name))
        except Exception:
            out.append((oid, f"0x{oid:08X}"))
    return out


_checks = 0
_failed = 0


def check(ok, what, detail=""):
    global _checks, _failed
    _checks += 1
    if not ok:
        _failed += 1
        print(f"FAIL  {what}{(' - ' + detail) if detail else ''}")


def load_sim(faults=None):
    """Import tools/rct_sim.py as a module without running its main()."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rct_sim.py")
    spec = importlib.util.spec_from_file_location("rct_sim_under_test", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    if faults:
        for i, val in enumerate(faults):
            mod.set_value(f"fault[{i}].flt", val)
    return mod


def read_all(sock, names, timeout=2.0):
    """READ every name once, return {name: decoded value}."""
    frame = ReceiveFrame(ignore_crc_mismatch=True)
    out = {}
    sock.settimeout(timeout)
    pending = 0
    # The extension frame the firmware sends first, exactly as the firmware
    # does - the simulator has to resync on it.
    sock.send(bytes([0x2B, 0x3C, 0xE1]))
    for name in names:
        oi = R.get_by_name(name)
        sf = SendFrame(command=Command.READ, id=oi.object_id)
        sock.send(sf.data)
        pending += 1
        while pending:
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                return out
            if not chunk:
                return out
            consumed = 0
            while consumed < len(chunk):
                try:
                    n = frame.consume(chunk[consumed:])
                except Exception:  # resync, as handle_connection() does
                    n = getattr(sys.exc_info()[1], "consumed_bytes", 1)
                    frame = ReceiveFrame(ignore_crc_mismatch=True)
                consumed += n
                if not frame.complete():
                    continue
                pending -= 1
                try:
                    oi_r = R.get_by_id(frame.id)
                    out[oi_r.name] = decode_value(oi_r.response_data_type,
                                                 frame.data)
                except KeyError:
                    pass
                frame = ReceiveFrame(ignore_crc_mismatch=True)
    return out


def near(a, b, tol):
    return abs(a - b) <= tol


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    sim = load_sim()
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(5)
    port = srv.getsockname()[1]

    def serve():
        while True:
            try:
                conn, _ = srv.accept()
            except OSError:
                return
            threading.Thread(target=sim.handle_connection, args=(conn, ("t", 0)),
                             daemon=True).start()

    threading.Thread(target=serve, daemon=True).start()
    threading.Thread(target=sim._drift_thread, daemon=True).start()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(("127.0.0.1", port))

    # --- 1. every id the firmware polls is served --------------------------
    polled = firmware_names()
    check(firmware_num_slots() == len(polled),
          "rctOids[] so lang wie das Slot-Enum (RCT_NUM_SLOTS)",
          f"{len(polled)} OIDs, Enum zählt {firmware_num_slots()}")
    got = read_all(sock, [n for _, n in polled])
    missing = [n for _, n in polled if n not in got]
    check(not missing, "alle von der Firmware abgefragten OIDs werden beantwortet",
          f"fehlend: {missing}")
    check(len(got) == len(polled), "Antwortzahl == Abfragezahl",
          f"{len(got)} von {len(polled)}")
    if args.verbose:
        print(f"      {len(polled)} OIDs aus rctOids[] abgefragt und beantwortet")

    # --- 2. types and plausibility ----------------------------------------
    check(0.0 <= got.get("battery.soc", -1) <= 1.0, "battery.soc ist 0..1",
          str(got.get("battery.soc")))
    check(got.get("android_description", "") == "PS 10.0 32WB",
          "android_description lesbar als Text", str(got.get("android_description")))
    check(got.get("prim_sm.island_flag") in (0, 1), "island_flag ist 0 oder 1",
          str(got.get("prim_sm.island_flag")))
    for i in range(3):
        check(got.get(f"g_sync.p_ac_load[{i}]", -1) >= 0,
              f"p_ac_load[{i}] nicht negativ",
              str(got.get(f"g_sync.p_ac_load[{i}]")))

    # The balance has to hold in every sample, otherwise the grid number on the
    # panel is decoration.
    worst = 0.0
    worst_grid = 0.0
    worst_island = 0.0
    for _ in range(25):
        time.sleep(0.2)
        sim._drift()
        s = read_all(sock, ["g_sync.p_ac_grid_sum_lp", "g_sync.p_ac_sc[0]",
                            "g_sync.p_ac_sc[1]", "g_sync.p_ac_sc[2]",
                            "g_sync.p_ac_load[0]", "g_sync.p_ac_load[1]",
                            "g_sync.p_ac_load[2]",
                            "dc_conv.dc_conv_struct[0].p_dc_lp",
                            "g_sync.p_acc_lp", "prim_sm.island_flag"])
        load = sum(s[f"g_sync.p_ac_load[{i}]"] for i in range(3))
        pv = s["dc_conv.dc_conv_struct[0].p_dc_lp"]
        bat = s["g_sync.p_acc_lp"]
        grid = s["g_sync.p_ac_grid_sum_lp"]
        ph = sum(s[f"g_sync.p_ac_sc[{i}]"] for i in range(3))
        if s["prim_sm.island_flag"]:
            worst_island = max(worst_island, abs(grid))
            continue
        worst = max(worst, abs((load - pv + bat) - grid))
        worst_grid = max(worst_grid, abs(ph - grid))
    check(worst < 25.0, "Bilanz load - pv + bat = grid", f"max. Abweichung {worst:.1f} W")
    check(worst_grid < 25.0, "Phasensumme = Gesamtwert Netz",
          f"max. Abweichung {worst_grid:.1f} W")

    # --- 3. the island cycle ----------------------------------------------
    # The outage starts _ISLAND_FIRST (40 s) after the sim started and lasts
    # _ISLAND_ON of every _ISLAND_PERIOD. Walk the module-global START over one
    # full period instead of waiting five minutes for it - _drift() reads t from
    # START and takes dt from the wall clock, so moving START only moves the
    # simulated phase, never the energy integration.
    island_oid = R.get_by_name("prim_sm.island_flag").object_id
    flags = set()
    sim.START = time.time() - sim._ISLAND_FIRST - 5.0
    steps = int((sim._ISLAND_PERIOD + sim._ISLAND_ON) / 5.0)
    for k in range(steps + 1):
        sim._drift()
        flags.add(int(sim.get_value(island_oid)))
        sim.START -= 5.0  # forward in simulated time
    check(1 in flags, "Inselbetrieb wird erreicht", f"Flags gesehen: {sorted(flags)}")
    check(0 in flags, "Inselbetrieb geht auch wieder vorbei",
          f"Flags gesehen: {sorted(flags)}")
    check(worst_island < 0.001, "Netzzähler stehen im Inselbetrieb auf 0",
          f"max. {worst_island:.3f} W")

    # --- 4. the two relay modes that need no fault -------------------------
    # Ueberschuss: surplus = (pvA+pvB) - (load + s0), Netzbezug: grid > 0.
    # Only a quarter of the load/pv mismatch reaches the grid meter (see
    # _drift), so the default household of ~870 W peaks at ~270 W of grid draw
    # and cannot reach the relay's 500 W default threshold. --lastung raises
    # the household, which is what a real surplus-poor site does.
    best_surplus = -1e9
    best_draw_default = -1e9
    for k in range(60):
        sim.START = time.time() - 1200.0 + 20.0 * k  # walk through a whole day
        sim._drift()
        s = read_all(sock, ["dc_conv.dc_conv_struct[0].p_dc_lp",
                            "dc_conv.dc_conv_struct[1].p_dc_lp",
                            "g_sync.p_ac_load[0]", "g_sync.p_ac_load[1]",
                            "g_sync.p_ac_load[2]", "io_board.s0_external_power",
                            "g_sync.p_ac_grid_sum_lp"])
        load = sum(s[f"g_sync.p_ac_load[{i}]"] for i in range(3))
        pv = (s["dc_conv.dc_conv_struct[0].p_dc_lp"] +
              s["dc_conv.dc_conv_struct[1].p_dc_lp"])
        best_surplus = max(best_surplus, pv - (load + s["io_board.s0_external_power"]))
        best_draw_default = max(best_draw_default, s["g_sync.p_ac_grid_sum_lp"])
    check(best_surplus > 500.0, "Überschuss über der Relay-Standardschwelle 500 W",
          f"{best_surplus:.0f} W")

    sim._LOAD_BASE = 654.0 * 4.0  # --lastung 4
    best_draw = -1e9
    best_load = -1e9
    for k in range(60):
        sim.START = time.time() - 1200.0 + 20.0 * k
        sim._drift()
        s = read_all(sock, ["g_sync.p_ac_load[0]", "g_sync.p_ac_load[1]",
                            "g_sync.p_ac_load[2]", "g_sync.p_ac_grid_sum_lp"])
        load = sum(s[f"g_sync.p_ac_load[{i}]"] for i in range(3))
        best_load = max(best_load, load)
        best_draw = max(best_draw, s["g_sync.p_ac_grid_sum_lp"])
    print(f"      größter Überschuss {best_surplus:7.0f} W, "
          f"größter Bezug bei Standardlast {best_draw_default:7.0f} W, "
          f"bei --lastung 4 {best_draw:7.0f} W (Last bis {best_load:.0f} W)")
    check(best_draw_default < 500.0,
          "Standardlast bleibt unter der 500-W-Schwelle (dokumentierte Grenze)",
          f"{best_draw_default:.0f} W")
    check(best_draw > 500.0, "Netzbezug über der Relay-Standardschwelle 500 W "
          "mit --lastung 4", f"{best_draw:.0f} W")
    # The grid number has to cross zero as well, otherwise the direction of the
    # relay can never be checked against what the panel shows.
    check(best_surplus > 0 and best_draw_default > 0, "beide Richtungen kommen vor")
    sim._LOAD_BASE = 654.0

    # --- 5. --faults reaches the fault words -------------------------------
    sock.close()
    sim2 = load_sim(faults=[0x00000040, 0, 0, 0])
    check(sim2.get_value(R.get_by_name("fault[0].flt").object_id) == 0x40,
          "--faults setzt fault[0].flt")
    sim2._drift()
    check(sim2.get_value(R.get_by_name("fault[0].flt").object_id) == 0x40,
          "_drift() überschreibt die Störung nicht")
    check(sim2.get_value(R.get_by_name("fault[3].flt").object_id) == 0,
          "die übrigen Störungsworte bleiben 0")

    sock.close()
    srv.close()

    print(f"\n{'OK' if _failed == 0 else 'FEHLER'}: {_checks} Prüfungen, "
          f"{_failed} fehlgeschlagen")
    return 0 if _failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
