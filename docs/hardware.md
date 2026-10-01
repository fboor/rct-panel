# Hardware and bring-up

The developer's hardware notes for **rct-panel**: the pin map the firmware
uses, what has to be checked once when the panel is on the bench for the first
time, the quirks of this board, and what is still open. The user-facing
documentation is the [Benutzerhandbuch](benutzerhandbuch.md) (German), the
firmware's behaviour is in the `docs/` files next to it.

## Pin map

From the vendor board diagram, cross-referenced with the ESPHome device config,
the openHASP discussion #603, the HomeDing board page and the Tasmota discussion
#20527:

| Signal | GPIO |
|---|---|
| LCD DE / VSYNC / HSYNC / PCLK | 18 / 17 / 16 / 21 |
| LCD R0..R4 | 11, 12, 13, 14, 0 |
| LCD G0..G5 | 8, 20, 3, 46, 9, 10 |
| LCD B0..B4 | 4, 5, 6, 7, 15 |
| LCD init SPI CS / SCK / MOSI | 39 / 48 / 47 |
| Backlight | 38 (LEDC PWM, 1 kHz, active high) |
| Touch I²C SDA / SCL | 19 / 45 (GT911 @ 0x5D) |
| Switching output (1-Way relay port) | 40 |
| SD card | CS 42, SCK 48, MOSI 47, MISO 41 |

## What only the hardware can answer

The host tests (`tools/run_host_tests.sh`) cover the logic. These five need a
panel on the wall, in this order:

1. **Backlight:** GPIO 38 high → the panel lights up. Driven as PWM
   (`src/display/Backlight.h`), dimmed to 30 % after 3 min without a touch and
   switched off after 5 min; a touch brings it back and restarts both timers.
2. **ST7701 init:** a non-garbage image (LVGL's boot stripes) means the init and
   the RGB timings are right. A shifted or colour-swapped image means the porch
   values or the R/B pin order in `src/display/Display.cpp` /
   `DisplayPins.h` are wrong.
3. **GT911:** with the panel lit, tap a button. If nothing registers, the
   controller may need its config blob pushed over I²C; polled mode should work
   for OTP-configured modules.
4. **RCT link:** set host and port in the provisioning portal and watch the
   `RCT: grid ...` lines on the serial console. The frame checksum
   (`src/rct/RctCrc.h`) is the first thing that fails silently here — a wrong
   checksum makes a healthy inverter look unreachable.
5. **Relay:** the polarity of the 1-Way port, and the test button (5 s on,
   5 s off).

A healthy boot on the serial console (115200 baud) looks like this — the GUI is
up before the radio is:

```
RCT Power Panel boot
Relais GPIO 40: Funktion 'Ueberschuss', Schwelle 500 W, Aus bei Start ...
Backlight: GPIO 38 at 1000 Hz/10 bit, full 1024/1024
LCD: display ready
[diag] mem gui.setup      frei 246620 B, lvgl aus dem ESP-Heap (kein eigener Pool)
WiFi: trying saved network '...' ...
Touch: GT911 found at 0x5D
SD: Puffer 288 Zeilen / 24 h (85248 bytes, PSRAM)
WiFi: connected, RSSI -5x dBm, RCT host '...' port '8899'
WiFi: ip 192.168.1.228, gw ..., dns ...
SD: mounted at 4000000 Hz, self-test 3.7 kB/s, 15.9 GB frei (/hist/UPT-0.csv)
SD: history scan 21263 B -> 8495 B, 176 Zeilen, 48 ms
hist: 288 samples restored from SD log, 9 gap(s) totalling 6300 s of missing data
RCT: connecting to 192.168.1.83:8899 ...
RCT: grid ... load ... PV ... bat ... | 51/60 fresh
```

Two lines in there are expected and not faults: `/hist/UPT-0.csv` is the file
name before SNTP has given the panel a time (the month file follows), and
`gap(s) totalling … of missing data` counts the hours the panel was off while
the 24 h chart has no samples of its own.

When no saved network is reachable — first boot, a different network, or erased
NVS — the line `WiFi: starting 'RCT-Panel' provisioning access point ...`
appears and the GUI keeps rendering while the portal stays available at
`http://192.168.4.1`.

## Quirks of this board

- **Octal PSRAM.** `memory_type: "qio_opi"` is required in
  `boards/guition-esp32-s3-4848s040.json`; the default quad setting fails the
  PSRAM self-test on the 4848S040.
- **`lv_init()` first.** It resets the TLSF heap allocator; calling it late
  crashes inside `esp_heap_caps`.
- **`touchInit()` before `lv_indev_create()`.** Otherwise the wire and touch
  tasks deadlock on the I²C lock ("could not acquire lock").
- **The display correction.** The screenshot path applies the same colour
  correction as the panel (`dispCorrectPixel`), so a picture on the card matches
  what the panel shows.

## Open questions

- The sign of `battery.current` follows the rctclient documentation; it should
  be confirmed against the live device. *(Checked against the real device on
  1.10.2026: charging reads positive, 1 % SOC with −0.11 kW, i.e. negative
  power. The sign is right.)*
- The S0 meter (`io_board.s0_external_power`) is added to the PV total when
  present; whether the household load phases already include it depends on the
  installation (the portal shows a separate "+EXT." node in that case).
- `power_mng.bat_next_calib_date` is read as a Unix timestamp (as in the Home
  Assistant integration) and `prim_sm.island_flag` as a bitfield where bit 0 is
  the flag — a whole-register read gives 0x02 on a grid-connected device, which
  a non-zero test would report as an island. Both were confirmed against the
  live device: the raw value 0x00000002 with the island off.
- German labels are written without umlauts (ue/ae/oe) because the built-in
  Montserrat has none; the generated `_uml` fonts add the Latin-1 range, so
  this only affects the strings compiled into the firmware.
- PCLK and porch values may need tuning for other panel revisions.
- There is no "reconfigure" path other than powering up without a reachable
  network; the Service page links back to the portal.
- The flow lines show direction and colour, not moving dashes like the portal.

## Source layout

```
src/
  main.cpp              wiring: init, loop (LVGL tick, non-blocking Wi-Fi
                        provisioning pump, 10 s RCT poll)
  config/               settings, NVS, non-blocking WiFiManager provisioning
                        AP (saved Wi-Fi + rct_host/rct_port)
  rct/                  RCT Power TCP client (ported from Energy2Shelly_ESP)
  display/              ST7701 + esp_lcd RGB driver, GT911 touch, pin map,
                        backlight (LEDC PWM)
  gui/                  LVGL pages + left/home/right navigation, generated
                        fonts (see ../NOTICE for the font licenses)
  storage/              SD logger: CSV row, parked-row ring, screenshots,
                        the web interface's file and listing caches
  web/                  the web interface (pages.h holds its CSS)
  i18n/                 one language per build: German or English
  output/               the switching output
include/lv_conf.h       LVGL 9 configuration
boards/                 Guition board definition (16 MB flash, PSRAM)
partitions/             16 MB partition table (OTA-capable)
tools/                  host tests, the RCT simulator, the md2pdf renderer
docs/                   developer documentation and the user manual
```