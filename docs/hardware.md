# Hardware and bring-up

The developer's hardware notes for **rct-panel**: the pin map the firmware
uses, what has to be checked once when the panel is on the bench for the first
time, the quirks of this board, and what is still open. The user-facing
documentation is the [Benutzerhandbuch](benutzerhandbuch-de.md) (German), the
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
| Switching output (1-Way port, drives an external relay coil) | 40 |
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
5. **Switching output:** the 1-Way port puts 3.3 V on one header pin when the
   output is on (GPIO 40) and 0 V when it is off, so an external relay coil plus
   a flyback diode is what switches a consumer; the test button drives it for
   20 s.

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

## The second board: Waveshare ESP32-S3-Touch-LCD-7

Same firmware, second panel: 800 × 480 with an ST7262 instead of 480 × 480 with an
ST7701, and an IO expander for what the first board has on GPIOs. Its profile is
`src/board/waveshare_touch_lcd7/`, its build `pio run -e esp32-s3-touch-lcd-7`.

Waveshare sells four boards under that name and the revision matters: the 7B is
**1024 × 600** and has an extra output (EXIO6 = LCD_VDD_EN), while the RGB data
pins are the same to the number. A profile written for the wrong one builds and
puts an 800-line picture on a 1024-line panel.

| | Guition 4848S040 | Waveshare Touch-LCD-7 |
|---|---|---|
| Panel | 480 × 480, ST7701 | 800 × 480, ST7262 |
| Module | ESP32-S3-WROOM-1-N16R8, 16 MB / 8 MB PSRAM | same part |
| Pixel clock | 12 MHz | **16 MHz**, falling edge — 12 MHz is out of sync (colours wander) |
| Vertical blanking | 8 back / 8 front | **16 back / 16 front** — 8/8 gives a picture that jumps |
| Display | init sequence over a 3-wire SPI, plus RGB timings | **reset on EXIO3**, plus RGB timings — this controller has no init sequence |
| LVGL draw buffer | 24 lines, 23 040 B, internal RAM | 24 lines, 38 400 B, internal RAM |
| Backlight | GPIO 38, PWM | GPIO 6, PWM, **behind a hardware mod** |
| TF card | CS on GPIO 42, mounts at 4 MHz | **does not mount**: CS is EXIO4 and no GPIO is behind it |
| USB / CAN selector | — | EXIO5 has to be low, or the board sits in CAN mode |
| Log over USB | CH343 USB-UART on the UART pins | the ESP32-S3's own USB (`ARDUINO_USB_CDC_ON_BOOT=1`) |
| Switching output | GPIO 40 | EXIO2, once the enable net is cut |

Two entries in that table are the connector and the expander, and both were wrong
before this board was built for:

- **`ARDUINO_USB_CDC_ON_BOOT` is per board.** It puts `Serial` on the ESP32-S3's own
  USB, which is what the 7-inch connector is. The 4848S040 has a CH343 USB-UART on
  the ESP32's UART pins; with the flag its log goes to a port the board does not
  connect, and the panel goes silent while it runs perfectly well. The flag lives
  in the two 7-inch environments and nowhere else.
- **No IO expander answers on the board that was tested.** A bus scan over
  0x08–0x77 on GPIO 8/9 finds the GT911 at 0x14 and nothing else; the wiring and
  the address (0x20) are the vendor's. Consequences, all four on the expander:
  LCD reset, USB/CAN selector, card chip select, touch reset. **After every flash
  the panel has to be unplugged** — a flash resets the ESP32 only, and a panel
  without its reset shows a white screen while the firmware draws and the scan runs
  at its 37.8 frames per second.

Two things about this board are not visible in the table and decide how it is
built:

- **The enable net has to be cut.** The backlight's enable is an input while the
  board comes up, and the board's own pull holds it there; afterwards it is the
  driver's business. With the wire from GPIO 6 to it the light takes a PWM — but
  then EXIO2 has to be off that net, and the expander's direction is one direction
  for all eight of its I/O pins. One line in the profile says which of the two
  worlds the board is in: `BOARD_CH422G_LCD_DISP_CUT` 1 = cut, light on GPIO 6
  with PWM; 0 = factory board, light on EXIO2 without the three-minute step. Both
  build.
- **The switching output is not a GPIO, and not a coil either.** What is connected
  to it is a module whose input side is an optocoupler LED, so it is a logic input
  with a resistor in front of it; the coil current is drawn on the module's
  isolated side.

The CH422G driver is `src/board/Ch422G.cpp`; register map and pin roles come from
the vendor's `esp_io_expander_ch422g.c` and `esp_panel_board_custom_conf.h`. Two
things the vendor's code does that a comment here had wrong: `IO_OE` **set** means
the pins are outputs (`write_direction_reg()` sets it for any non-zero direction
mask, `read_direction_reg()` answers "all twelve" when it is set), and a
transmission without a data byte returns success in this core without touching the
bus — so an address probe that way finds every address there is.

## Which version of the board

