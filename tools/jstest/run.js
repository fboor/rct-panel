// Host test for the browser-side logic of the web interface.
//
// The logic block (src/web/pages.h, kLogic) is cut out of the header at run
// time and run here in node, so what is tested is what is sent to the browser -
// not a copy of it that could drift.
//
// The check values come from Python's zoneinfo, which is a different
// implementation: the panel states its time zone as a POSIX rule, and getting
// that half right costs an hour for half the year and moves the day boundaries
// with it. tools/tzcheck.py writes the values; they cover every six hours of
// 2026 plus every single hour around both switch-over dates.
//
// SPDX-License-Identifier: MIT
'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const root = path.join(__dirname, '..', '..');
const header = fs.readFileSync(path.join(root, 'src', 'web', 'pages.h'), 'utf8');

function blockOf(name) {
  const m = new RegExp(name + '\\[\\] PROGMEM = R"\\(([\\s\\S]*?)\\)";').exec(header);
  if (!m) {
    console.error('FEHLER: Block ' + name + ' nicht in src/web/pages.h gefunden');
    process.exit(1);
  }
  return m[1];
}

const sandbox = {console: console};
vm.createContext(sandbox);
vm.runInContext(blockOf('kLogic') + '\nthis.__api={rpTz:rpTz,rpOffsetAt:rpOffsetAt,rpDayKey:rpDayKey,rpHm:rpHm,rpDateHm:rpDateHm,rpRows:rpRows,rpSpan:rpSpan,rpEnergy:rpEnergy,rpDays:rpDays,rpBands:rpBands,rpShift:rpShift,rpShiftMonth:rpShiftMonth,rpDow:rpDow,rpDaysInMonth:rpDaysInMonth,rpFmtDate:rpFmtDate,rpKeyOf:rpKeyOf,rpGaps:rpGaps,rpDayPoints:rpDayPoints};',
                     sandbox);
const api = sandbox.__api;

let checks = 0;
let failed = 0;

function check(ok, what, got, want) {
  checks++;
  if (!ok) {
    failed++;
    console.log('  FEHLER ' + what + ': "' + got + '", sollte "' + want + '" sein');
  }
}

// The rule the panel itself runs with (src/config/Configuration.h).
const tz = api.rpTz('CET-1CEST,M3.5.0,M10.5.0/3');

// --- the rule was read apart at all ------------------------------------------
check(tz.std === 1, 'winter offset', tz.std, 1);
check(tz.dst === 2, 'summer offset', tz.dst, 2);
check(tz.hasDst === true, 'daylight saving present', tz.hasDst, true);
check(tz.start.m === 3 && tz.start.w === 5 && tz.start.d === 0, 'start rule',
      tz.start.m + '.' + tz.start.w + '.' + tz.start.d, '3.5.0');
check(tz.start.h === 2, 'start hour', tz.start.h, 2);
check(tz.end.m === 10 && tz.end.w === 5 && tz.end.d === 0, 'end rule',
      tz.end.m + '.' + tz.end.w + '.' + tz.end.d, '10.5.0');
check(tz.end.h === 3, 'end hour', tz.end.h, 3);

// --- every check value from zoneinfo ------------------------------------------
const values = JSON.parse(fs.readFileSync(
  path.join(__dirname, 'tzcheck.json'), 'utf8'));
for (const v of values) {
  check(api.rpOffsetAt(tz, v.t) === v.off, 'offset at ' + v.t,
        api.rpOffsetAt(tz, v.t), v.off);
  check(api.rpDayKey(tz, v.t) === v.day, 'day at ' + v.t,
        api.rpDayKey(tz, v.t), v.day);
  check(api.rpHm(tz, v.t) === v.hm, 'time at ' + v.t, api.rpHm(tz, v.t), v.hm);
}

// --- the two days the change bites -------------------------------------------
// The switch is on a Sunday at 02:00 local. A day key that ignored the change
// would put 03:30 on the wrong side of it.
check(api.rpDayKey(tz, Date.UTC(2026, 2, 29, 0, 30) / 1000) === '2026-03-29',
      'half an hour before the switch', api.rpDayKey(tz, Date.UTC(2026, 2, 29, 0, 30) / 1000),
      '2026-03-29');
