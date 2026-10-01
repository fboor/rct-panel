# rct-panel

A wall-mount energy panel for the **Guition ESP32-S3 4848S040** (4" 480×480
ST7701 RGB touch display) that shows live data read from an **RCT Power**
device over TCP (RCT "Serial Communication Protocol", default port 8899).

- **UI:** LVGL 9, driven by the **GT911** capacitive touch. Three buttons
  (◀ home ▶) at the bottom navigate between pages.
- **Pages:**
  - **Overview** — mirrors the RCT Portal *Energiefluss* pane (RCT logo
    omitted): a central household node with PV / battery / grid nodes around
    it, connector lines that turn red in the direction of the live energy
    flow, kW values under each node (SOC inside the battery node), and the
    Erzeugung/Verbrauch/Netz/Batterie status table. PV power uses solar
    generators A+B plus the S0 meter; the household node uses the Power
    Sensor load phases.
  - **Heute** — current-day summaries in the style of the portal's
    *Übersicht* and *Energiestatistiken*: Erzeugt / Eigenverbrauch / Eingespeist
    (kWh), Verbrauch / Bezug, plus Autarkie and Eigenverbrauch in %. Autarkie
    is derived as 1 − grid draw / household load of the day.
  - **Verlauf** — line graph of the last 24 hours of power (netz, haus, PV,
    S0, batterie) in kW. A data point is stored every 5 minutes while the
    device runs (288 entries = 24 h). PV A+B and the S0 meter are plotted as
    separate series, matching the portal's separate "+EXT." node; the Y axis
    autoscales and the history lives in RAM (resets on reboot).
  - **Info** — link state, host, port, last data, uptime, per-phase grid
    power (L1/L2/L3), PV total, household load, battery SOC/power/current/
    voltage.
  - **Geraet** — device info: name, control software version, core / battery /
    heat-sink temperatures, next battery calibration (date + day countdown
    once the time is synced), battery cycles, grid frequency, battery SOH and
    island ("Inselbetrieb") mode.
- **Data rate:** all live values are re-read from the inverter every 10 s (the
  device-info group above on the same 10 s cadence); the display redraws at
  1 Hz from the last-known-good values, and the Verlauf graph stores one
  sample every 5 minutes. When the inverter is unreachable the panel keeps
  rendering (badge `no data`, Info page `offline`) and retries the connection
  every 30 s with a bounded 2 s connect timeout, so the UI never stalls.
- **Provisioning (non-blocking):** the GUI keeps running while the panel
  connects to the saved network in the background. Without saved credentials —
  or when the saved network stays unreachable for ~15 s — it serves the
  **RCT-Panel** access point with a captive-portal web page at
  `http://192.168.4.1` where the Wi-Fi credentials and the **RCT host / port**
  are entered. The AP stays up until the panel is configured. Settings and the
  captured Wi-Fi credentials are kept in NVS.
- **Save-only provisioning:** submitting the portal form never blocks the loop —
  the credentials are stored and the network is joined in the background; if the
  network is unreachable (wrong password, no reply) the portal simply re-opens
  after ~15 s so it can be corrected.