Waveshare sells several things under this name, and the firmware is only right for
one of them. The vendor distinguishes **variants** on its product pages (two article
numbers for this one: 27078 with touch, 30406 without; separate pages for the 7B
and the 7C-BOX). **PCB revisions it does not publish** — but the schematic it
publishes carries its revision on the drawing, and that one says **V1.2**, which is
the board this firmware was measured on.

| Name | Resolution | Difference | What this firmware would do |
|---|---|---|---|
| **Touch-LCD-7, V1.2** | 800 × 480 | — | **this one**; everything below was measured here |
| Touch-LCD-7, **V1.1** | 800 × 480 | **RGB data bits on other pins, controller EK9716 instead of ST7262, IO expander at 0x24 instead of 0x20**, 8 MB flash | **builds and runs with wrong colours** — the pins are the same, the bits on them are not |
| ESP32-S3-LCD-7 (30406) | 800 × 480 | no touch panel or connector | boots, finds no GT911, keeps the light on (the dimming needs a touch) — but the profile claims a touch; untested |
| **Touch-LCD-7B** | **1024 × 600** | CH32V003 instead of the CH422G, adjustable brightness, own timings | **builds, runs, and puts an 800-line picture on a 1024-line panel.** Not supported, and the installer offers it under no name |
| Touch-LCD-7C-BOX | 800 × 480 | case version | presumably identical; untested |

The V1.1 row is from a third-party board list, not from the vendor — the vendor
publishes no V1.1 documentation any more. It is in the table because it is the one
difference that would make a V1.1 board look broken in a way that has nothing to do
with the firmware: same sixteen pins, different colour bits on them.

Two more facts from the V1.2 schematic, both of which are about this board's
hardware rather than about its software:

- **GPIO 6 is the sensor input** (the "AD" net on the three-pin sensor header). The
  backlight mod puts a PWM on that pin, which works — and costs the sensor input.
- The chip on the schematic is an **N8R8** (8 MB flash, 8 MB PSRAM) on the printed
  sheet, while the board we have answers 16 MB flash and quad-spi PSRAM. Vendor
  documentation and module marking disagree here; the device says 16 MB.

What the same document, Espressif's own FAQ on the RGB panel, says about the
symptom this board showed (it calls it **drift**), and it is worth having next to
the measurements above:

| | |
|---|---|
| A cause | flash and PSRAM share the SPI interfaces — **PSRAM is disabled while the flash is written**, which Wi-Fi and every update do |
| Highest pixel clock at **octal** PSRAM, 80 MHz | about 22 MHz |
| Highest pixel clock at **octal** PSRAM, 120 MHz | about 30 MHz |
| Highest pixel clock at quad PSRAM, 80 MHz | about 11 MHz |
| The fix the vendor and the FAQ both name | octal PSRAM at 120 MHz + XIP from PSRAM + the RGB driver's bounce buffer |

This build runs octal PSRAM at 80 MHz — that is what the board file asks the
Arduino core for, and 120 MHz is a menuconfig switch in ESP-IDF. At 16 MHz pixel
clock the panel is inside the 22 MHz that 80 MHz allows, which is why the picture
holds still now; the bounce buffer is not in the linked `esp_lcd` either. Both of
the named fixes live in ESP-IDF, not in an Arduino build.

## What the 7-inch panel's own screen said

Four builds, four different symptoms, and the pin set of every one of them matches
a configuration that drives this board steadily — red 1/2/42/41/40, blue
14/38/18/17/10, green 39/0/45/48/47/21, HSYNC 46, VSYNC 3, DE 5, PCLK 7, inverted
edge.

| Pixel clock | Vertical blanking | What the panel did |
|---|---|---|
| 16 MHz | 8 / 8 | picture jumps |
| 12 MHz | 8 / 8 | colours wander |
| 12 MHz | 16 / 16 | colours wander — so 16 MHz is the panel's floor, not a preference |
| **16 MHz** | **16 / 16** | **picture still, colours right** |

The jumping is not the write. It is the scan out of PSRAM: 800 · 480 · 2 · 37.8 =
29 MB/s of continuous read, out of the same memory LVGL renders into and the network
stack works in. A line that arrives late is what a panel shows as the picture
jumping, with the colours left alone — which is how a late line is told from a
panel out of sync, which takes the colours with it.

| Redraw of the whole screen | PSRAM written |
|---|---|
| two LVGL buffers in PSRAM | 2.3 MB |
| one LVGL buffer in internal RAM | **1.5 MB** |

| Way out that is not available | |
|---|---|
| pixel clock below 16 MHz | the panel goes out of sync |
| frame buffer out of the PSRAM | 768 kB against 512 kB of SRAM |
| bounce buffer or second frame buffer | the linked `esp_lcd` has neither |

The flush callback waits for one thing only: that the beam is not on the strip it
is writing. Behind the strip the change appears a frame later, ahead of it in this
frame; only writing while the beam is on it shows half old and half new. The beam
is on a 24-line strip for 24 of 516 lines, so that wait is about 1.3 ms.

