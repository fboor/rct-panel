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
  - **Energy** — consumption / feed-in counters (kWh).
  - **Info** — link state, host, port, last data, uptime, per-phase grid
    power (L1/L2/L3), PV total, household load, battery SOC/power/current/
    voltage.
- **Provisioning:** first boot (or no saved Wi-Fi) starts the **RCT-Panel**
  access point with a captive-portal web page at `http://192.168.4.1` where
  the Wi-Fi credentials and the **RCT host / port** are entered. Settings are
  kept in NVS.
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

To reconfigure: with the device booted, reset it **and hold the BOOT button**
during the portal window (or erase NVS with `pio run -e esp32-s3 -t erase`).

## Source layout

```
src/
  main.cpp              wiring: init, loop (LVGL tick + 5 s RCT poll)
  config/               settings, NVS, WiFiManager portal (rct_host/rct_port)
  rct/                  RCT Power TCP client (ported from Energy2Shelly_ESP)
  display/              ST7701 + esp_lcd RGB driver, GT911 touch, pin map
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
| Backlight | 38 (on/off, active high) |
| Touch I²C SDA / SCL | 19 / 45 (GT911 @ 0x5D) |
| SD card (unused) | CS 42, SCK 48, MOSI 47, MISO 41 |

Bring-up checklist (in priority order) once you have hardware:

1. **Backlight:** GPIO38 high → panel should light up (even with garbage).
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

The OLED-less boot sequence shows splash text while WiFiManager provisions,
then the live pages.

## Roadmap / open questions

- **On-hardware verification of the flow signs:** battery.power
  (`g_sync.p_acc_lp`, + = charging) and grid + (Bezug) follow the rctclient
  docs; the battery.current sign should be confirmed against the live device
  once hardware is available.
- The S0 meter (`io_board.s0_external_power`) is merged into the PV total when
  present; confirm whether the household load phases already include it for
  your installation (the portal adds a separate "+EXT." node in that case).
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