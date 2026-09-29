# Energy page ("Energie") — planning (rct-panel)

Status: **planning document, not yet implemented.**

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

Mechanics:
- `GuiApp.cpp`: rename `PAGE_ENERGY` → `PAGE_HEUTE` (its builder
  `pageBuildEnergy` → `pageBuildHeute`, keeps the current day tiles and the
  `EN_*` label enum unchanged).
- Add `PAGE_ENERGY = 1` with a new `pageBuildEnergy` (bars page) and a new
  `EB_*` label enum.
- Titles / builders arrays gain the new page; nothing else moves.

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

`RctSnapshot` gains 12 floats: `monthPvWh, yearPvWh, totalPvWh,
monthLoadWh, yearLoadWh, totalLoadWh, monthFeedInWh, yearFeedInWh,
totalFeedInWh, monthGridLoadWh, yearGridLoadWh, totalGridLoadWh`.
Eigenverbrauch per period is derived in the GUI (PV − Einspeisung).

## 4. Layout (480 × 364 content)

- Selector row (~44 px): four buttons `Tag | Monat | Jahr | Gesamt`; the
  active period is highlighted (portal dashboard style).
- Five bar rows (~48 px each):

  ```
  █ PV Erzeugung    ████████████▌      12,4 kWh
  █ Eigenverbrauch  ███████▌            7,9 kWh
  █ Netzeinspeisung ████▌               4,5 kWh
  █ Netzbezug       ███                 3,0 kWh
  █ Verbrauch       ██████████████▌    15,6 kWh
  ```

  Per row: 10 px color chip, label (German, ~110 px), bar length normalized
  to the largest value of the shown period, value right-aligned. The chip +
  label rows are the multi-line legend (portal look); no separate legend block
  is needed at 480 px width.
- Value formatting: < 1000 kWh → `"X,X kWh"` (1 decimal), ≥ 1000 kWh →
  `"X,XX MWh"` (2 decimals). No data yet → `--`.
- Bars update at the 1 Hz UI tick from the snapshot; no extra polling logic.

## 5. Simulator

`tools/rct_sim.py` gains the 13 OIDs with realistic month/year/total values
(day values already simulated). `_drift()` increments the month counters by a
tiny fraction of the day drift so the totals slowly move, matching the panel's
10 s / 1 Hz refresh.

## 6. Steps if approved

1. `RctTypes.h`: +12 snapshot fields.
2. `RctClient.cpp`: +13 slots + OIDs, publish into the new fields.
3. `GuiApp.cpp`: enum/split `PAGE_ENERGY`→`PAGE_HEUTE`, new `PAGE_ENERGY`
   bars page (selector buttons, bars, legend rows, formatting).
4. `tools/rct_sim.py`: +13 OIDs + drift.
5. Build, flash, verify against the sim (bars render, period switch works,
   colors match the portal palette).

> Not included (currently): weekly totals (portal has "Woche", but there are
> no weekly OIDs in the registry — a week would have to be derived from
> per-day counters, which are not all exposed), AC/external energy, self-
> consumption percentages (those stay on the "Heute" page).