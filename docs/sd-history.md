# SD-card history logging — evaluation (rct-panel)

Status: **implemented and verified on the board** (2026-09-29). The design
below was verified against the board hardware, the installed rctclient
registry and the current code; see section 6 for the outcome.

Goal: log, every 5 minutes to the on-board microSD (TF) slot:

- both PV inputs (A: `dc_conv.dc_conv_struct[0].p_dc_lp`, B: `[1]`),
- S0 meter input (`io_board.s0_external_power`, already polled for the graph),
- temperatures (core `db.core_temp`, battery `battery.temperature`, heat sink `db.temp1`),
- household consumption (`g_sync.p_ac_load[0..2]`),
- battery charge/discharge load (`g_sync.p_acc_lp`, signed, + = charge) and SOC (`battery.soc`),
- grid load / feed-in (`g_sync.p_ac_sc[0..2]`, signed, + = feed-in).

## 1. Hardware: the 4848S040 has a TF slot — and it works in SPI mode

The module carries a microSD (TF) slot. Pinout (vendor diagram, cross-checked
with a working Tasmota 13.4 setup and the ArduinoGFX example on this exact
board):

| Function      | GPIO | Notes                                      |
|---------------|------|--------------------------------------------|
| TF SCK        | 48   | shared with LCD-config SPI SCK (boot-only) |
| TF SDA (MOSI) | 47   | shared with LCD-config SPI MOSI (boot-only)|
| TF MISO       | 41   | SD card DO                                |
| TF CS         | 42   | SD card chip select                        |

Facts established by others on this board:

- Tasmota on the ESP32-4848S040 mounts this slot: `UFS: SDCard mounted (SPI
  mode) with ~30 GB free` — SPI works with a 32 GB FAT32 card.
- SDIO/1-bit attempts showed `0 kB free` (did not work). Conclusion: **use SPI
  mode** (SDMMC is not wired usefully).
- Tasmota notes that DMA must be disabled for SD sharing the bus with the
  display config link. Our display config SPI (`Display.cpp`) is **bit-banged**
  and only active during boot init (CS 39). After init, GPIO 48/47 are idle, so
  a hardware SPI (FSPI) bus `SPI.begin(48, 41, 47, 42)` is free to drive the
  TF slot. Keep LCD_CS (39) de-asserted. A blocking, non-DMA SPI transaction
  once every 5 minutes has zero impact on the LCD (which uses esp_lcd RGB DMA
  on entirely different pins).
- No card-detect pin is documented → detect by mount result, retry periodically.
- Bus clock: use ~5–10 MHz (some UHS-class microSDs are picky in SPI mode).

## 2. Data availability

Everything above is already in `RctSnapshot` (no new RCT registrations needed
except optional extras):

| Column              | Snapshot field       | OID / source                 | Unit |
|---------------------|----------------------|------------------------------|------|
| timestamp           | —                    | `time(nullptr)` (SNTP, see main.cpp `configTzTime`, CET/CEST) | unix s |
| pv_a / pv_b         | `pvPower[0..1]`      | dc_conv.dc_conv_struct[i].p_dc_lp | W |
| s0                  | `s0Power`            | io_board.s0_external_power   | W |
| temp_core/bat/hsink | `coreTemp`,`batteryTemp`,`heatSinkTemp` | db.core_temp, battery.temperature, db.temp1 | °C |
| household           | `loadPower[0..2]`    | g_sync.p_ac_load             | W (per phase) |
| battery             | `batteryPower`       | g_sync.p_acc_lp (signed)     | W |
| soc                 | `batterySoc`         | battery.soc                  | % |
| grid                | `gridPower[0..2]`    | g_sync.p_ac_sc (signed)      | W (per phase) |

Optional extras already in the snapshot: `batteryStatus` bitfield,
`faultBits[4]`, `deviceName`. A `status` column (0 = ok, bits for active
faults) costs nothing and makes power-loss / fault correlation easy.

