# Web interface in normal operation

In addition to the setup portal (Wi-Fi "RCT-Panel", 192.168.4.1) the panel runs
its own web server on port 80 in normal operation: status page with energy bars,
history with charts, CSV data and screenshots from the SD card, firmware update by
upload.

## Why an own server instead of WiFiManager

The setup path stays WiFiManager, but the pages in normal operation are our own.
Reason: `WiFiManager` keeps its server in a private
`std::unique_ptr<WM_WebServer>` (`WiFiManager.h`), so the root handler cannot be
replaced. In portal mode `/` delivers the **Wi-Fi form**, and submitting it calls
`connectWifi()` — a normal call to `192.168.x.x` would thereby throw the panel out
of the home network. An own server avoids that without patching anything into the
library.

Second condition from the same source: `startConfigPortal()` only checks
`configPortalActive`; a foreign server on port 80 does not disturb it. Hence
`startProvisioningAp()` / `restartProvisioning()` in
`src/config/Configuration.cpp` call `webStop()` beforehand — the two servers share
port 80 and the radio, never at the same time.

## Routes

| Route              | Kind           | Purpose                                              |
| ------------------ | -------------- | ---------------------------------------------------- |
| `/`                | GET            | Overview: one card per meter of the device, energy bars, device details |
| `/einstellungen` | GET / POST    | Device type, address, port, switched output, theme, maintenance; POST saves to NVS and restarts |
| `/verlauf`        | GET            | 24 h and the history: line or band chart, period selection |
| `/api/energie.json` | GET          | the energy figures of one period as numbers (`?zeitraum=tag\|monat\|jahr\|gesamt`) |
| `/api/verlauf.json` | GET          | the 24 h ring from RAM as numbers                     |
| `/daten`           | GET            | list of the CSV files in `/hist`                      |
| `/daten/<name>`    | GET            | one CSV file, `?tail=<bytes>` for the last n bytes    |
| `/bilder`          | GET            | list of the screenshots in `/shot`                    |
| `/bilder/<name>`   | GET            | one screenshot (BMP)                                  |
| `/update`          | GET / POST     | Firmware update                                       |
| `/aktion`          | POST           | `was=neustart`, `was=setup`, `was=ausgang`, `was=test`, `was=bild` (all with the code) |

Everything else: 404 page.

## What works without the code and what does not

Reading (overview, lists, files) is open — that is the purpose of the page.
Everything that changes the panel requires the 4-digit code:

* firmware update (`/update`)
* restart (`/aktion`)
* setting up Wi-Fi again (`/aktion`)
* function and threshold of the switched output (`/aktion?was=ausgang`)
* test of the switched output (`/aktion?was=test`)
* triggering a screenshot (`/aktion?was=bild`)

The code is drawn anew on every start (`esp_random()`), stands on the panel's
**Service** page and can be entered there: draw a new one if somebody could read
it. It is **not** stored in NVS and therefore does not survive a restart — that is
deliberate, an old code should not be reusable out of a logbook.

The code is checked **before** anything is written, not afterwards: the form on
`/update` puts the code field **before** the file field, because the web server
evaluates the parts in order. At `UPLOAD_FILE_START` the field is therefore
already there, and a wrong code does not write a single byte to flash.

## The chart pages: the browser draws

**The panel draws nothing.** It delivers numbers, the browser turns them into bars
and lines. The reason is memory, not convenience:

| Resource                    | Value                          |
| --------------------------- | ------------------------------ |
| internal heap in operation  | 120 764 bytes free             |
| PSRAM                       | 7 588 299 bytes free           |
| Flash                       | 5.9 MB free                    |
| card                        | ~470 kB/s measured             |
| streaming a whole month file | works today, around 3 s        |

A server-rendered image would be a draw buffer plus PNG encoding (~0.5 MB PSRAM)
and several seconds of compute per call. On a device with 480 × 480 pixels that is
pointless besides, because the charts are as sharp and as large as the window you
look at them in.

The measurement above is from 2026-10-02; it is in the boot log, with the `stall`
counter next to it, because that is the only number here that moves immediately
with every change.