- **Save reply + hand-off:** the portal serves its "Saved!" confirmation on an
  undisturbed radio (the panel runs the provisioning AP in `AP_STA` mode with the
  station idle, so the library's save step needs no radio mode change) and keeps
  the AP up for ~3 s afterwards so the browser reliably receives the reply. Only
  then it switches over to the configured network — at which point the
  `RCT-Panel` access point disappears. That is expected: the panel is on your
  network now. A banner on every portal page explains this.
- **Hardware:** ST7701S via 3-wire 9-bit SPI (init) + ESP32-S3 parallel RGB
  (pixels, esp_lcd LCD_CAM), GT911 on I²C `0x5D` (polled; RST/INT not wired).

## Build & flash

```sh
pio run -e esp32-s3
pio run -e esp32-s3 -t upload
pio device monitor           # 115200 baud
```

Requires PlatformIO (`espressif32@6.8.0`, Arduino-ESP32 core 3.x).
The custom board definition lives in `boards/`, partitions in `partitions/`.

## Configuration

| Setting | Field in portal | Default |
|---|---|---|
| RCT device IP / hostname | `rct_host` | `192.168.0.1` |
| RCT TCP port | `rct_port` | `8899` |

To reconfigure: opening the panel's web portal again is easy — power it up with
no reachable network (or wipe NVS with `pio run -e esp32-s3 -t erase`) and it
serves the `RCT-Panel` AP. Connecting to the saved network is always tried
first in the background, so no manual action is needed on normal boots.

> **Upgrading from older builds:** panels flashed with a build older than the
> non-blocking provisioning change did not reliably persist the Wi-Fi
> credentials (the WiFiManager NVS layout was never populated on first connect),
> so a one-time manual provisioning may be required. The current build captures
> the credentials into its own NVS on the first successful connect and is fully
> self-contained from then on.

## Source layout

```
src/
  main.cpp              wiring: init, loop (LVGL tick, non-blocking Wi-Fi
                        provisioning pump, 10 s RCT poll)
  config/               settings, NVS, non-blocking WiFiManager provisioning
                        AP (saved Wi-Fi + rct_host/rct_port)
  rct/                  RCT Power TCP client (ported from Energy2Shelly_ESP)
  display/              ST7701 + esp_lcd RGB driver, GT911 touch, pin map,
                        backlight (LEDC PWM, dims after 3 min without touch,
                        off after 5)
  gui/                  LVGL pages + left/home/right navigation
include/lv_conf.h       LVGL 9 configuration
boards/                 Guition board definition (16 MB flash, PSRAM)
partitions/             16 MB partition table (OTA-capable)
```

## Hardware bring-up notes

Pin map and ST7701 init come from the vendor board diagram; cross-referenced
with the ESPHome device config, openHASP discussion #603, HomeDing board page
and Tasmota discussion #20527 (see `NOTICE`):

| Signal | GPIO |
|---|---|
| LCD DE / VSYNC / HSYNC / PCLK | 18 / 17 / 16 / 21 |
| LCD R0..R4 | 11, 12, 13, 14, 0 |
| LCD G0..G5 | 8, 20, 3, 46, 9, 10 |
| LCD B0..B4 | 4, 5, 6, 7, 15 |
| LCD init SPI CS / SCK / MOSI | 39 / 48 / 47 |
| Backlight | 38 (LEDC PWM, 1 kHz, active high) |
| Touch I²C SDA / SCL | 19 / 45 (GT911 @ 0x5D) |
| SD card (unused) | CS 42, SCK 48, MOSI 47, MISO 41 |

Bring-up checklist (in priority order) once you have hardware:

1. **Backlight:** GPIO38 high → panel should light up (even with garbage). The
   pin is driven as PWM (`src/display/Backlight.h`), so `analogWrite`-style
   dimming is available and the panel dims itself when it is left alone.
2. **ST7701 init:** a non-garbage image (stripes/pattern from LVGL boot
   splash) means the init + RGB timings are right. If the image is shifted or
   color-swapped, adjust the porch values or the R/B pin order in
   `src/display/Display.cpp`/`DisplayPins.h`.
3. **GT911:** with the panel lit, touch a button. If nothing registers, the
   controller may need its config blob written over I²C (polled mode should
   work for OTP-configured modules; a config-table push can be added in
   `src/display/Touch.cpp`).
4. **RCT link:** set host/port in the portal; watch the `RCT: grid ...`
   lines on serial (grid, load, PV, battery values).

Boot sequence (serial 115200): the GUI comes up immediately; a healthy boot
looks like

```
RCT Power Panel boot
Backlight: GPIO 38 at 1000 Hz/10 bit, full 1024/1024
LCD: display ready
WiFi: trying saved profile ...        # or "trying saved network '<ssid>' ..."
Touch: GT911 found at 0x5D
WiFi: connected, RSSI -5x dBm, RCT host '...' port '8899'
WiFi: ip 192.168.1.228, gw ..., dns ...
RCT: grid ... load ...               # live values every 10 s
```

When no saved network is reachable (first boot, moved to a different network,
or NVS erased) the `WiFi: starting 'RCT-Panel' provisioning access point ...`
line appears and the GUI keeps rendering while the portal web page stays
available at `http://192.168.4.1`.

Known hardware quirks encountered during bring-up:

- The board has **octal PSRAM**; `memory_type: "qio_opi"` is required in
  `boards/guition-esp32-s3-4848s040.json` — the default quad setting fails
  the PSRAM self-test on this 4848S040.
- `lv_init()` must run before any other LVGL call (it resets the TLSF heap
  allocator; calling it late crashes with `esp_heap_caps` errors).
- `touchInit()` must run before `lv_indev_create()`, otherwise the wire/touch
  tasks deadlock on the I²C lock ("could not acquire lock").

## Roadmap / open questions

- **On-hardware verification of the flow signs:** battery.power
  (`g_sync.p_acc_lp`, + = charging) and grid + (Bezug) follow the rctclient
  docs; the battery.current sign should be confirmed against the live device
  once hardware is available.
- The S0 meter (`io_board.s0_external_power`) is merged into the PV total when
  present; confirm whether the household load phases already include it for
  your installation (the portal adds a separate "+EXT." node in that case).
- **Device-info decode** (Geraet page): `power_mng.bat_next_calib_date` is
  treated as a Unix timestamp (as in the HA integration) and
  `prim_sm.island_flag` as nonzero = island mode; both should be cross-checked
  against the live device, along with the temperatures / SOH / cycles readings.
- German labels are written without umlauts (ue/ae/oe transliteration) because
  the built-in Montserrat font has no umlauts; a custom subset font could
  render them natively.
- Exact RCT Power device model — OIDs that never answer (e.g. battery values
  on an inverter without a battery) simply stay `--`.
- PCLK/porch values may need tuning for the specific panel revision.
- A "reconfigure" soft-reset path (button or hidden gesture) is not wired yet.
- Flow-line "animation" (moving dashes like the portal) can be added on top of
  the current direction/color indication once basic flows are confirmed.

## License

MIT for the project code (see `LICENSE`). The RCT protocol client is ported
from Energy2Shelly_ESP (Apache-2.0, see `NOTICE` and `LICENSE-APACHE`); the
ST7701 init sequence comes from Arduino_GFX/LVGL (MIT).