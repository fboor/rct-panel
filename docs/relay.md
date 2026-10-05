# The switched output (relay port)

One output, one selectable function. The panel decides, the user chooses *what*
the output follows.

Implementation: `src/output/Relay.{h,cpp}`, pins and polarity in
`src/output/RelayPins.h`. Display and operation on the Service page
(`src/gui/GuiApp.cpp`), form on the web interface's start page
(`src/web/WebServer.cpp`), two number fields in the setup portal
(`src/config/Configuration.cpp`).

## 1. The five functions

| # | Function | Switches on when |
|---|----------|------------------|
| 0 | Off (default) | never |
| 1 | Grid draw > threshold | `gridPowerSum` (positive = import) above the threshold |
| 2 | Feed-in > threshold | exported power (`−gridPowerSum`) above the threshold |
| 3 | Fault | one of the 128 fault bits is set |
| 4 | Island operation | `islandKnown && islandMode` |

The numbers are at the same time the order in which the Service page cycles
(Off → Grid draw → Feed-in → Fault → Island operation → Off), and at the same
time the values of the portal field `relay_mode`.

### Why that order

`Off` comes first because it is the state the device ships in and the one you
want back after an experiment — a misconfiguration then costs nothing. The two
threshold functions follow because they are the ones the device is bought for.
Fault and island operation come last: they are special, but they are exactly the
cases in which an output makes sense as an alarm, and nobody configures them by
accident.

### The grid meter and the new rule

**Changed on 2026-10-03.** "Feed-in" now means **export**, no longer "own
generation minus house consumption":

```
Feed-in = −gridPowerSum          // positive while energy goes into the grid
```

The meter at the grid is the only figure that answers the question that matters:
**Is energy leaving the house?** Everything the house sends out is available to
the consumer — own generation, the battery and the S0 meter. The panel does not
have to add anything up; it reads one measurement.

The old rule had two errors that showed up in daily use:

* **The battery.** 3000 W generation, 1000 W house, 2000 W into charging the
  battery: by the old rule that was 2000 W of "surplus" and the output switched
  on although nothing went into the grid. By the new rule it is 0 W and the
  output stays off — the surplus is in the battery, not in the grid.
* **Foreign power.** A 2 kW S0 plant with 1 kW house consumption exports 1 kW. By
  the old rule that was *own* surplus only with own generation, the S0 counted as
  consumption — the output stayed off although 1 kW sat in the grid. By the new
  rule it counts, because it is energy that is available.

Sign and display: grid power is negative while exporting (as everywhere in the
project), so the web interface shows the **magnitude** of the feed-in in watts —
"on · 640 W now" means 640 W into the grid.

The old figure can no longer be computed from the data the panel now has: it lived
in `pvPower` and `loadPower`, which are still logged for the history view, but
the decision now comes only from the grid meter.

## 2. Time behaviour

```c
ON_DELAY_MS   = 20000   // the rule must want "on" this long, then it switches
MIN_HOLD_MS   = 60000   // once on, at least this long on
BAND_PERMILLE =  200    // hysteresis: off only below (threshold − 20 %)
EVAL_MS       =  1000   // the rule is checked at 1 Hz (RCT data comes every 10 s)
DATA_MAX_AGE_MS = 600000// no fresh data -> off (10 min, see below)
```

The delay and the minimum hold time were originally 10 s and 30 s and have been
doubled. Reason: a cloud in front of the PV generator and a house that pulls hard
and lets go again both produce crossings at which a short window would chatter. A
mechanical relay that switches every few seconds is a fault report, not a
function.

The hysteresis is separate and sits at 20 % of the threshold: with 500 W the
output switches on at 500 W and off at 400 W. Without it, a value sitting exactly
on the threshold would toggle once per `poll`.

**No data → off.** The output follows a state the panel does not know. An output
that stays on because the inverter disappeared would be the worse variant — it
would keep running no matter what the device last did. Hence `!haveData || age >
2 min` is an "off", and that for *every* function.

### The data deadline: 2 min → 10 min (October 2026)

Observed on the user's installation: the inverter sometimes delivers no values
for several minutes although the TCP connection stays up the whole time. With the
old two minutes the deadline was therefore too short — with a function like
`Grid draw` the output switched off once during every such pause and on again at
the next value. The rule was working against the device it is supposed to follow.

`DATA_MAX_AGE_MS` is therefore **600000** (10 min). The alternative would be to
switch the age limit off entirely and use only `haveData`, i.e. an actually closed
stream; that is the stricter and, for a heating load, the more dangerous variant,
because the output would then also stay on when the device has been dead for
hours. Rejected.