check(api.rpDayKey(tz, Date.UTC(2026, 2, 29, 2, 30) / 1000) === '2026-03-29',
      'half an hour after the switch (still the same day, one hour later)',
      api.rpDayKey(tz, Date.UTC(2026, 2, 29, 2, 30) / 1000), '2026-03-29');
check(api.rpHm(tz, Date.UTC(2026, 2, 28, 12, 0) / 1000) === '13:00',
      'the day before the switch', api.rpHm(tz, Date.UTC(2026, 2, 28, 12, 0) / 1000),
      '13:00');
check(api.rpHm(tz, Date.UTC(2026, 2, 29, 0, 0) / 1000) === '01:00',
      'the hour before the switch', api.rpHm(tz, Date.UTC(2026, 2, 29, 0, 0) / 1000),
      '01:00');
check(api.rpHm(tz, Date.UTC(2026, 2, 29, 1, 0) / 1000) === '03:00',
      'the switch itself: 02:00 local winter time becomes 03:00 local summer time',
      api.rpHm(tz, Date.UTC(2026, 2, 29, 1, 0) / 1000), '03:00');

// --- the switch in October, where the fifth week of the month has 31 days ----
// M10.5.0 is 25 October 2026 at 01:00 UTC. Counting to the fifth Sunday instead
// of the last one puts the end of summer time on 1 November.
check(api.rpOffsetAt(tz, Date.UTC(2026, 9, 24, 12, 0) / 1000) === 2,
      'still summer time on 24 October',
      api.rpOffsetAt(tz, Date.UTC(2026, 9, 24, 12, 0) / 1000), 2);
check(api.rpOffsetAt(tz, Date.UTC(2026, 9, 25, 0, 59) / 1000) === 2,
      'still summer time one minute before the switch',
      api.rpOffsetAt(tz, Date.UTC(2026, 9, 25, 0, 59) / 1000), 2);
check(api.rpOffsetAt(tz, Date.UTC(2026, 9, 25, 1, 0) / 1000) === 1,
      'winter time at the switch',
      api.rpOffsetAt(tz, Date.UTC(2026, 9, 25, 1, 0) / 1000), 1);
check(api.rpHm(tz, Date.UTC(2026, 9, 25, 1, 0) / 1000) === '02:00',
      '03:00 local summer time is 02:00 local winter time',
      api.rpHm(tz, Date.UTC(2026, 9, 25, 1, 0) / 1000), '02:00');

// --- midnight is the boundary, not 23:00 -------------------------------------
check(api.rpDayKey(tz, Date.UTC(2026, 0, 14, 22, 59) / 1000) === '2026-01-14',
      '23:59 local on 14 January',
      api.rpDayKey(tz, Date.UTC(2026, 0, 14, 22, 59) / 1000), '2026-01-14');
check(api.rpDayKey(tz, Date.UTC(2026, 0, 14, 23, 1) / 1000) === '2026-01-15',
      '00:01 local on 15 January',
      api.rpDayKey(tz, Date.UTC(2026, 0, 14, 23, 1) / 1000), '2026-01-15');

// --- a rule without daylight saving must not be read as having one -----------
const wint = api.rpTz('CET-1');
check(wint.std === 1, 'winter zone offset', wint.std, 1);
check(wint.hasDst === false, 'winter zone has no daylight saving', wint.hasDst, false);
check(api.rpOffsetAt(wint, Date.UTC(2026, 6, 1) / 1000) === 1,
      'winter zone in July', api.rpOffsetAt(wint, Date.UTC(2026, 6, 1) / 1000), 1);

// --- a half-hour zone, because the sign there is the classic trap -------------
const kol = api.rpTz('IST-5:30');
check(kol.std === 5.5, 'India offset', kol.std, 5.5);
const ny = api.rpTz('EST5EDT,M3.2.0,M11.1.0');
check(ny.std === -5 && ny.dst === -4, 'New York offsets', ny.std + '/' + ny.dst, '-5/-4');
check(api.rpOffsetAt(ny, Date.UTC(2026, 0, 15) / 1000) === -5,
      'New York in January', api.rpOffsetAt(ny, Date.UTC(2026, 0, 15) / 1000), -5);
