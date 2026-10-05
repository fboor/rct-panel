# Making the inverter replaceable

## What this is about

The panel speaks exactly one protocol to exactly one device: the RCT Power, 60
registers over TCP on port 8899. The abstraction described here hides **that one
implementation** behind an API that all the other components call: GUI, web
interface, switched output, CSV logger, history.

Explicitly in scope:

- **One** implementation, namely RCT. No second inverter is written, guessed at or
  prepared.
- **Different transports should be possible**, but none is implemented. Behind the
  interface sits a TCP connection; the interface is built so that a serial one can
  be added later without touching the driver side.
- **The origin of the data is in the file name** (`RCT-202610.csv`), not in a CSV
  column. A device type gets an abbreviation, the abbreviation travels into the
  file name, and all logs of one type sit side by side.

Not in scope: a second inverter, a second protocol, own Modbus code, a task of its
own, changes to the CSV format or to the web interface.

> **Where this stands today:** the second driver became an
> *OpenInverterGateway*, and with it `DeviceCaps` — the display asks the driver
> once what its family can do and draws only that. See
> [“What came after”](#what-came-after).

## Starting point

Five places read `rctState`, and none of them knows the protocol:

| Consumer | What it needs | Code |
|---|---|---|
| GUI: overview, history, energy, device, service | almost everything | `GuiApp.cpp`, `refreshCb()` |
| Web interface (6 pages, 2 JSON endpoints) | almost everything | `WebServer.cpp`, `handleRoot()` |
| Switched output (4 rules) | grid, fault bits, island | `Relay.cpp`, `ruleWantsOn()` |
| CSV row (23 columns, every 5 min) | powers, temperatures, fault mask | `sdlog.cpp`, `sdLogSample()` |
| Self-report on the data age | `haveData`, `connected`, `lastUpdateMs` | `DataStatus.h` |

No consumer builds a frame, checks a CRC or knows an OID. The protocol knowledge
sits in `RctClient.cpp` — so the place is right, only the `RCT` is in the name and
in the file.

What *residue* hangs on are the rules. “House = load meter + external yield”
(because the RCT's load meter does not see the S0 yield) stands at **five places
in the C++** (`WebServer.cpp` `loadSum`, `GuiApp.cpp` three times, `CsvRow.h`
`toSample`) and six times in the browser. “Generation = two strings + S0” at four
places in the C++ and one in the browser — and in the history *without* S0, which
without a comment looks like a bug. The signs (grid + = import, battery + =
discharge, PV ≥ 0) are measured and stand in comments next to the fields. These
rules belong in the API, because they are plant logic and not device logic.

Three further places name the device outright:

| Place | What |
|---|---|
| `sdlog.cpp`, `updatePath()` and `sdWorkerReadHistory()` | `"/hist/RCT-%s.csv"`, three times as a string |
| `Configuration.h` | `extern char rct_host[41]`, `rct_port[6]`, NVS keys `rct_host`/`rct_port` |
| Directory and file names | `src/rct/RctClient.{h,cpp}`, `RctTypes.h`, `RctCrc.h` |

## The API

Four new building blocks, one moved one. Names: English like the rest of the code;
in the panel “device” is the inverter anyway (page “Gerät”).

```
  GUI · Web · Relay · CSV · history
              ▲   deviceState(), devicePoll(), deviceLoadW(), …
  ┌───────────┴────────────────────────────────────────────┐
  │ src/device/    DeviceState.h    the neutral values       │
  │                DeviceDriver.h   the driver interface     │
  │                DeviceTransport.h the transport interface │
  │                Device.cpp       factory, poll, prefix    │
  ├─────────────────────────────────────────────────────────┤
  │ src/rct/       RctDriver.{h,cpp}, RctCrc.h              │
  └─────────────────────────────────────────────────────────┘
```

| File | Content |
|---|---|
| `src/device/DeviceState.h` | today's `RctSnapshot` with device-neutral names, reachable as `deviceState()` |
| `src/device/DeviceDriver.h` | the driver interface (pure virtual) |
| `src/device/DeviceTransport.h` | the transport interface (pure virtual) |
| `src/device/Device.cpp` | factory, `devicePoll()`, `deviceTypePrefix()`, the rule functions |
| `src/rct/RctDriver.{h,cpp}` | today's `RctClient.cpp`, as a driver against the transport interface |

### `DeviceState`: the neutral names

The field names are the actual content of the abstraction — they say *what* is
measured, not which register delivers it. The unit is in the name, because it
changes between the fields (a counter in Wh, a power in W) and because the API is
used by several kinds of device.

| today (`RctSnapshot`) | new (`DeviceState`) | unit |
|---|---|---|
| `gridPower[3]`, `gridPowerSum` | `gridW[3]`, `gridExchangeW` (+ = import) | W |
| `gridVoltage[3]`, `gridFrequency[3]` | `gridV[3]`, `gridHz[3]` | V, Hz |
| `loadPower[3]` | `houseW[3]` | W |
| `pvPower[2]` | `genW[2]` (A, B) | W |
| `s0Power` | `extW` (external yield, which the RCT reads at S0) | W |
| `batteryPower`, `batteryVoltage`, `batteryCurrent`, `batterySoc` | `batW`, `batV`, `batA`, `socPct` | W, V, A, % |
| `dayPvWh`, `monthPvWh`, … 14 counters | `dayGenWh`, `monthGenWh`, … | Wh |
| `feedInEnergyWh`, `gridDrawTotalWh` | `feedInTotalWh`, `gridDrawTotalWh` | Wh |
| `dayExtWh`, … and the `Plain` variant | `dayExtWh`, … | Wh |
| `batteryStatus`, `faultBits[4]` | unchanged | bit field |
| `deviceName`, `firmwareVersion` | unchanged | text |
| `coreTemp`, `batteryTemp`, `heatSinkTemp`, `nextCalibTs`, `batteryCycles`, `batterySoh` | unchanged | °C, s, cycles, % |
| `islandMode`, `islandKnown`, `haveData`, `haveBattery`, `connected`, `lastUpdateMs` | unchanged | — |

`haveBattery` and `islandKnown` were flags instead of capabilities: they say "the
device has not answered yet", not "the device cannot do this". For a second driver
that is not enough, and exactly at this difference a second device becomes
visible — hence `DeviceCaps` now exists (see below).

### The rules belong in the API

Because they stand in five places, they become functions of the abstraction and
land in `src/device/Rules.h` — header-only and without Arduino, in the style of
`DataStatus.h`, so the host test can check them:

```cpp
float deviceGridExchangeW();   // grid, + = import
float deviceGenerationW();     // genW[0] + genW[1] + extW
float deviceHouseW();          // houseW[0..2] + extW
float deviceBatteryW();        // + = discharge
```

The history keeps its own computation (`toSample`: generation without S0, S0 as
its own series), because that is a deliberate exception and not a forgotten spot.

### `DeviceDriver`

```cpp
class DeviceDriver {
public:
  virtual ~DeviceDriver() = default;
  // Once. Own buffers, no heap, no task.
  virtual void begin(const DeviceConfig &cfg) = 0;
  // Exactly one retrieval, at most budgetMs long. Must hold old values when a
  // register is missing, and call the yield hook within that time.
  virtual void poll(uint32_t budgetMs) = 0;
  virtual bool connected() const = 0;
  virtual const char *typeName() const = 0;   // "RCT" - also the file name
};
```

The time condition is the part that is not negotiable: `rctParse()` runs in the
LVGL task and blocks there for up to 4 s; what saves it is `rctSetYieldHook()`,
which `main.cpp` fills with `displayLooper()+lv_tick_inc`. A second driver that
does not do this freezes the panel — which is why it is in the contract and not in
a header comment.

### `DeviceTransport`

```cpp
class DeviceTransport {
public:
  virtual ~DeviceTransport() = default;
  // Millisecond timebase for all waits: a serial interface computes its frame
  // pauses from the baud rate, not from a socket.
  virtual bool open(const char *host, const char *port, uint32_t timeoutMs) = 0;
  virtual size_t write(const uint8_t *buf, size_t n) = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual bool peerOpen() = 0;   // TCP: socket alive; RS485: always true
  virtual void close() = 0;
};
```

Four properties follow the interface, so that an RS485 variant later does not have
to change anything on the driver side:

- **Bytes, not frames.** Framing, CRC, addressing and answer collection belong to
  the driver, not to the transport. A Modbus RTU frame (address, function code,
  register, CRC16, 3.5 character pause) is a different framing language from the
  RCT bus with escaping — both have to be able to coexist.
- **`peerOpen()` instead of `connected()`.** With TCP, “the connection is down” is
  a report from the socket and a separate error case (`RctClient` stops the socket
  on it). With RS485 that does not exist.
- **Milliseconds instead of a timebase of its own** in the interface, so that the
  driver can compute its wait windows itself.
- **DE/RE belongs in the transport**, not in the driver: switching the transmit
  direction is a property of the electrical connection. This interface
  deliberately has no method for it — it arrives with the implementation, not as
  an empty slot.

### Configuration

`DeviceConfig { char type[12]; char host[41]; char port[6]; }`, from NVS:

| Key | Meaning | Fallback |
|---|---|---|
| `device` | device type, today always `RCT` | `RCT` |
| `device_host` | address of the device | `rct_host` |
| `device_port` | port | `rct_port` |

The fallback is a requirement, not cosmetics: a panel that loses its address on
update has no inverter afterwards, and that is noticed unpleasantly. The old keys
are read for one release, no longer written.

## The file name as proof of the device

`deviceTypePrefix()` delivers `"RCT"`, from `RctDriver::typeName()`. The logger
builds the path from it, in two places instead of one with hard-coded text:

```cpp
snprintf(path, sizeof(path), "/hist/%s-%s.csv", deviceTypePrefix(), key);
```

Thus the logs of two device types sit side by side and the assignment is in the
name. A column in the CSV would not be needed for that — and would be harmful,
because it would change a format that the old history still has to be able to
read.

A side effect one has to know: the history finder reads `<type>-<month>.csv` and
`<type>-<month before>.csv`. After a device type change it does not find the old
files, and the 24-hour history has a gap on the day of the change. That is correct:
the rows of two devices in one diagram would be wrong, and the file name is
exactly what one can see it on. The uptime name without a clock (`UPT-<days>.csv`)
stays as it is, without a prefix — as long as the clock is not valid, the type in
the name is not true yet.

## Step order

All five steps are built (commits `2be46e5`, `cfc1d42` and the driver and transport
commit). No step changed what stands on the display; that was verified with both
builds and the host tests, not with a photo of the panel.

| Step | Content | Proof |
|---|---|---|
| **1. Names** | `DeviceState.h` with neutral fields, `deviceState()` as the access; all five consumers converted. Pure renaming, no behaviour. | both builds green, host tests green |
| **2. Rules** | `Rules.h` with the access functions plus a sign table; `loadSum()` and the five duplicates die, the history keeps its exception. `tools/device_test` checks both kinds of device. | 42 checks green |
| **3. Transport** | `DeviceTransport` + `TcpTransport`; the `WiFiClient` concerns move out of `RctClient.cpp` into the transport. Purely mechanical, the byte protocol stays untouched. | `crc_test` green, device still reachable |
| **4. Driver** | `DeviceDriver` + factory; `RctClient.cpp` becomes `RctDriver.cpp`; `main.cpp` calls `devicePoll()`. | both builds green |
| **5. Type in the name** | `deviceTypeName()` in the logger (two places), NVS `device_type`/`device_host`/`device_port` with fallback to `rct_host`/`rct_port`. | the new file is still called `RCT-202610.csv` |

Step 1 was the big diff (field names in `GuiApp.cpp`, `WebServer.cpp`,
`sdlog.cpp`) and nevertheless the least critical: the compiler finds every spot,
and no number changes.

One item from the list turned out bigger while building it: the duplicated period
computation. `energyPeriodValues()` in C++ and `rpEnergy()` in the browser do the
same thing from different sources, and both had to be hung on the rule — the C++
part through `rulePeriod()`, the browser part through the rows it reads from the
CSV. The rule now stands once in each place instead of twice.

A detail that only showed up while building: the sixth copy of the house rule was
not in the flow diagram but in the “Heute” cards, which added up their daily
counters themselves. It was inconspicuous only because it stood in the same file
as the others.

## What came after

The plan is executed, and ten commits later the second driver stands. What stood
above as “out of scope” is now built — except for the name the panel uses in the
log and in the CSV. The order was not the plan's; it arose from what turned out to
be the first thing that unlocked something else.

### 1. The second driver: OpenInverterGateway

`src/oig/` with two parts that have to be kept apart:

- **`OigFields.h`** — the field names as data. They are extracted from all seven
  Growatt protocols, because the models carry different names for the same
  quantity. The panel asks `/status` and reads the answer by name; it does not ask
  for a register. That way the grid frequency is also right on a model that calls
  it `grid_freq` instead of `fac_frequency`.
- **The eighth source is a real device.** A Growatt MIC 1000 in the test differs
  from the seven protocols in two places: the AC power is called `OutputPower`
  (not `AcPower`), and the generation counters are called
  `TodayGenerateEnergy`/`TotalGenerateEnergy`. On top of that come fields that
  only exist on a hybrid — and the panel-less question of whether **a battery is
  connected at all**: the MIC 1000 without a battery reports `BatteryState` 0,
  `SOC` 0, `ChargePower` 0, `DischargePower` 0 and `BatteryVoltage` 0. So “there is
  an SOC field” does not follow as “there is a battery”. The rule stands as
  `oigBatteryPresent()` in the header so the host test can reach it, and it logs
  the raw value along with it.
- **The buffer size was a guess and it failed.** `kOigBodyMax` stood at 1024,
  argued with “a string inverter reports about forty fields”. The MIC 1000 answers
  with 64 fields and 1462 bytes. The last 21 fields — **all the energy counters**
  — fell behind the truncation limit, and the panel showed “today 0.00 kWh, total
  0.00 kWh” for hours on a device that has been measuring for years. The buffer is
  now 4096 bytes (3 kB RAM), and a truncated answer stands as `ABGESCHNITTEN` in
  the log line that runs every cycle anyway.
- **`OigDriver.cpp`** — the driver. HTTP/1.0 on the configured port (default
  8899), a `GET /status` whose JSON is read with `Json.h` (header-only,
  host-testable, no Arduino). No `chunked` decoder, because the stick answers
  without chunking.

That was the part with the real surprises: the stick answers `/status` with
**503** when no inverter is running — so from here an awake stick without an
inverter looks exactly like a sleeping one. The driver treats both as “sleeps”
(`DataStatus::Asleep`, the text says “schläft” instead of “keine Daten”), which is
more honest than a display that claims an answer that is not one. The distinction
stands as open decision 4 below.

### 2. `DeviceCaps`: capabilities are a statement about the family

`src/device/DeviceCaps.h`, set by the driver in `begin()` — not from what just
arrived but from what the family can do. Five capabilities and one property:

| Field | Meaning |
|---|---|
| `houseMeter` | there is a household meter (otherwise the panel cannot show a household value — not zero, but nothing) |
| `gridMeter` | there is a meter at the grid connection |
| `battery` | the state of charge answers |
| `islandFlag` | the island flag is backed |
| `faultBits` | the four fault words are backed |
| `sleepsWithoutGeneration` | the device switches itself off at night without PV (the OIG does, an RCT does not — that is the difference between “sleeps” and “is stuck”) |

The RCT driver long did not report its capabilities and worked only because an
empty `caps` happened to read as “nothing known yet” — which happened to mean the
full layout. That is exactly the kind of coincidence a device with fewer meters
gives away at once, and it is fixed.

Two of the capabilities now also decide about **energy figures**, because own
consumption is a difference: `houseMeter − gridMeter` enters `Rules.h` as
`ruleOwnKnown()`. On the MIC 1000 (house no, grid no) there is therefore no own
consumption, no self-sufficiency and no own-consumption share — before, “100 %”
stood there out of a difference of two zeros, and the line *Verbrauch 0,00 kWh*
read like a house that needs nothing. The displays now write a dash, and the JSON
endpoints deliver `null`.

### 3. The diagram follows the capabilities

`src/gui/FlowLayout.h`, header-only and without LVGL, so that the host test
`tools/flow_layout_test` (80 checks) can check the six cases. One rule generates
every layout:

> **The centre is the house, if there is one; otherwise the PV.**

Everything else follows: without a house the battery hangs on the PV, without a
battery and without a grid the PV is the only node and fills the page — 190 px,
with the value in the middle of the band between the circle and the pill. What is
not measured is not drawn: no node, no value, no pill. No arrow without a
connection.

Four facts are remembered as **one byte** in NVS (`gui`/`flowcaps`). Reason: the
capabilities arrive with the first answer, about ten seconds after start. A
diagram that rearranges itself then looks like a bug on a panel that one looks at
rather than diagnoses. So the page builds the layout of the last device that
answered, and a device that says something else is applied once, logged and
remembered for the next start.

### 4. The consumption rule is device-dependent — and is not in the API

That was the actually dangerous spot in the plan: “house consumption = load
measurement plus S0” only holds for an RCT. A device behind a stick has no such
S0 sum. Hence the computation does not stand in the API but in `DeviceSemantics`
(three flags, set by the driver: `loadMeterSeesExternal`,
`genCounterSeesExternal`, `feedCounterNegative`), and the access functions in
`Rules.h` ask them. A driver that does the computation itself cannot forget it.

### 5. The rest that showed up

- **The number format** (`fmtPower()` in `NumFmt.h`): whole watts below 1 kW
  (“380 W”), kilowatts with two decimals above (“5.75 kW”), separator of the
  language. The unit is decided by the **rounded** value, so that 999.5 W is not
  printed as “1000 W” and the same number does not look different in the next
  tick. The host test found exactly this error.
- **The icon** got a font of the size in which it is drawn
  (`lv_font_mdi_icons_136`): a 28 px glyph at 480 % is a 134 px glyph made from 28
  px of information. With a 190 px circle that leaves 38 px of air on each side.
- **The switched output and the maintenance** moved in the web interface from the
  overview to the new `/einstellungen` page, together with device and theme:
  everything that changes something onto a page where nobody reads values. The
  panel previously had no place at which one could change a device — see decision
  1.
- **The cards** in the web follow the same capabilities, the energy bars below
  them do not (decision 5).

## What is deliberately not built

- **No second driver.** An interface that is designed before the second use case
  is largely guesswork. The frame is nevertheless built now, because it is cheap
  and it no longer holds up the detailed work on the driver.
- ~~**No capability negotiation.**~~ **Superseded.** The plan did not want it
  first; with the second driver `DeviceCaps` is built, and the driver reports in
  `begin()` what its family can do. What is *not* built: a negotiation at runtime.
  The display asks once, and a driver does not change its answer.
- **No new CSV column, no change to `/api/*.json`.** Both are promises to the
  outside.
- **No heap and no task of its own** in the 10-second cycle. LVGL shares the task
  with the driver; the same safeguard as today holds. If a device really pushes
  later, that is the one place where something changes — and then `RowQueue` from
  the SD history is the pattern that already exists.
- **No renaming of the 23 CSV columns.** The legacy rule allows appending only.

## Open decisions

1. ~~**Selection of the device type.**~~ **Done.** There are two drivers and a
   `/einstellungen` page with the fields *Gerät* (`RCT` or `OpenInverterGateway`),
   *Adresse*, *Port* and *Theme*. The portal knows only address and port — the
   type is a matter of the panel, not of the access point, because the list of
   drivers grows with the panel.
2. **How strict is the driver?** A register the RCT does not know is not answered
   at all; the driver holds the old value and ends the round after the rest
   period. For another device “no answer” versus “0.0 A” is a real question, and
   the answer belongs in the driver, not on the screen.
3. ~~**Whether the device type should be selectable in the portal at all.**~~
   **Done, answered differently:** not in the portal (see 1), but on a page of
   the panel.
4. **What a silent device means.** The OIG driver cannot distinguish HTTP 503 from
   “no answer”: an awake, speaking stick without an inverter looks like a sleeping
   one, and both mean “sleeps” today. The distinction belongs in the driver
   (evaluate the status code, an `answered` flag in the state) — not in the
   display.
5. **Energy bars and history series.** They still show all quantities and write
   zero rows for counters that are not measured. That was kept deliberately: they
   are counters, not measurements, and “0.0 kWh” on a device without a household
   meter is a statement about the counter, not about the house. Anyone who wants
   it otherwise needs a decision, not a guess.

## Effort

| Step | Order of magnitude |
|---|---|
| 1. Names | medium, purely mechanical |
| 2. Rules | small, plus a test |
| 3. Transport | small, purely mechanical |
| 4. Driver | small to medium |
| 5. Type in the name | small |

## References

- RCT register overview: `https://rctclient.readthedocs.io/en/latest/` (the OID
  table in `RctDriver.cpp` comes from there).
- `src/rct/RctClient.cpp` is a port of the `RctParser` from Energy2Shelly_ESP
  (Apache 2.0). This origin belongs to the driver and travels with it into
  `NOTICE` when the file moves.
- `docs/sd-history.md` describes the format that the name change must not touch.