### Two endpoints, both from RAM

`/api/energie.json?zeitraum=tag|monat|jahr|gesamt` delivers the five energy values
of a period and the two percentages:

```json
{"tz":"CET-1CEST,M3.5.0,M10.5.0/3","period":"day","unit":"Wh",
 "values":{"pv":23680,"own":2080,"feed":15890,"draw":1810,"load":3890},
 "autarky":53.5,"ownShare":11.6}
```

`/api/verlauf.json` delivers the 24 h ring that the panel page *24 h Verlauf*
draws:

```json
{"tz":"...","points":288,"series":["grid","load","pv","ext","battery","soc"],
 "unit":["W","W","W","W","W","%"],
 "data":[{"t":1790875294,"v":[82,1214,750,0,-480,55]}, null, …],
 "from":1790875294,"to":1790884294}
```

Four rules with that, each one a place where something can go wrong:

1. **Integers, no exponent.** The device's counters are whole watt-hours;
   `snprintf("%g")` would have made `1e-05` out of a small number. The formatting
   stands in `src/web/Json.h`, without Arduino dependency, and is checked in
   `tools/json_test`.
2. **`null` instead of `nan`.** A NaN reaches `snprintf` as `nan`, and a JSON
   parser stops on it — the rest of the answer would be gone with it.
3. **`tz` is the POSIX rule, not an offset.** The panel sets its time with
   `configTzTime(kTimeZone, …)` (`src/config/Configuration.h`), so with daylight
   saving. A fixed offset would be an hour wrong from the last Sunday in October
   on, and the day boundaries of the whole history would shift by exactly one day
   then. The browser computes the rule itself; the computation is checked in
   `tools/jstest` against values from Python's `zoneinfo` (every sixth hour of 2026
   and every hour around both changeover days).
4. **`data` is flat and in ring order**, oldest point first. A gap is `null` and
   not six zeros — that way the line breaks where nothing was measured instead of
   crossing a period in which nothing happened.

The numbers come from the same computations that fill the display pages:
`guiEnergyPeriod()` calls `energyPeriodValues()`, `guiHistoryPoint()` reads the
ring that `histPush()` writes. Page and JSON therefore cannot drift apart — the
same discipline as `src/DataStatus.h` for the badge on panel and web.

The ring is read point by point, not copied: 288 × 6 values plus timestamp would
be 8 kB of RAM for a single request, and the answer is gone afterwards. The JSON
answer itself is a `String` with a single reservation for the whole length.
Measured on a filled answer from RAM: **16 kB** for the ring (288 points, `?k=`
only against the cache), **163 bytes** for the energy values. Both go over Wi-Fi
in one go; the effort is more in formatting than in sending.

### The same number on both sides

Both sides compute from counters, not from momentary values — only from different
ones:

* the **overview** reads the counters the device reports itself
  (`energy.e_dc_*`, `e_load_*`, `e_feed_*`, `e_grid_*`) — day, month, year and
  lifetime, as the inverter keeps them;
* the **history page** forms the difference of these counters in the recorded file
  between the first and the last row of the period.

Within one day both name the same. Measured on 2026-10-03 at 01:52: overview
709 Wh consumption for the day, file 603 Wh up to the last sample at 01:38 — the
106 Wh are the fourteen minutes in between.

Two cases in which they diverge, and both lie in the file, not in the display:

* **A period in which the file has no row at the edge.** The difference then
  starts with the first and ends with the last sample, not with the beginning of
  the day. On the development card that is the case for October: the month file
  begins on 2026-10-01 at 19:21 (the card was not in this format before), the
  device's month counter covers the whole month — 35 184 Wh against 23 684 Wh of
  difference in the file.
* **A month whose first rows are still in the old format** — see the next section.

The S0 share is **not** extrapolated from the momentary power. `ext_total_wh`
counts the *generation* at that input, and a plant without generation there simply
has no share in the sums — even though the EXT line in the chart shows the
consumption at the same input (`io_board.s0_external_power`, a momentary value). On
the development device that is exactly so: over 30 h and 338 samples not a single
change of `ext_total_wh` (fixed at 1 545 861 Wh), while `s0` was non-zero in 117
samples.