check(api.rpOffsetAt(ny, Date.UTC(2026, 6, 15) / 1000) === -4,
      'New York in July', api.rpOffsetAt(ny, Date.UTC(2026, 6, 15) / 1000), -4);

// --- the date written the way the page shows it ------------------------------
check(api.rpDateHm(tz, Date.UTC(2026, 9, 25, 12, 0) / 1000) === '2026-10-25 13:00',
      'date and time on the page',
      api.rpDateHm(tz, Date.UTC(2026, 9, 25, 12, 0) / 1000), '2026-10-25 13:00');

// --- the CSV ----------------------------------------------------------------
// A month file, built here and read the way the browser reads it: by column
// name, out of csvrow::kHeader (src/storage/CsvRow.h).
const NAMES = ('ts,pv_a,pv_b,s0,temp_core,temp_bat,temp_hsink,load_l1,load_l2,' +
               'load_l3,bat,soc,grid_l1,grid_l2,grid_l3,status,island,' +
               'pv_a_total_wh,pv_b_total_wh,ext_total_wh,load_total_wh,' +
               'feed_total_wh,grid_total_wh').split(',');

// 2026-10-02, 06:00 and 20:00 local. The counters at the start and at the end
// are the ones behind the figures the panel showed on 2 October 2026: 23.68 kWh
// generated, 15.89 kWh fed in, 1.81 kWh drawn, 3.89 kWh consumed.
const T0 = Date.UTC(2026, 9, 2, 4, 0) / 1000;          // 06:00 CEST
const T1 = Date.UTC(2026, 9, 2, 18, 0) / 1000;         // 20:00 CEST
const START = [10000, 20000, 5000, 30000, -4000, 8000];
const END = [33680, 20000, 5000, 33890, -19890, 9810];

function row(t, sums, o) {
  o = o || {};
  const v = (k, d) => (o[k] !== undefined ? o[k] : d);
  let s = [t, v('pv_a', 0), v('pv_b', 0), v('s0', 0),
           25.0, 26.0, 30.0,
           v('l1', 0), v('l2', 0), v('l3', 0), v('bat', 0), v('soc', 0),
           v('g1', 0), v('g2', 0), v('g3', 0), 0, 0].join(',');
  if (sums) {
    s += ',' + sums.join(',');
  }
  return s;
}

const csv = NAMES.join(',') + '\n' +
            row(T0, START, {l1: 400, l2: 500, g1: 50, bat: 100, soc: 50}) + '\n' +
            row(Date.UTC(2026, 9, 2, 12, 0) / 1000,
                [20000, 20000, 5000, 32000, -9000, 8900],
                {pv_a: 3200, l1: 300, l2: 400, g1: 300, bat: 100, soc: 78}) + '\n' +
            row(T1, END, {pv_a: 4100, l1: 350, l2: 380, g1: 120, bat: 120,
                          soc: 51}) + '\n';

const rows = api.rpRows(csv, NAMES, tz);
check(rows.length === 3, 'three rows read', rows.length, 3);
check(rows[0].t === T0, 'first timestamp', rows[0].t, T0);
check(rows[0].dk === '2026-10-02', 'day key of the first row', rows[0].dk, '2026-10-02');

// The six values, in the order the panel writes them: grid, consumption
// (meter plus external), production, external, battery, state of charge.
check(rows[0].v[0] === 50, 'grid of the first row', rows[0].v[0], 50);
check(rows[0].v[1] === 900, 'consumption is the sum of the three phase meters',
      rows[0].v[1], 900);
check(rows[0].v[2] === 0, 'production of the first row', rows[0].v[2], 0);
check(rows[0].v[3] === 0, 'external generator of the first row', rows[0].v[3], 0);
check(rows[0].v[4] === 100, 'battery of the first row', rows[0].v[4], 100);
check(rows[0].v[5] === 50, 'state of charge of the first row', rows[0].v[5], 50);
check(rows[2].v[2] === 4100, 'production of the last row', rows[2].v[2], 4100);

// The energy of the day: the figures the panel shows, computed from the file.
const span = api.rpSpan(rows, '2026-10-02');
check(span !== null && span[0] === 0 && span[1] === 2, 'the span of the day',
      String(span), '0,2');
