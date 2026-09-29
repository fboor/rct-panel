# Energy page ("Energie") — design (rct-panel)

Status: **implemented and verified on the board** (2026-09-29) against
`tools/rct_sim.py`. The five series, colors and layout below are as built.

Goal: the second page (after "Energiefluss") shows the accumulated energies in
kWh as horizontal bars, like the RCT portal's measurement area
("Auswahl Messungen"). The period is selectable (Tag / Monat / Jahr / Gesamt),
and the portal's color scheme appears as a multi-line legend that fits the
480×364 content area.

Reference: `examples/RCT Portal _ Speichersysteme neu gedacht-page2.html`
(saved portal "Reports" page), OIDs cross-checked against the fetched
`rctclient` inverter registry (energy section).

## 1. Page order — insert a new page at position 2

User decision: insert a new "Energie" page; the existing "Heute" page remains.

| Index | Title before | Title after          |
|-------|--------------|----------------------|
| 0     | Energiefluss | Energiefluss         |
| 1     | Heute        | **Energie** (new)    |
| 2     | Info         | Heute                |
| 3     | Verlauf      | Info                 |
| 4     | Gerät        | Verlauf              |
| 5     | Service      | Gerät                |
| 6     | —            | Service              |

Mechanics (as built):
- `GuiApp.cpp`: `PAGE_ENERGY` → `PAGE_HEUTE` (builder `pageBuildEnergy` →
  `pageBuildHeute`, day tiles and the `EN_*` label enum unchanged); the new
  `PAGE_ENERGY = 1` with its own builder and the `EB_*` label enum.
- Titles / builders arrays gain the new page; nothing else moves.
- Side effect worth knowing: the fast poll group grew from 32 to 45 slots, past
  the width of the 32-bit "all answered" mask in `RctClient.cpp` (whose
  `1u << 32` was already undefined behaviour). It is a `uint64_t` now.

## 2. Measurements (5 active portal series)

User decision: the 5 series the portal has enabled (AC Erzeugung and Externe
Energieerzeugung are deactivated there and are skipped).

| Bar             | Color (portal)  | Period sources (Wh)                          |
|-----------------|-----------------|----------------------------------------------|
| PV Erzeugung    | `#EBD300` gelb  | `e_dc_day[0..1]` · `e_dc_month[0..1]` · `e_dc_year[0..1]` · `e_dc_total[0..1]` |
| Netzbezug       | `#CA0C0F` rot   | `e_grid_load_day` · `_month` · `_year` · `_total` |
| Netzeinspeisung | `#F48756` orange| `e_grid_feed_day` · `_month` · `_year` · `_total` |
| Eigenverbrauch  | `#12A40A` grün  | derived: PV − Netzeinspeisung (clamped ≥ 0)  |
| Verbrauch       | `#3CBCD4` türkis| `e_load_day` · `_month` · `_year` · `_total` |

Colors are the portal chart palette (visual check against the saved HTML
during bring-up). Sign convention: all counters are ≥ 0; Eigenverbrauch is the
only derived value.

## 3. New RCT OIDs (13 sockets, all FLOAT counters)

Already polled day + total values are reused; only month/year/total for the
three meters plus year/total for PV is new:

| Slot                | OID          | Object                   |
|---------------------|--------------|--------------------------|
| DC month A/B        | 0x81AE960B / 0x7AB9B045 | `e_dc_month[0..1]` |
| DC year A/B         | 0xAF64D0FE / 0xBD55D796 | `e_dc_year[0..1]` |
| DC total A/B        | 0xFC724A9E / 0x68EEFD3D | `e_dc_total[0..1]` |
| load month / year / total | 0xF0BE6429 / 0xC7D3B479 / 0xEFF4B537 | `e_load_month`, `e_load_year`, `e_load_total` |
| feed-in month / year | 0x65B624AB / 0x26EFFC2F | `e_grid_feed_month`, `e_grid_feed_year` |
| grid-load month / year | 0x126ABC86 / 0xDE17F021 | `e_grid_load_month`, `e_grid_load_year` |