The price of the extension, spelled out: for up to ten minutes the output acts on
a value that is up to ten minutes old — in the extreme case it switches **on**
once, with a nine-minute-old justification. Hence the number is visibly marked as
old instead of letting the rule quietly run on a stale value:

- the **status bar** shows `wartet` (yellow) instead of `aktiv` (green) after 60 s
  without a new frame — the values on the pages are then the ones that arrived
  last;
- the **line under the output** reads `AN · 512 W (letzte Messung)` instead of
  `AN · 512 W jetzt` and is dimmed;
- the **web interface** says `wartet` in its "Wechselrichter" line as well.

All three use the same threshold and the same decision (`src/DataStatus.h`,
checked in `tools/badge_test`) — they cannot drift apart.

The value lives in `Relay.cpp` and is not stored in NVS. It is therefore a
property of the build and not of the installation — anyone who needs a different
deadline needs a different firmware.

### Checking `wartet` against the simulator

`wartet` only arises from a connection that is up and silent: in `rctParse()`
the socket is only given up at `RCT_RX_TIMEOUT` when `rctClient.connected()` is
false. Simply stopping the simulator makes the panel show `verbinde neu` — a
different state.

The simulator can therefore do both: `--quiet-after SECONDS` stops answering from
that point on and leaves the connection open, `SIGUSR1` toggles:

```
python3 ../rct-panel-simulator/rct_sim.py --port 8899 --quiet-after 60   # silent from 60 s on
kill -USR1 <pid>                                                         # once: off, twice: on
```

The panel has to point at the simulator for this. There is a build flag for it,
`RCT_SIM_HOST` (see `Configuration.cpp`); the invocation in the comment there is
the verified one. For the test, `Grid draw` with a high threshold is better:
nothing switches, but the line with `(letzte Messung)` is there.

Verified on 2026-10-01 on the user's installation: the simulator on a machine in
the same network, the output on grid draw / 5000 W, after 70 s `wartet` in the
panel's status bar and in the "Wechselrichter" line of the web interface. The
line with `(letzte Messung)` stayed open in that run because the screenshot
showed the page the panel was on.

## 3. Pin and polarity

`RELAY_PIN 40` — the board's 1-way relay port (silkscreen "1Way/3WayRelayPort"),
the only free pin in the project: display and touch take 3..21/38/39, the SD card
41/42/47/48, 1 and 2 are the board's two other relay ports and stay free for a
second output.

`RELAY_ACTIVE_LOW 0` in `RelayPins.h` — so closing on HIGH. This does not follow
from the code but from the module; it was measured on the wall (2026-09-30, see
`RelayPins.h`): the pin sits HIGH while the output is idle, and the output follows
a HIGH. ESPHome drives `switch: GPIO 40, inverted` for this board, which claims
the opposite — if something switches there, it is this constant. The test button
on the Service page (5 s on, 5 s off, twice) is the check; if the relay stays
silent, flip the constant and flash again. Nothing else changes, because every
switching goes through `relayWrite()`.

Hardware caveat: a module with active-low triggering is *on* while the pin floats
— and between reset and `relayInit()` the pin is an input. On the board's own
relay port the surrounding hardware decides; for a self-wired module 10 kOhm from
the pin to 3V3 (or to GND with active-high) belong there, so the pin does not sit
in the wrong state while the firmware boots. `relayInit()` is the first call in
`setup()` after the two diagnostic calls — long before `displayInit()` — and puts
the pin on the off level with the internal pull first, before it makes it an
output.

## 4. Off on every start

`relayInit()` switches the output off **before** the function is read from NVS. A
relay that slams on while the device boots is a slam — and with a heating load a
surprise on the bill. Besides that: until the rule has held for 20 s it would not
switch on anyway.

The function lives in NVS (namespace `relay`, keys `mode` and `thresh`) and
survives a restart. That is the deliberate decision to be configurable and
therefore to stay configurable: someone who has configured the socket that way
does not want to set it up again after a power cut.

## 5. Operation, three ways

1. **Service page, tap** — cycles the function. The everyday way. The button is,
   like the code, a field with a background; next to it reads
   "antippen = wechseln".
2. **Web interface, "Ausgang" section** — selection field for the function and a
   number field for the threshold, plus the test. Behind the code, because it
   changes the device.