const en = api.rpEnergy(rows);
check(en.values.pv === 23680, 'generated in Wh', en.values.pv, 23680);
check(en.values.feed === 15890, 'fed in in Wh (the counter is negative)',
      en.values.feed, 15890);
check(en.values.draw === 1810, 'drawn from the grid in Wh', en.values.draw, 1810);
check(en.values.load === 3890, 'consumed in Wh', en.values.load, 3890);
check(en.values.own === 7790, 'own consumption in Wh', en.values.own, 7790);
check(Math.abs(en.autarky - 53.47) < 0.01, 'self-sufficiency',
      en.autarky.toFixed(2), '53.47');
check(Math.abs(en.ownShare - 32.89) < 0.01, 'own consumption share',
      en.ownShare.toFixed(2), '32.89');

// A day with no row is not a day with zero energy.
check(api.rpSpan(rows, '2026-10-01') === null, 'a day without rows', 'null', 'null');
check(api.rpSpan(rows, '2026-10-03') === null, 'another day without rows', 'null', 'null');

// The external generator counts towards the production and towards the
// consumption, exactly as the panel's energyPeriodValues() has it.
const csvExt = NAMES.join(',') + '\n' +
               row(T0, [0, 0, 0, 0, 0, 0], {s0: 300}) + '\n' +
               row(T1, [0, 0, 1000, 0, 0, 0], {s0: 300}) + '\n';
const rowsExt = api.rpRows(csvExt, NAMES, tz);
const enExt = api.rpEnergy(rowsExt);
check(enExt.values.pv === 1000, 'external generator is generated energy',
      enExt.values.pv, 1000);
check(enExt.values.load === 1000, 'external generator is consumed energy',
      enExt.values.load, 1000);

// --- a file from before the sums were added ---------------------------------
const csvOld = NAMES.slice(0, 16).join(',') + '\n' +
               row(T0, null, {l1: 400, l2: 500, g1: 50, bat: 100, soc: 50}) + '\n' +
               row(T1, null, {pv_a: 4100, l1: 350, l2: 380, g1: 120, bat: 120,
                              soc: 51}) + '\n';
const rowsOld = api.rpRows(csvOld, NAMES, tz);
check(rowsOld.length === 2, 'both old rows are still read', rowsOld.length, 2);
check(rowsOld[0].s === null, 'an old row has no sums', String(rowsOld[0].s), 'null');
check(rowsOld[1].v[2] === 4100, 'the powers of an old row are there',
      rowsOld[1].v[2], 4100);

// A period that is entirely in the old format: the sums are filled with 0, the
// way the panel's own reader fills them, so the view stays continuous - and the
// two rates are left out rather than invented.
const enOld = api.rpEnergy(rowsOld);
check(enOld !== null, 'an old period still has an answer', String(enOld), 'object');
check(enOld.values.pv === 0, 'no generated energy without a counter',
      enOld.values.pv, 0);
check(enOld.values.own === 0, 'no own consumption without a counter',
      enOld.values.own, 0);
check(enOld.values.load === 0, 'no consumption without a counter',
      enOld.values.load, 0);
check(enOld.autarky === null, 'no self-sufficiency without a counter',
      String(enOld.autarky), 'null');
check(enOld.ownShare === null, 'no own share without a counter',
      String(enOld.ownShare), 'null');
check(enOld.sums === false, 'and it says so', String(enOld.sums), 'false');
check(enOld.missing === 1, 'and that rows are missing the sums',
      String(enOld.missing), '1');

// A month file created before the update: the old header, and behind it the new
// rows. That is what the development card has (231 rows with 16 values, 333 with
// 23 in October 2026), and it is the reason the decision is made per row and not
// per file.
const csvGemischt = NAMES.slice(0, 16).join(',') + '\n' +
                    row(T0, null, {l1: 400, g1: 50, bat: 100, soc: 50}) + '\n' +
                    row(T0 + 300, null, {l1: 420, g1: 30, bat: 101, soc: 51}) + '\n' +
                    row(T0 + 600, [20000, 20000, 5000, 32000, -9000, 8900],
                        {pv_a: 3200, l1: 300, g1: 300, bat: 102, soc: 52}) + '\n' +
                    row(T1, END, {pv_a: 4100, l1: 350, g1: 120, bat: 120,
                                  soc: 52}) + '\n';
