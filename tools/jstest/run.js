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
vm.runInContext(blockOf('kLogic') + '\nthis.__api={rpTz:rpTz,rpOffsetAt:rpOffsetAt,rpDayKey:rpDayKey,rpHm:rpHm,rpDateHm:rpDateHm};',
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

console.log('== ' + checks + ' checks, ' + failed + ' failed ==');
process.exit(failed === 0 ? 0 : 1);