That the sum **contains** the S0 can be read off the lifetime counters: the
device's total sits exactly 1 545 860 Wh above the sum of the two CSV strings
`pv_a_total_wh + pv_b_total_wh`, that is by the amount of the S0 counter. It is
added exactly once, on both sides: in the panel in `energyPeriodValues`
(`pv += ext; load += ext;`), in the browser in `rpEnergy`
(`pv = Δpv_a + Δpv_b + Δext`, `load = Δload + Δext`).

### The cards follow the device

The cards (grid, PV, battery, card; on an RCT Power all four) are built from the
capabilities of the device, not from a fixed list: a device without household
meter, battery or grid meter gets only the cards for which there are values. The
reason is the same rule as on the panel — a card for a meter that does not exist
would be a number without meaning.

The grid is `repeat(auto-fit, minmax(140px, 1fr))`, so it fills the width with as
many cards as fit, and the cards keep the same size whether there are four or one.

### The energy bars on the overview

Below the cards stand five bars (generation, own consumption, feed-in, grid draw,
consumption) with the value as text above them and a period switch **Tag | Monat |
Jahr | Gesamt** above that — wording, colours and order as on the panel page
*Energie*.

They are fetched **once**, not reloaded. Reason: further down on the same page
stands the form for the switched output's threshold, and a page that reloads itself
overwrites what somebody is typing. The values are counters, a page from ten
minutes ago is at worst ten minutes old, and reloading is one tap.

### The history page

Four areas, one state:

* **24 h** from `/api/verlauf.json`, without card access. Updates itself every
  5 s — unlike the overview there is nothing on this page that somebody types into,
  and the newest sample comes every five minutes.
* **Day** from the same month file, as a line: 288 points, one sample every five
  minutes, a missing sample a gap in the line.
* **Week** and **month** as a **band per day** (daily minimum to daily maximum).
  Five-minute points over a month as a line through points would be an invented
  precision; a band says what the day really delivered. The six bands stand side by
  side instead of on top of each other, otherwise they would hide each other. A day
  for which the file has no row gets neither band nor label; the label at the
  bottom counts the days **that exist**, not the slots — otherwise two missing days
  in a month would put two dates on top of each other.

On top a navigator (‹ ›) over the periods, with the date in the middle. The week
begins on Monday, because that is the German usage; the year appears in the middle
only when it changes.

### The pointer on the chart

`rpChart()` gets `{t, v[6]}` per sample (line) respectively `{t, lo[6], hi[6]}`
(band) from `/api/verlauf.json` and computes the scales `xOf`, `yOf`, `ySoc` from
it. Everything the pointer needs afterwards, it puts as `rpCtx` **on the element**
— not into a closure: the drawing is rebuilt every 5 s, the pointer is not.

* The pointer's position goes through `svg.getScreenCTM().inverse()`, not through a
  division by the width. The SVG keeps its proportions and is capped at 380 px, so
  on a wide screen it sits centred with margins; a division would be half a chart
  off there.
* The crosshair is built **once per drawing** as a `<g>` with one line and one dot
  per line (`createElementNS`, because a piece of markup inserted as HTML into an
  SVG lands outside the drawing) and afterwards only **moved**. A redraw can then
  only change attributes, nothing can pile up, and going away is a `display="none"`
  on the group instead of a search through the drawing for what has to be removed
  again.
* The pointer is served by a **single `pointermove` hung on `document`**; a
  listener per drawing would die every 5 s with its element. Leaving is caught on
  `pointerout`, **not** on `pointerleave` — the event does not bubble, a listener
  on `document` would only see it when the pointer leaves the window, and the
  crosshair would then stand over the chart for good. Two cases are excepted:
  `relatedTarget` (the pointer only walked from one element to another; the
  `pointermove` handles that) and `pointerType == 'touch'` (after a tap the
  pointer is gone and the values have to stay until the next tap).