const rowsGemischt = api.rpRows(csvGemischt, NAMES, tz);
check(rowsGemischt.length === 4, 'all four rows are read', rowsGemischt.length, 4);
check(rowsGemischt[0].s === null && rowsGemischt[1].s === null,
      'the two old rows have no sums',
      String(rowsGemischt[0].s) + '/' + String(rowsGemischt[1].s), 'null/null');
check(rowsGemischt[2].s !== null, 'the new row has sums',
      String(rowsGemischt[2].s), 'numbers');
const enGemischt = api.rpEnergy(rowsGemischt);
check(enGemischt.sums === true, 'a mixed period has sums to subtract',
      String(enGemischt.sums), 'true');
check(enGemischt.missing === 1, 'and says that rows are missing them',
      String(enGemischt.missing), '1');
// The difference runs from the first row that has sums to the last, not from
// the first row of the period: with all rows counted the result would be the
// counter's whole life.
check(enGemischt.values.pv === 13680, 'the generated energy is the difference',
      enGemischt.values.pv, 13680);
// Smaller than the figures of a period without old rows, because the start is
// the first row that has sums - the two old rows are not counted as zero, they
// are not counted at all.
check(enGemischt.values.load === 1890, 'and so is the consumption',
      enGemischt.values.load, 1890);
check(enGemischt.values.draw === 910, 'and the grid draw',
      enGemischt.values.draw, 910);

// One row with sums is still no difference - it is the first day after the
// update, and a counter needs two readings before it says anything.
const csvEin = NAMES.slice(0, 16).join(',') + '\n' +
               row(T0, null, {l1: 400, g1: 50, bat: 100, soc: 50}) + '\n' +
               row(T0 + 300, [20000, 20000, 5000, 32000, -9000, 8900],
                   {pv_a: 3200, l1: 300, g1: 300, bat: 101, soc: 51}) + '\n';
const enEin = api.rpEnergy(api.rpRows(csvEin, NAMES, tz));
check(enEin.sums === false, 'a single counter reading gives no difference',
      String(enEin.sums), 'false');
check(enEin.values.pv === 0, 'and no energy', enEin.values.pv, 0);
check(enEin.missing === 1, 'while it still names the old row',
      String(enEin.missing), '1');

// --- the days a period covers ----------------------------------------------
check(api.rpDays('day', '2026-10-07').join(',') === '2026-10-07', 'one day',
      api.rpDays('day', '2026-10-07').join(','), '2026-10-07');
check(api.rpDays('week', '2026-10-07').join(',') ===
      '2026-10-05,2026-10-06,2026-10-07,2026-10-08,2026-10-09,2026-10-10,2026-10-11',
      'the week runs from Monday to Sunday',
      api.rpDays('week', '2026-10-07').join(','),
      '2026-10-05,2026-10-06,2026-10-07,2026-10-08,2026-10-09,2026-10-10,2026-10-11');
check(api.rpDays('week', '2026-10-11')[0] === '2026-10-05',
      'a Sunday belongs to the week that started on the Monday',
      api.rpDays('week', '2026-10-11')[0], '2026-10-05');
check(api.rpDays('month', '2026-10-07').length === 31, 'October has 31 days',
      api.rpDays('month', '2026-10-07').length, 31);
check(api.rpDays('month', '2026-02-10').length === 28, 'February 2026 has 28 days',
      api.rpDays('month', '2026-02-10').length, 28);
check(api.rpDays('month', '2026-10-07')[30] === '2026-10-31', 'the last day of October',
      api.rpDays('month', '2026-10-07')[30], '2026-10-31');

// --- shifting a day is calendar arithmetic, not time ------------------------
// Across the change-over: a day is a day, whatever the clocks do inside it.
check(api.rpShift('2026-03-28', 1) === '2026-03-29', 'the day before the change',
      api.rpShift('2026-03-28', 1), '2026-03-29');
check(api.rpShift('2026-03-29', 1) === '2026-03-30', 'the day of the change',
      api.rpShift('2026-03-29', 1), '2026-03-30');