The 5-minute cadence already exists in `GuiApp.cpp` refreshCb (the "Verlauf"
ring buffer, `HIST_INTERVAL_MS`); the SD writer reuses that beats or keeps its
own timer — the same values are already assembled there.

## 3. Volume, endurance, power

- CSV line ≈ 110–140 B (13 columns). 288 lines/day ≈ **~40 kB/day**,
  ≈ 1.2 MB/month, ≈ **15 MB/year**.
- Binary (uint32 ts + 14×float32) ≈ 60 B/line ≈ 17 kB/day ≈ 6 MB/year.
- Either format fits a 1 GB card for decades; SD wear is negligible at this
  rate. Flush after each line: worst case on power loss is the current sample.
- One write per 5 min: no meaningful current draw.

## 4. Recommended design

**Format: CSV, one file per calendar month, FAT32.**
Volume is trivial; CSV is directly importable (Excel, pandas, InfluxDB) and
inspectable on the card. Monthly rotation (`hist/RCT-202609.csv`) keeps files
small and avoids FAT32 single-file size limits entirely (would not be hit
anyway).

- New `src/storage/` module, **own FreeRTOS task that owns the card** (section 4b):
  - `sdInit()`: starts the worker task. The worker does
    `SPI.begin(48, 41, 47, 42)`, `SD.begin()`; retried every ~10 s if no card.
    Idle when absent (no UI errors forced).
  - `sdLogSample(const RctSnapshot &s)`: skip while `!s.haveData` (no zero
    rows for a disconnected inverter); format the line (pure computation, needs
    the snapshot) and hand it to the worker, which appends it + `flush()`;
    opens the month file with `FILE_APPEND`, creates it with a header row on
    first write.
  - Monthly rotation keyed off SNTP date; if SNTP not yet valid, fall back to
    uptime-based `UPT-<days>.csv` until the first sync.
- Call site: `main.cpp` loop (or next to the existing 5-minute history code) —
  one `sdLogSample()` per 5 min.
- Errors: a row that cannot be written is **parked in RAM and retried** (see
  section 4a), never silently dropped. Service page shows `SD: OK | 8,4 GB frei`,
  `SD: -- | 5 gepuffert` or `SD: OK | 2 Zeilen verloren`.
- Capability check at boot: write a `hist/PROBE` marker once per mount and
  remove it after the first successful flush, as a self-test. The marker is
  **not** re-written when a card is re-inserted during operation: that
  delete + create is FAT metadata traffic which measured 1971 ms on this
  400 kHz bus, and `SD.cardSize()` already answers the same presence
  question for free.

### 4a. Card pulled out while running: RAM queue, one hour deep

The card sits in an external slot and may be pulled at any time; a write can
also fail on a full or marginal card. Dropping the row would punch a hole in
the 24 h chart, so:

- **12-slot ring buffer** of already-formatted CSV rows — 12 × 5 min = one
  hour. On overflow the oldest row goes (recent data is what the chart needs)
  and the loss is counted.
- Each entry stores the **formatted line plus its target path**, not the
  snapshot. The row therefore keeps its original timestamp, and a month
  rollover during the outage still splits correctly across two files.
- Retry is throttled (5 s) from the worker's periodic pass, oldest first,
  stopping at the first failure so the file stays chronological. A returning
  card is flushed immediately on mount.
- **Detecting removal**: a card pulled out is invisible to a writer that only
  notices at the next 5-minute write. The worker therefore polls
  `SD.cardSize()` every 5 s while mounted; `0` means the card is gone → unmount,
  report `SD: -- | n gepuffert`, and let the normal mount retry bring it back.
- Write success is judged by the **return value of `println()`**, not
  `getWriteError()`: the ESP32 core's FS write path never calls
  `setWriteError()`, so that flag stays 0 even on a failed write.
- Status text mirrors the queue: `SD: OK | 12 gepuffert | 16,0 GB frei`, and
  `SD: OK | 3 Zeilen verloren` while rows have been dropped since boot.