* A value that does not exist in that sample **gets no dot** and a dash in the
  box. A dot at the last known value would be a number nobody measured.

The test for this stands in `tools/jstest` (block “the crosshair under the
pointer”) with a stubbed DOM: a hundred pointer movements must leave **one** group
in the drawing, and the redraw must deliver a new group with the crosshair at the
pointer's sample.

### How the browser computes from the CSV

**The energy of a period is the difference of the lifetime counters between its
first and its last row** — not the sum of momentary values. That is exactly the
quantity the device counts itself, and it stays correct across a gap. The external
generator counts towards the generation and towards the consumption (the same
computation as `energyPeriodValues`). The feed-in counters arrive at the device
negatively, therefore the magnitude is taken — in one place, not six times.

The **own consumption is consumption minus grid draw** — the same computation as
`rulePeriod` on the panel, and for the same reason: it is counted on the way out
and not on the way in, so that one day does not tell the charging of another. The
three bars therefore do not add up; the difference is the battery charge plus the
conversion losses, and no counter carries either. `rpEnergy()` gets `ownOk` from
the page's `data-own` attribute for that: the difference needs both meters, and
without them there is no own consumption, no self-sufficiency and no share — three
`null`, out of which the browser makes nothing. The **own-consumption share has
the denominator own consumption + feed-in**, not the generation.

The S0 share is **not** extrapolated from the momentary power. `ext_total_wh`
counts the *generation* at that input, and a plant without generation there simply
has no share in the sums — even though the EXT line in the chart shows the
consumption at the same input (`io_board.s0_external_power`, a momentary value). On
the development device that is exactly so: over 30 h and 338 samples not a single
change of `ext_total_wh` (fixed at 1 545 861 Wh), while `s0` was non-zero in 117
samples.

The six series are the same as in `csvrow::toSample()`: the inverter's load meter
has already subtracted the S0 counter, therefore the consumption is meter plus
external, and the generation is both strings together.

The column names come from `csvrow::kHeader` and stand as `data-cols` in the HTML;
the browser reads the rows **by name**, not by position. A new column in the CSV
therefore changes nothing on this page, and unknown columns are ignored instead of
guessed.

The month files are fetched **one after another**, not in parallel: the panel
serves one download at a time and answers a second one with “busy”. They then stay
in the browser's memory, keyed by their **full file name** — an `RCT-202609.csv`
and an `RCT-202610.csv` are different files, and a cache keyed by month alone would
return the wrong one. Anyone who pages back three months afterwards has a ~1.2 MB
file in their phone and not one single further access to the panel.

### Files in the old format

Month files that were created before the switch to 23 columns carry 16 names above
the rows. Behind the header line there can nevertheless be rows with 23 values —
**that is the normal case and not the exception**: on the development card
`RCT-202610.csv` has 231 rows with 16 and 333 rows with 23 values. The decision is
therefore made **per row**, not per file; a message from the header line would name
days without sums that do have them.

The browser fills the missing sums with **0** — exactly as the panel's reader does
(`csvrow::parse()`: a counter that was not logged reads as 0, and only the one
place that knows that may say so). That keeps the view filled throughout and lets
the browser compute without a special case. So that the zero is not read as a
measurement, a sentence stands above the chart as soon as **in the selected
period** there is a row without sums:

> Parts of the rows have no sums (from before the update): days entirely before it
> show 0, a period across the change begins with the first row that has sums.

Three cases, and none of them invents a number:

* **The period has sums everywhere.** The difference between its first and its last
  row — the same quantity as in the chapter about the energy page.
* **The period has none.** Then there is nothing to subtract: the bars show 0 and
  the two percentages stand as **–**. A period without counters has no share, and
  “100 % own consumption” would be an answer to a question nobody asked. That also
  covers the first day after the update, when it has only a single row with sums: a
  counter needs two readings before it says anything.
* **The period has both.** Then the difference runs from the first row **with**
  sums to the last with sums, not from the first row of the period — otherwise it
  would be the difference between a counter and a zero, that is its whole life
  instead of the energy of this period. What is missing at the beginning of the
  period stands in the sentence above the chart.