check(api.rpShift('2026-02-28', 1) === '2026-03-01', 'the end of February',
      api.rpShift('2026-02-28', 1), '2026-03-01');
check(api.rpShift('2026-12-31', 1) === '2027-01-01', 'the turn of the year',
      api.rpShift('2026-12-31', 1), '2027-01-01');
check(api.rpShift('2027-01-01', -1) === '2026-12-31', 'and back',
      api.rpShift('2027-01-01', -1), '2026-12-31');
check(api.rpShift('2026-01-01', -1) === '2025-12-31', 'the turn of the year back',
      api.rpShift('2026-01-01', -1), '2025-12-31');
check(api.rpShift('2024-02-28', 1) === '2024-02-29', 'a leap day',
      api.rpShift('2024-02-28', 1), '2024-02-29');
check(api.rpDaysInMonth('2024-02-01') === 29, 'February in a leap year',
      api.rpDaysInMonth('2024-02-01'), 29);

// --- a month is a month, not a number of days -------------------------------
// The trap this rules out: stepping back from October by 31 days lands on the
// 31st of August, and stepping back from a 30-day month by "its length" leaves
// the month altogether.
const sm = api.rpShiftMonth;
check(sm('2026-10-01', -1) === '2026-09-01', 'October back is September',
      sm('2026-10-01', -1), '2026-09-01');
check(sm('2026-10-01', -9) === '2026-01-01', 'nine months back is January',
      sm('2026-10-01', -9), '2026-01-01');
check(sm('2026-10-01', -10) === '2025-12-01', 'ten months back is last December',
      sm('2026-10-01', -10), '2025-12-01');
check(sm('2026-01-01', -1) === '2025-12-01', 'January back is last December',
      sm('2026-01-01', -1), '2025-12-01');
check(sm('2025-12-01', 1) === '2026-01-01', 'December forward is January',
      sm('2025-12-01', 1), '2026-01-01');
check(sm('2026-12-01', 1) === '2027-01-01', 'the turn of the year forward',
      sm('2026-12-01', 1), '2027-01-01');
check(sm('2026-05-15', -3) === '2026-02-01', 'the anchor day does not matter',
      sm('2026-05-15', -3), '2026-02-01');
for (const start of ['2026-01-01', '2026-02-01', '2026-03-01', '2026-04-01',
                     '2026-07-01', '2026-11-01', '2026-12-01']) {
  check(sm(sm(start, -1), 1) === start, 'a month back and forward again from ' + start,
        sm(sm(start, -1), 1), start);
}

// --- the bands of a week or a month ----------------------------------------
// Two days, with a lowest and a highest value per series.
const csv2 = NAMES.join(',') + '\n' +
             row(Date.UTC(2026, 9, 1, 10, 0) / 1000, [0, 0, 0, 0, 0, 0],
                 {pv_a: 100, l1: 200, l2: 250, g1: 90, bat: 60, soc: 60}) + '\n' +
             row(Date.UTC(2026, 9, 1, 16, 0) / 1000, [0, 0, 0, 0, 0, 0],
                 {pv_a: -800, l1: 900, g1: -800, bat: -50, soc: 64}) + '\n' +
             row(Date.UTC(2026, 9, 2, 12, 0) / 1000, [0, 0, 0, 0, 0, 0],
                 {pv_a: 400, l1: 300, l2: 300, g1: 400, bat: 70, soc: 70}) + '\n';
const rows2 = api.rpRows(csv2, NAMES, tz);
const bands = api.rpBands(rows2, api.rpDays('week', '2026-10-01'));
check(bands.length === 2, 'one band per day with rows', bands.length, 2);
check(bands[0].lo[0] === -800, 'the lowest grid value of the first day',
      bands[0].lo[0], -800);
check(bands[0].hi[0] === 90, 'the highest grid value of the first day',
      bands[0].hi[0], 90);
check(bands[0].lo[1] === 450, 'the lowest consumption of the first day',
      bands[0].lo[1], 450);
check(bands[0].hi[1] === 900, 'the highest consumption of the first day',
      bands[0].hi[1], 900);
check(bands[0].lo[5] === 60 && bands[0].hi[5] === 64, 'the state of charge band',
      bands[0].lo[5] + '/' + bands[0].hi[5], '60/64');