3. **Setup portal** — `relay_mode` (0..4) and `relay_w` (0..5000) as number
   fields. Why numbers and not a selection field: `WiFiManagerParameter` creates
   an `<input>` from its ID (`WiFiManager.cpp`, `HTTP_FORM_PARAM`), and a
   parameter without an ID is written as raw HTML but does not get its value back
   when saving. The way through numbers is the only one that works in the portal at
   all — and it is the only one needed when the panel is not on the home network
   and its own web interface is not reachable at all.

Besides the function name the Service page shows the value it is compared against
("AN · 512 W jetzt"). Without that number a threshold in watts is a number nobody
can set sensibly.

## 6. Test

`relayStartTest()` switches 5 s on, 5 s off, twice — regardless of the rule and
therefore also without data from the inverter. Its purpose is in exactly one
direction: *is this the right pin, and is the polarity right?* A test that
assumes a running function cannot answer that question.

While the test runs, it has the output; afterwards the rule starts at zero (no
afterglow, no open minimum hold time left over from the test).

## 7. The host

`tools/relay_test/` builds `src/output/Relay.cpp` unchanged against two small
stubs (`millis()` from a global variable, `digitalWrite()` into a variable the
test can read, `Preferences` as a RAM file). What is checked is what cannot be
checked on the desk: the 20 s switch-on delay, the 60 s minimum hold time, the 20 %
hysteresis, "no data means off", the S0 share of the feed-in rule, the test
sequence, the function change and the NVS path including values outside the valid
range.

```
tools/relay_test/run.sh      # 70 checks, ~1 s
```

The test found a real error on the first run: `s_lastEvalMs` survived
`relayInit()`. After a second `relayInit()` the evaluation window stayed shut
until `millis()` had caught up with the old value — minutes in which no rule ran.
On the board that does not show (once at startup, statically initialised), in a
test process it very much does.

What remains to be checked on the board, which the host cannot do: pin 40 and
polarity. Hence the test button.

## 8. Checking against the simulator

`rct_sim.py` is an inverter replacement on port 8899. For the two functions that
need no fault word it is the tool with which the threshold is set:

```
python3 ../rct-panel-simulator/rct_sim.py --port 8899                            # standard load ~870 W
python3 ../rct-panel-simulator/rct_sim.py --port 8899 --lastung 4                # household ~2.8 kW
python3 ../rct-panel-simulator/rct_sim.py --port 8899 --faults 0x00000040,0,0,0  # fault bit 6
```

`--lastung` is necessary because in the simulator only a quarter of the load/PV
difference runs through the grid meter (the battery takes 75 %): with the
standard load at most about 270 W of grid draw arise, never the 500 W of the
preset. Only `--lastung 4` makes the import large enough.

The simulator is **not** in this repository: it uses the Python package
`rctclient` (GPL-3.0), and a copyleft dependency in the repository would raise the
question of what licence the repository as a whole is under. It has its own
directory next to this project (`../rct-panel-simulator/`, with its own README)
and its own test run, `rct_sim_test.py`: that it answers every identifier that
`rctOids[]` in `RctClient.cpp` queries, that the balance `load − PV + battery =
grid` adds up in every sample, that island operation ends again, and that feed-in
and grid draw exceed the default threshold. `tools/run_host_tests.sh` runs it as
soon as the directory sits next to the project.

The test found two real errors: the simulator did not answer the six
`energy.e_ext_*` counters of the inverter (the panel would have shown a gap in the
S0 line there), and the test itself missed five identifiers because they are in
the firmware array with seven instead of eight hex digits (`0x3A39CA2`, load L1).
The test run therefore reads `rctOids[]` straight out of `RctClient.cpp` and
compares the length with `RCT_NUM_SLOTS` — a list in the test would hide exactly
what it is there for.

What the simulator cannot do: S0 sits at 0. A generator fed from outside could
only be introduced by counting it in the balance identity, and then the number
would be a decoy.

## 9. What this is not

- **Not a safety device.** The output switches on the basis of a rule that rests
  on measurements. It is not a residual-current device, not an overload
  protection, and no guarantee that a storage heater runs on solar power alone.
- **Not a replacement for the inverter's own load shedding.** Anyone who uses the
  RCT's own-consumption regulation already has it.
- **One output only.** Ports 2 and 3 (GPIO 2 and 1) stay free; a second function
  would be the same machine with one more `RelayMode`, but it is not implemented
  because nobody asked for it.
- **The test switches without asking.** It is a manual action, not a remote
  command — the web interface asks for the code for that.