The touch controller's X_MAX/Y_MAX registers (0x8148) are read and used to scale
onto the panel, but only when they can be a range at all — both sides non-zero and
within a factor of two of the panel. This board's controller reads **480 × 0**, a
range nobody configured it with, so its numbers are the panel's own. That is worth
knowing against a fixed 480 in the driver: on this board that made everything right
of x = 480 a dead zone, half the screen, with the right navigation button in it.

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
- PCLK and porch values are measured on the 7-inch board above; other panel
  revisions may need their own.
- There is no "reconfigure" path other than powering up without a reachable
  network; the Service page links back to the portal.
- The flow lines show direction and colour, not moving dashes like the portal.

## The front plate (planned)

The module as sold is a bare display module in its own plastic case. The plan
is to stop using that case: a **front plate** takes the touch panel and nothing
else, and the board gets its own housing behind it. Two drawings, both
dimensioned in mm, the second one at 1:1 for printing:

| File | What |
|---|---|
| `img/frontplatte.svg` | front view with dimensions, section A–A, legend, colour variants |
| `img/frontplatte-1zu1.svg` | front view at 1:1, with the glue ring and the cut edge filled in |
| `img/frontplatte.dxf` | DXF R12 in mm for the laser cutter, four layers: `SCHNITT`, `GRAVIEREN`, `MARKIERUNG`, `RASTER` |

Visicut imports DXF (and the 1:1 SVG, but that one has filled shapes and the
DXF is unambiguous). The geometry is nominal, so let Visicut compensate the
kerf. The four layers are four operations:

| Layer | Geometry | Operation |
|---|---|---|
| `SCHNITT` | outer contour 120 × 200 (r 8) and the 81 mm window (r 2) | cut through |
| `GRAVIEREN` | 84.3 mm outer (r 2.2) and the 81 mm window, as two closed contours | fill the ring between them and engrave it **0.45 mm deep** — this is the glue surface, 1.65 mm wide |
| `MARKIERUNG` | four crosses at the window corners | align on the bed |
| `RASTER` | 16 closed loops at 0.1 mm spacing inside the ring, each a rounded square like `GRAVIEREN` | trace the ring loop by loop, for machines that cannot fill an area — same 0.45 mm depth as `GRAVIEREN` |

Two things to know about the `GRAVIEREN` layer. The depth is not in the file —
DXF R12 has no depth for an entity, so 0.45 mm goes into the machine's job
parameters. And the machine has to *fill* the area between the two contours
rather than follow them: a laser that only traces a path would burn two lines
where the pocket should be. If it cannot fill, the `RASTER` layer carries the
ring as 16 closed loops — the `GRAVIEREN` shape shrunk by 0.1 mm per loop, each
traced as one continuous path so the head never reverses. Run it as an engrave at
the same 0.45 mm depth instead of the fill, and `GRAVIEREN` stays in the file
untouched as the contour reference. The alternative if the machine can do
neither: mill the recess, or print the plate in PETG.

Without the recess the panel sits 0.3 mm proud of the front and the glue seam
becomes a visible hairline at 42 mm from the centre — which is why the recess is
a separate object rather than a fourth contour in the cut layer.

The numbers that come from the device, not from a datasheet:

| Measured | Value |
|---|---|
| Touch panel | 84 × 84 × 0.3 mm, laminated to the LCD |
| Active area (vendor) | 71.8 × 70.2 mm, centred in the panel |
| Plastic frame around it (vendor) | 86.5 × 86.5 mm, 13.6 mm visible lip |

The plate: 120 × 200 × 6 mm with the display at one end (18 mm margin on three
sides, 98 mm at the other end, which is still free). The panel sits in a
0.45 mm deep recess 84.3 mm square, its front face flush with the plate's front;
the 0.15 mm gap between panel and recess floor is filled with two-component
epoxy, which leaves a 1.65 mm ring to glue on and hides the seam under the
black border. The 81 × 81 mm window is cut behind that ring, so no plate
material ends up between the panel and the LCD — a full-width recess would
leave 6 mm of air in the optical stack.

The plate carries no mounting of its own: the housing hangs on the wall and the
plate is only the fascia in front of it. That frees the thickness as well — 6 mm
is what the recess and the stiffness want, not what the load needs. What the
plate does need is something the housing can grip, in the free area, that is
not a through-hole and never touches the panel: blind pockets for clips, or a
small rim on the back edge. The colour variants are drawn the way the front
reads: the display area stays whatever the panel is, the glue ring gets the
epoxy's colour, the outer 3 mm get a colour of their own.

Open for the housing: the depth behind the panel (the module is 37.8 mm deep
with its case), the glass corner radius (r 2 assumed), what the 98 × 108 mm of
free plate below the display is for, how the housing holds the plate without
touching the panel, and where the cables leave towards the wall.

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
tools/                  host tests and the md2pdf renderer (the RCT simulator
                        lives outside the repository, see ../rct-panel-simulator)
docs/                   developer documentation and the user manual
```