For an installation that gets the firmware later, the case does not exist.

### Gaps

Counted as on the panel (`histPush` in `src/gui/GuiApp.cpp`): a sample that comes
more than one and a half intervals late means that the slots in between were
never written. The same sentence is shown as on the panel (“9 gaps, 110 min
without measurements”, `T_D_GAP_MANY`). Without that line a pause in the recording
reads like a collapse.

In the 24 h view the gaps are the empty slots of the ring, therefore counted
directly — the same number the panel shows under its chart.

### What it costs and what does not work

* **No reloading on `/`.** For the reason above.
* **No server rendering, no library, no CDN.** The page runs in the local
  network; loading from the internet would break exactly the page that shows
  whether the panel is still alive. Hand-written SVG and DOM, together in
  `src/web/pages.h`: 18.4 kB CSS, 11.8 kB logic, 17.8 kB script. In the flash
  accounting that is one line (5.9 MB free); the real price is testability, not
  space — and that is paid for with `tools/jstest`.
* **No texts in the script.** Labels, headings and the error sentence come as
  `data-*` attributes from the firmware, otherwise a word could be spelled
  differently on the page than in the language table. The same holds for the date
  format (`{D}.{M}.{Y}` or `{Y}-{M}-{D}`), the separator and the six series
  colours.
* **Units on the axes, because there is room here.** Every scale mark on the left
  carries the unit behind the number (`10.0 kW`), and on the right at the **same
  height** the state of charge — written as one would write it in running text.
  The state of charge runs over the full height from 0 % to 100 %, which is why
  next to the zero line stands the battery's level at that moment (here around
  50 %), at the top 100 %, at the bottom 0 %. The unit comes from the panel's
  answer, so it is the one it sent. On the 480 pixel display there is no room for
  that; the marks stand there without a unit, the legend names the series.
* **The interface is blocked during a file transfer.** At the measured rate of
  ~470 kB/s the ~1.2 MB of a month file are around 2.6 s of reading time during
  which operating the panel waits — the same work during which the interface
  already stalls for 4.4 s while writing the CSV row. The 24 h area is not
  affected: it comes from RAM.
* **A period with two month files** (a week across the month boundary) loads both,
  the second only when the first is finished.

### Checking

* `tools/jstest` cuts the logic block out of `src/web/pages.h` and runs it in node
  — the **same** code that runs in the browser, not a copy. Checked are the time
  zone rule against `zoneinfo`, reading the CSV, the day ranges, the month
  computation and the energy difference on the numbers from 2026-10-02 (23.68 kWh
  generated, 15.89 kWh fed in, 2.08 kWh own consumption, 1.81 kWh grid draw,
  3.89 kWh consumption).
* `tools/json_test` checks the number formatting of the answers.
* The pages were looked at in a browser against both column formats at 360 px and
  1024 px width, in both language versions.

## Data from the SD card: stream instead of file in RAM

The card belongs to the worker from `src/storage/sdlog.cpp`; the web server never
touches it. A download is a handshake over three calls:

```
sdRequestStream(path, tailBytes)   // request, returns immediately
sdStreamTotal()                    // byte count, as soon as the worker has opened the file
sdTakeStreamChunk(buf, max)        // 0 = not done yet, -1 = done/error
sdStopStream()                     // abort
```

The worker fills **one** 16 kB buffer per pass and only if the previous one has
been collected. A browser that reads slowly thereby holds the worker back — finite
buffers, no growth. The other way round, a parked 5-minute CSV row waits at most
one buffer (≈40 ms at 4 MHz); logging and download do not starve each other.

`Content-Length` is the real file size, not what was read: an aborted download is
therefore a failed transfer in the browser and not a silently truncated file.

### Two things the library would otherwise have ruined

1. **Five seconds.** `WebServer::handleClient()` leaves the connection open for
   `HTTP_MAX_DATA_WAIT` = 5000 ms and then closes it. A whole CSV month file takes
   longer. While a download runs, `handleClient()` is therefore **not** called —
   only that keeps the socket open. Price: during a download the panel serves no
   second request. A directory with 999 screenshots does not fit into a 2 kB
   buffer; the page says so instead of outputting a shortened list as a complete
   one.
