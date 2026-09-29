# SD-card history logging — evaluation (rct-panel)

Status: **evaluation, not yet implemented.** Everything below is verified against
the board hardware, the installed rctclient registry and the current code.

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

- New `src/storage/` module:
  - `sdInit()`: `SPI.begin(48, 41, 47, 42)`, `SD.begin()`; retried every ~10 s
    if no card. Idle when absent (no UI errors forced).
  - `sdLogSample(const RctSnapshot &s)`: skip while `!s.haveData` (no zero
    rows for a disconnected inverter); append one line + `flush()`; open the
    month file with `FILE_APPEND`, create with header row on first write.
  - Monthly rotation keyed off SNTP date; if SNTP not yet valid, fall back to
    uptime-based `UPT-<days>.csv` until the first sync.
- Call site: `main.cpp` loop (or next to the existing 5-minute history code) —
  one `sdLogSample()` per 5 min.
- Errors: on card full / write failure, log once to serial, keep retrying the
  next sample; show `SD: OK · 8,4 GB frei` / `SD: leer` on the Service page.
- Capability check at boot: write a `hist/PROBE` marker once per mount and
  remove it after the first successful flush, as a self-test.

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

## 5. Effort / steps if approved

1. `src/storage/sdlog.{h,cpp}`: SPI/FSPI init + mount/retry, CSV writer with
   monthly rotation, sample capture from `rctState`.
2. Hook into the 5-minute beat in `main.cpp` / next to the history sampler.
3. Service page: small `SD:` status line (mounted / free kB / missing).
4. platformio.ini: nothing new needed (Arduino ESP32 core ships `SD` + FS).
5. Test with a real FAT32 microSD: mount log, one sample, power-cycle mid-log,
   pull card and inspect CSV.

> Anything S0-related beyond `io_board.s0_external_power` (e.g. the S0 energy
> counter `io_board.s0_sum`) would be one additional RCT slot — say the word.