Reused: `e_dc_day[0..1]` (DC0/DC1), `e_load_day` (ELOADDAY),
`e_grid_feed_day` (EFEEDDAY), `e_grid_load_day` (EGRIDLOADDAY),
`e_grid_feed_total` (EFEED), `e_grid_load_total` (ELOAD).

All 13 go into the fast group (FLOAT). They are static accumulators: the plain
10 s poll cadence costs only 13 small reads a cycle.

`RctSnapshot` gains 10 floats: `monthPvWh, yearPvWh, totalPvWh, monthLoadWh,
yearLoadWh, totalLoadWh, monthFeedInWh, yearFeedInWh, monthGridLoadWh,
yearGridLoadWh`. The two lifetime grid meters are the pre-existing
`feedInEnergyWh` (`e_grid_feed_total`) and `loadEnergyWh` (`e_grid_load_total`),
so no duplicate fields were added. Eigenverbrauch per period is derived in the
GUI (PV − Einspeisung).

Bring-up log (once, on the first poll where all 13 answered), so the wiring can
be checked without a screen:

```
RCT energy [kWh Tag/Monat/Jahr/Gesamt] PV 3.6/33.9/706.0/4448.0 |
  Verbrauch 9.8/103.7/865.0/9779.0 | Einspeisung 2.1/18.4/402.0/3124.0 |
  Bezug 3.0/88.2/561.0/8455.0
```

## 4. Layout (480 × 364 content)

- Selector row (~44 px): four buttons `Tag | Monat | Jahr | Gesamt`; the
  active period is highlighted (portal dashboard style).
- Five bar rows, each a label line over a full-width bar:

  ```
  PV Erzeugung                              12,4 kWh
  ████████████████████████████▌░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
  Eigenverbrauch                             7,9 kWh
  ███████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
  Netzeinspeisung                            4,5 kWh
  ███████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
  ```

  Per row: name in the series color, left aligned, above its bar; value right
  aligned on the name line; bar length normalized to the largest value of the
  shown period. See 4.1 for why the chip variant was dropped.
- Value formatting: < 1000 kWh → `"X,X kWh"` (1 decimal), ≥ 1000 kWh →
  `"X,XX MWh"` (2 decimals). No data yet → `--`.
- Bars update at the 1 Hz UI tick from the snapshot; no extra polling logic.

As built (constants in `GuiApp.cpp`): selector buttons 108 × 34 at y = 8, row
pitch 58 px starting at y = 56, bar track 440 × 16 at x = 20, value column
right-aligned at x = 340. A non-zero but tiny value keeps a 3 px stub so it does
not read as "zero".

### 4.1 Legend: label above the bar

First cut put a color chip + name to the left of each bar, which squeezed the
bars into 190 px and needed a legend explanation. As built instead: the **name
sits above its own bar, left aligned, in the series color**, with the value
right aligned on the same line. The bar below is the same color and spans the
full 440 px.

```
PV Erzeugung                              12,4 kWh
██████████████████████████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
Eigenverbrauch                            7,9 kWh
████████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
Netzeinspeisung                           4,5 kWh
███████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
```

Consequence: the bars are more than twice as long, the reading order is
unambiguous (name directly above its bar), and there is no separate legend
whose entries could be mistaken for something else. The chip objects are gone
from the builder.

## 5. Simulator

`tools/rct_sim.py` gains the 13 OIDs with realistic month/year/total values
(day values already simulated). `_drift()` increments the month counters by a
tiny fraction of the day drift so the totals slowly move, matching the panel's
10 s / 1 Hz refresh. The seeded numbers are internally consistent — per period
`Verbrauch = (PV − Einspeisung) + Bezug` — so Eigenverbrauch and Verbrauch do
not contradict each other on screen. Sim serves 54 objects (was 41).

## 6. Implementation status

All five steps done. Verified against the sim on the board: 45/54 slots fresh
(all 13 new OIDs answer), the energy bring-up line above, `SD: mounted`, and
`hist: 4 samples restored from SD log` after a reboot.

> Not included (currently): weekly totals (portal has "Woche", but there are
> no weekly OIDs in the registry — a week would have to be derived from
> per-day counters, which are not all exposed), AC/external energy, self-
> consumption percentages (those stay on the "Heute" page).