2. **`sendContent()` returns nothing.** In this core version the return value is
   `void`; a partial write (full TCP window) would pass as data loss. The body
   therefore goes through its own copy of the client: `WiFiClient` is
   reference-counted (a `shared_ptr` socket handle), the copy talks to the same
   socket and `write()` delivers the number really written. That also makes the
   case “browser went away” detectable at all (4 s without progress → abort the
   stream, release the card).

A write never blocks: `WiFiClient::write()` uses `select()` with a 35 ms timeout
and `MSG_DONTWAIT`, at most four attempts — in the worst case ~140 ms, and only
while a browser does not read.

## Directory listing

`sdRequestListing(dir)` / `sdTakeListing(out, cap)` — one line per entry,
`name|size|epoch`, in directory order. Also via the worker, for the same reasons.
The page does not hold the answer: the handler requests and returns without an
answer, rendering happens as soon as the worker has it (the connection lives 5 s,
the worker needs few milliseconds). After 4 s without an answer: 503.

The buffer is 2 kB (~60 lines). If it fills in the middle of a line the page says
so — but the check for the last character has to happen **before** rendering: the
renderer replaces the line breaks in the buffer with `\0`, after which the last
byte is always `\0` and the message would stand on every page with files.

## Triggering a screenshot (`/bilder`)

Below the image list stands a form (code + **Screenshot auslösen**) that goes to
`/aktion?was=bild`. `guiRequestShot()` takes the capture **without** the 5 s
delay of the panel button: that delay exists because you first have to page to the
right page — in the browser the desired page is already the visible one.

The answer is a 303 to `/bilder?neu=1`, and `?neu` counts the steps of the
**one** reload that belongs to one capture:

| Step | Address         | What happens                                        |
| ---- | --------------- | --------------------------------------------------- |
| 1    | `/bilder?neu=1` | the same list as before, plus a meta refresh after 6 s |
| 2    | `/bilder?neu=2` | **one** reload that reads the card anew — after that the page stays still |

6 s, because writing 691 kB at 4 MHz was measured at 3 s and (on a card that wrote
the CSV row in parallel) needed 5 s. Step 2 reads the directory with
`sdRequestListing(dir, /*force=*/true)`: without that, the up-to-5-second-old list
buffer would still withhold the file just written. If the writing is then still
running (slow card), step 2 asks again, at most `kShotReloadMax` (8) steps — after
that the button **Seite neu laden** is there. A page that reloads itself endlessly
is unreadable; hence exactly one.

An untruncated write is ruled out: `sdWorkerWriteShot()` checks every `write()` and
repeats a short one up to four times, then compares the file size on the card with
the intended one and **deletes** a file that is too short. Measured on the wall, 3
of 8 captures were short (0, 167 kB, 499 kB of 675 kB), while the return value went
unread and the log line reported the target size every time. A too-short file
counts in the web server as “does not exist” (404), not as a read error of the
card.

## Firmware update (OTA)

`Update.begin/write/end` from a single context (`loop()`), because the `Updater`
has no mutex of its own. The updater deletes lazily: `begin()` only chooses the
target slot and allocates 4 kB, the deleting happens per written 64 kB block in
`_writeBuffer`. The longest single system call is therefore a block delete in tens
of milliseconds, not a multi-second partition erase — the panel stays operable
while writing.

A failed update bricks nothing: the image is checked magic-byte-wise before the
boot, the other 7 MB slot stays untouched, in the worst case the panel boots the
old firmware.

Rejected before the first written byte: the code and a file name without `.bin`.
Two properties of this web server version made the update **always** fail before,
without a single byte written:

* `HTTPUpload::totalSize` is **0** at `UPLOAD_FILE_START` — the library only sums
  the chunk sizes while iterating and does not know the request's
  `Content-Length`. `Update.begin(0, ...)` is an `UPDATE_ERROR_SIZE`, so the log
  always said “Update rejected (size does not match)”. Now
  `Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)`: the whole slot, and `end(true)`
  cuts the image to what really arrived. The size limit is therefore checked
  **during** the writing (`s_otaBytes + currentSize > kMaxFirmware` →
  `Update.abort()`), not before.