check(bands[1].lo[2] === 400 && bands[1].hi[2] === 400, 'a flat day',
      bands[1].lo[2] + '/' + bands[1].hi[2], '400/400');
check(api.rpDayPoints(rows2, '2026-10-01').length === 2, 'two points for one day',
      api.rpDayPoints(rows2, '2026-10-01').length, 2);
check(api.rpBands(rows2, api.rpDays('week', '2026-10-08')).length === 0,
      'no bands for a week without rows', api.rpBands(rows2, api.rpDays('week', '2026-10-08')).length, 0);

// --- the gaps of a period ---------------------------------------------------
// The rule the panel uses (histPush in src/gui/GuiApp.cpp): more than one and a
// half intervals between two samples means the slots in between are missing.
const T = Date.UTC(2026, 9, 5, 0, 0) / 1000;
function stamps(offsets) {
  return offsets.map(o => ({t: T + o * 300}));
}
check(api.rpGaps(stamps([0, 1, 2, 3])).count === 0, 'no gap in a full day',
      api.rpGaps(stamps([0, 1, 2, 3])).count, 0);
// 25 minutes between two samples: four slots missing, ten minutes.
check(api.rpGaps(stamps([0, 5])).count === 4, '25 minutes is four missing samples',
      api.rpGaps(stamps([0, 5])).count, 4);
check(api.rpGaps(stamps([0, 5])).minutes === 20, 'and twenty minutes',
      api.rpGaps(stamps([0, 5])).minutes, 20);
// A sample that is a little late is not a gap: the threshold absorbs up to one
// and a half intervals, so 400 s of extra time still counts as the next sample.
check(api.rpGaps(stamps([0, 1, 2, 3]).concat([{t: T + 3 * 300 + 400}])).count === 0,
      'a sample 400 s late is not a gap',
      api.rpGaps(stamps([0, 1, 2, 3]).concat([{t: T + 3 * 300 + 400}])).count, 0);
check(api.rpGaps(stamps([0, 1, 2, 3]).concat([{t: T + 3 * 300 + 600}])).count === 1,
      'a sample two intervals late leaves one slot empty',
      api.rpGaps(stamps([0, 1, 2, 3]).concat([{t: T + 3 * 300 + 600}])).count, 1);
check(api.rpGaps(stamps([0, 7])).count === 6, '35 minutes is six missing samples',
      api.rpGaps(stamps([0, 7])).count, 6);
check(api.rpGaps(stamps([0, 3, 10])).count === 8, 'two gaps add up',
      api.rpGaps(stamps([0, 3, 10])).count, 8);
check(api.rpGaps(stamps([0, 3, 10])).minutes === 40, 'and their minutes',
      api.rpGaps(stamps([0, 3, 10])).minutes, 40);
check(api.rpGaps([]).count === 0, 'nothing to count', api.rpGaps([]).count, 0);

// --- the date on the page ----------------------------------------------------
check(api.rpFmtDate('2026-10-02', '{D}.{M}.{Y}') === '02.10.2026', 'German date',
      api.rpFmtDate('2026-10-02', '{D}.{M}.{Y}'), '02.10.2026');
check(api.rpFmtDate('2026-10-02', '{Y}-{M}-{D}') === '2026-10-02', 'English date',
      api.rpFmtDate('2026-10-02', '{Y}-{M}-{D}'), '2026-10-02');