### 4b. The card lives in its own task

LVGL, the RCT poll and `main.cpp`'s `loop()` all run on the Arduino loop task.
At 400 kHz SPI a single card operation is milliseconds, and that is invisible.
Two of them were not:

| Operation | Measured |
|---|---|
| `SD.begin()` at boot | 1457 ms |
| History restore (scan of up to 288 CSV rows) | 1418 ms |
| PROBE `SD.remove()` + re-create (removed, see 3) | 1971 ms |

Each of these held the loop task, so the display, the touch input and the RCT
poll all stopped together — a visibly frozen panel, and any click landing in
that window is lost. The stall logger in `main.cpp` is what caught them.

`sdlog.cpp` therefore runs a worker task (`sdTask`, FreeRTOS priority 1, i.e.
equal to the loop task, 4096-byte stack) that **owns the card exclusively** —
the ESP32 SD/FS layer is not thread-safe, so a shared "SD is free" semaphore
would be a data race, not a solution. The GUI thread only ever:

- formats the CSV line and posts it (`SDREQ_LOG`, queue depth 4),
- asks for the history and collects it later (`SDREQ_HISTORY`),
- reads the status string, which is copied out under a mutex.

The history restore is therefore asynchronous, because the caller lives on the
GUI thread and must not wait for the card:

```c
sdRequestHistory(HIST_POINTS, !graceOver);   // once, returns immediately
int n = sdTakeHistory(seed, HIST_POINTS);    // each GUI tick: -2 pending,
                                             // -1 clock not up, >= 0 rows
```

`s_histOut` holds the result, `s_histReady`/`s_histDeferred`/`s_histCount` are
written under `s_lock` and cleared once collected, so a second request while
one is running simply overwrites the result — harmless, the GUI only asks once.

**Explicitly not recommended:**

- **SDMMC (SDL/SD_MMC library)**: pinout does not work on this board (verified
  above); SPI is plenty fast for 5-minute logging.
- **Internal flash (LittleFS/NVS)**: works for small logs but competes with
  OTA space and incurs flash wear over years; the TF slot exists and is the
  right home for bulk history.
- **Logging daily energy counters instead of powers**: powers at 5-min
  resolution are the requested data; daily totals already live on the panel
  ("Heute"). If energy integration is wanted, it can be derived from the
  logged powers later.

### 4c. Long writes (screenshot BMP) and the task watchdog

The 400 kHz bus keeps a 5-minute CSV append in milliseconds, but a **screenshot
BMP** is 691 254 bytes (480×480 RGB565 + 54-byte header). At 400 kHz the SPI
transfer alone needs >14 s, and the data phase of `spi_device_transmit` holds
CPU 0 for seconds at a time. With `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5` the panel
rebooted *mid-write* (watchdog abort, `CPU 0: sd` in the panic dump), leaving a
0-byte file behind from the truncated open().

Two fixes, both verified on the board:

- The row loop of `sdWorkerWriteShot` calls `esp_task_wdt_reset()` and
  `vTaskDelay(1)` after every row, so IDLE0 gets CPU time and the TWDT is fed
  for the ~16 s a legitimate write needs — a transfer that *really* hangs still
  panics, because the feed happens only between rows, never inside a stuck
  `f.write()`.
- The stall logger in `Diag.cpp` treats the `sd.shot` phase specially: the
  generic 4 s warning threshold would otherwise report every legitimate 16 s
  write as a hang. The phase runs up to 20 s before it is flagged.

Verified in the serial log: two consecutive screenshots completed
(`SD: Screenshot -> /shot/shot00N.bmp (480x480, 691254 Bytes)` after ~16 s)
with no `task_wdt` abort and no reboot. Files written before the fix under the
same numbering (shot001–005, 0 bytes) are leftovers, not regressions.

## 5. History restore: 24 h chart survives a reboot

The "Verlauf" ring buffer (`s_hist`, 288 x 5 min) lives in RAM only, so a
reboot used to leave the chart empty until it had refilled. The SD log now
feeds it back at boot:

- The worker's history scan (`SDREQ_HISTORY`, section 4b) reads the newest
  ≤ 288 CSV rows and maps them to the chart series: `Netz = grid_l1+l2+l3`, `Verbrauch = load_l1+l2+l3 + S0`
  (the inverter's load meter reads demand minus the external generator, so the
  external power is added back), `PV = pv_a+pv_b`, `S0`, `Bat`, `SOC` — exactly
  what the live sampler stores. SOC is a percent value on its own chart axis
  (`LV_CHART_AXIS_SECONDARY_Y`, 0..100), so 0 % is the bottom and 100 % the top
  of the chart regardless of the power autoscale.
- The rows are replayed through the **same** writer (`histPush()`) as live
  samples, so ring cursor and the LVGL series cursor stay in lockstep and the
  chart looks exactly like a continuous recording.
- Month rotation on read: when the current month file has fewer rows than
  288, the previous month's tail is prepended, so the 24 h window stays full
  across a calendar boundary.
- Boot order: the first live sample waits for a decision — card mounted (seed
  from log) or no card within a 60 s grace window (start fresh). A card
  inserted later in the session does not clobber the running history.
- The card often mounts within ~2 s of boot, i.e. *before* SNTP has a time. The
  monthly file cannot be named then, and the pre-SNTP uptime file is the wrong
  one (the writer switches to `RCT-YYYYMM.csv` the moment the clock is up).
  The worker therefore defers (`sdTakeHistory()` returns −1) while the clock is
  pending, and the seed is re-requested on the next 1 Hz tick. After the 60 s grace window
  the deferral stops and the uptime file is read as a last resort.
- Early in a new month the current month's file is empty while the last 24 h
  are still in the previous one: the reader now falls back to the previous
  month file in that case (it previously returned "nothing to restore").

## 6. Implementation status

Implemented: `src/storage/sdlog.{h,cpp}`, hook in `main.cpp` (5-min beat),
Service page "SD-Log" status line, the history restore above, the one-hour RAM
queue with card-removal detection (section 4a), and the SD worker task that
keeps the card off the GUI thread (section 4b).

Verified on the board: TF slot in SPI mode (SCK 48 / MISO 41 / MOSI 47 /
CS 42), 400 kHz init per SD spec, mount retry every 10 s while the card is
absent, monthly CSV + header, `PROBE` self-test marker. Screenshot feature
(Service-page button, 5 s delay, PSRAM buffer, `lv_draw_buf` snapshot, BMP
writer in the SD worker) verified end-to-end: two complete 691 254-byte BMPs
written in a row without the watchdog abort that the first attempt triggered
(section 4c).

Not verifiable here: pulling the card while running (no hardware access to the
slot), and a full card. A card that is present but *not writable* was not
tested; the parked-row path assumes the mount succeeds first. The ring arithmetic and the retry order were checked
separately against the exact code; the write path itself is confirmed by the
restore count growing across runs (20 → 22 rows, i.e. two 5-minute samples
landed).

> A first card was dead (no CMD0 response on any device). The pinout was
> cross-checked against vendor pinout, Tasmota and ESPHome configs — all
> agree on 42/41/47/48. With a healthy card the mount succeeds immediately.

## 7. Effort / steps if approved

1. `src/storage/sdlog.{h,cpp}`: SPI/FSPI init + mount/retry, CSV writer with
   monthly rotation, sample capture from `rctState`.
2. Hook into the 5-minute beat in `main.cpp` / next to the history sampler.
3. Service page: small `SD:` status line (mounted / free kB / missing).
4. platformio.ini: nothing new needed (Arduino ESP32 core ships `SD` + FS).
5. Test with a real FAT32 microSD: mount log, one sample, power-cycle mid-log,
   pull card and inspect CSV.

> Anything S0-related beyond `io_board.s0_external_power` (e.g. the S0 energy
> counter `io_board.s0_sum`) would be one additional RCT slot — say the word.