* `HTTPUpload::name` is the **form field name**, not the file name; for this form
  both are `fw`. The `.bin` check belongs on `filename`, otherwise every file is
  rejected (log: “Update rejected (no .bin file)”).

Both verified on the wall: 1 472 560 bytes in 4534 ms, then a restart with
`rst:0xc`, new maintenance code, card and values there again.

## Switched output

The start page carries state and function of the output at the top, the form
below: selection field for the function (5 values), number field for the threshold
in watts, and two buttons — `was=ausgang` (apply) and `was=test` (5 s on, 5 s off).
The rules behind it stand in `docs/relay.md`.

`was=test` answers immediately and starts the test in the background: the browser
would otherwise wait 20 s for an answer it does not need. A second test start
during a running test is a 409, not a second test.

`was=ausgang` reads both fields and saves the threshold even when the selected
function has none — so the value is still there when switching back to the
threshold functions. The Service page can choose the same function by tapping, but
cannot set a threshold: numbers in watts need a keyboard, and the panel only has a
touchscreen.

## Language

The visible text stands in `src/i18n/`, one table per language, and is chosen at
build time: `pio run -e esp32-s3` is German, `pio run -e esp32-s3-en` is English
(`-DRCT_LANG_EN`). There is no switch at runtime — the display pages are built
once in `guiStartApp()`, and a second table would be a second state for a few kB
of text that one cannot check on the wall.

For the web pages that means: `tr(T_...)` instead of a German literal, and the
`<html lang>` value comes from the same table (`T_HTML_LANG`). Two conventions in
the tables: web texts carry HTML entities (`&uuml;`), display texts are normal
UTF-8 (Montserrat has the umlauts). This is checked in `tools/i18n_test`: same IDs
and same placeholders on both sides, no German letter in `src/web/` and
`src/storage/`.

For the chart pages the same way through the browser: the words, the date format
and the separator stand as `data-*` attributes in the HTML, which the script
reads. A word that stood in the script would be a second place where it can go
wrong at the next translation.

Not translated are the serial protocol (developer text, stays as it is) and the
CSV column headers (`ts,pv_a,...` — a table in Excel may not change its columns
with the display language).

## Buffers and memory

* 16 kB stream buffer from PSRAM (`heap_caps_malloc`, fallback to internal RAM)
* 2 kB list buffer in the worker
* 2 kB send buffer in the web server
* PROGMEM pages (`src/web/pages.h`), per request ~2-3 kB `String` in RAM
* JSON answers: 163 bytes for the energy values, ~16 kB for the 24 h ring, each
  with a single reservation for the whole answer

Internal heap reserve in normal operation ~150 kB; web server and worker add about
~2 kB of static requirement on top. The pages are composed from flash building
blocks (a shell document with `%T`/`%L`/`%R`/`%S`/`%J`/`%B` placeholders, `%J` is
the chart script and stays empty on the pages without a chart), not from `String`
concatenation — otherwise every page build would use a large part of the heap.

The history split the chart page into **two** parts, and that is the actual memory
reason: the overview does not load its file, it asks for numbers from RAM. The
history loads exactly one month file — and that one sits in the **browser's**
memory, not in the panel. Statically the chart pages added nothing (112 264 bytes,
measured before and after the change).

## SD clock

The card runs at 4 MHz (`kSdFastHz`) and falls back to 400 kHz when the loopback
test does not pass it. Reason and measurement in `docs/sd-history.md`.

## Start and stop

`main.cpp` calls `webStart()`/`webUpdate()` only in `normalOperation()`
(`WIFI_READY` and no portal). `Configuration.cpp` calls `webStop()` before the
portal takes over the radio. mDNS runs along (`rct-panel.local`), but is pure
convenience: the IP stands on the panel's Service page and in the overview, and a
network that blocks mDNS only loses the name.