// --- the drawing block in a stubbed browser ---------------------------------
// The parts of web::kScript that need a DOM: the first attempt fails, and the
// page has to ask again by itself. Without that, a page open while the panel
// restarts keeps its error note for good - the five-second poll only starts
// after a success. rpRetryMs is shortened here instead of waiting.
{
  const code = blockOf('kScript');
  let rufe = 0;
  let interval = null;
  const el = {
    attributes: {
      'data-col': 'ca0c0f,a45ee5,3ec97a,2e93e5,f0a202,ffea00',
      'data-lab': 'Netz|Verbrauch|PV|EXT|Akku|SOC',
      'data-dfmt': '{D}.{M}.{Y}',
      'data-sfmt': '{D}.{M}.',
      'data-stampfmt': 'Stand %s.',
      'data-live': 'live',
      'data-load': 'laedt',
      'data-old': 'alt',
      'data-none': 'keine Messwerte',
      'data-nofile': 'keine Datei',
      'data-gap1': '1 Lücke',
      'data-gapn': '%d Lücken',
      'data-err': 'Daten konnten nicht geladen werden.',
      'data-sep': ',',
      'data-key': 'pv|own|feed|draw|load',
      'data-r': 'Autarkie|Eigenverbrauchsquote',
      'data-lab-balken': 'x',
      'data-cols': NAMES.join(','),
    },
    innerHTML: '',
    setAttribute(k, v) { this.attributes[k] = v; },
    getAttribute(k) { return this.attributes[k] !== undefined ? this.attributes[k] : null; },
    set textContent(v) { this._t = v; },
    get textContent() { return this._t; },
    addEventListener() {},
    getElementsByTagName() { return []; },
    querySelector() { return null; },
    querySelectorAll() { return []; },
    style: {},
    disabled: false,
  };
  const dokumente = {'verlauf': el, 'range': null, 'nav': null,
                     'rangetext': null, 'periode': null, 'hinweis': null,
                     'luecken': null};
  const ring = {
    tz: 'CET-1CEST,M3.5.0,M10.5.0/3', points: 3,
    series: ['grid', 'load', 'pv', 'ext', 'battery', 'soc'],
    unit: ['W', 'W', 'W', 'W', 'W', '%'],
    data: [{t: 1790894400, v: [10, 200, 300, 0, -50, 60]},
           null,
           {t: 1790895000, v: [20, 210, 310, 0, -55, 61]}],
    from: 1790894400, to: 1790895000,
  };
  const sandbox2 = {
    console: console,
    document: {
      readyState: 'complete',
      getElementById: (id) => (id in dokumente ? dokumente[id] : null),
      addEventListener: () => {},
    },
    setInterval: (fn, ms) => { interval = {fn: fn, ms: ms}; return 1; },
    clearInterval: () => { interval = null; },
    setTimeout: (fn, ms) => { setTimeout(fn, ms); },
    fetch: (url) => {
      rufe++;
      if (rufe === 1) {
        return Promise.reject(new Error('simulierter Ausfall'));
      }
      return Promise.resolve({ok: true, json: () => Promise.resolve(ring)});
    },
  };
  vm.createContext(sandbox2);
  // Der Fuss des Script-Blocks ruft rpBoot() selbst, readyState ist 'complete'.
  vm.runInContext(blockOf('kLogic') + '\n' + code.replace(
    'var rpRetryMs=5000;', 'var rpRetryMs=300;'), sandbox2);
  // Der Rueckruf der Fehlschlaege laeuft als Mikrotask, die Wiederholung ueber
  // einen Timer: beides braucht Zeit, deshalb in zwei Schritten warten. Die
  // Wartezeiten sind grosszuegig, weil der Test auf einem belasteten Rechner
  // sonst an der eigenen Planung scheitert und nicht am Code.
  check(rufe === 1, 'the first attempt went out', rufe, 1);
  check(interval === null, 'no poll runs before the first success',
        interval === null, true);

  new Promise((r) => setTimeout(r, 25)).then(() => {
    check(el.innerHTML.indexOf('note bad') >= 0, 'the failure is shown',
          el.innerHTML.indexOf('note bad') >= 0, true);
    check(interval === null, 'and still no poll', interval === null, true);
    return new Promise((r) => setTimeout(r, 500));
  }).then(() => {
    check(rufe >= 2, 'the page asked again by itself', rufe >= 2, true);
    check(el.innerHTML.indexOf('<svg') >= 0, 'and drew the chart',
          el.innerHTML.indexOf('<svg') >= 0, true);
    check(el.innerHTML.indexOf('note bad') < 0, 'the error note is gone',
          el.innerHTML.indexOf('note bad') < 0, true);
    check(interval !== null && interval.ms === 300, 'the poll waits rpRetryMs',
          String(interval && interval.ms), '300');
    console.log('== ' + checks + ' checks, ' + failed + ' failed ==');
    process.exit(failed === 0 ? 0 : 1);
  });
}
