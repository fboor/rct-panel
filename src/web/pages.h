// HTML/CSS for the web interface (see docs/web-interface.md).
//
// Everything here lives in flash (PROGMEM) and is sent with send_P, because
// these pages are rebuilt for every request and internal heap is the scarce
// resource on this panel: ~150 kB free in normal operation, and a String-based
// page would churn a large part of that per request.
//
// Style follows the portal page (same colours and the same German UI text) so
// the two pages the user can reach - setup portal and normal-operation web
// interface - feel like the same product. Mobile-first: most visits come from a
// phone.
//
// The page shell is assembled with a placeholder token rather than by string
// concatenation, so a request costs one flash buffer plus whatever the values
// themselves need.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_WEB_PAGES_H
#define RCT_WEB_PAGES_H

#include <Arduino.h>

namespace web {

// ---------------------------------------------------------------------------
// Shared shell. %T is replaced by the page title, %L by the language attribute,
// %R by a refresh tag, %S by the style block, %B by the body.
// ---------------------------------------------------------------------------
static const char kShell[] PROGMEM = R"(<!DOCTYPE html>
<html %L><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%T</title>
%R
<style>%S</style>
</head><body>
<div class="wrap">%B</div>
</body></html>)";

// One style block for all pages: a couple of rules that would bloat every
// single page if they were repeated inline.
static const char kStyle[] PROGMEM = R"(
*{box-sizing:border-box}
body{margin:0;font:16px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;background:#f2f4f6;color:#1d2530}
.wrap{max-width:760px;margin:0 auto;padding:0 0 40px}
header{background:#2f6fb5;color:#fff;padding:14px 16px}
header h1{margin:0;font-size:19px;font-weight:600}
nav{background:#fff;border-bottom:1px solid #dfe3e8;overflow-x:auto;white-space:nowrap}
nav a{display:inline-block;padding:12px 16px;color:#2f6fb5;text-decoration:none;font-size:15px;border-bottom:3px solid transparent}
nav a.on{color:#1d2530;border-bottom-color:#2f6fb5;font-weight:600}
main{padding:16px}
h2{font-size:16px;margin:22px 0 8px}
h2:first-child{margin-top:0}
table{width:100%;border-collapse:collapse;background:#fff;border:1px solid #dfe3e8;border-radius:8px;overflow:hidden}
td{padding:9px 12px;border-bottom:1px solid #eef1f4;font-size:15px}
tr:last-child td{border-bottom:0}
td.k{color:#5a6672;width:45%}
td.v{text-align:right;font-variant-numeric:tabular-nums}
.big{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-bottom:4px}
/* Four values side by side from here up. The grid sets the widths, so there is
   no width in between where the row breaks into three plus one - which is what
   a minimum width on the cards produced. */
@media(min-width:560px){.big{grid-template-columns:repeat(4,1fr)}}
.card{background:#fff;border:1px solid #dfe3e8;border-radius:8px;padding:11px 10px}
.card .l{color:#5a6672;font-size:13px}
.card .n{font-size:21px;font-weight:600;margin-top:2px;white-space:nowrap}
a.lnk{color:#2f6fb5}
.btn{display:inline-block;padding:11px 18px;background:#2f6fb5;color:#fff;border:0;border-radius:8px;font-size:15px;text-decoration:none;cursor:pointer}
.btn.gray{background:#6b7785}
.note{background:#fff8e1;border:1px solid #f0dca0;border-radius:8px;padding:11px 13px;font-size:14px;margin:14px 0}
.ok{color:#1d7a3c}
.bad{color:#b3352c}
ul.plain{list-style:none;padding:0;margin:0}
ul.plain li{background:#fff;border:1px solid #dfe3e8;border-radius:8px;margin-bottom:8px;padding:11px 13px;display:flex;align-items:center;gap:12px}
ul.plain li .g{flex:1;min-width:0}
ul.plain li .g b{display:block;word-break:break-all}
ul.plain li .m{color:#5a6672;font-size:13px;white-space:nowrap}
form{margin:0}
input[type=file]{display:block;width:100%;margin:10px 0;padding:11px;background:#fff;border:1px solid #dfe3e8;border-radius:8px}
input[type=text],input[type=number],select{width:100%;padding:11px;margin:10px 0;border:1px solid #dfe3e8;border-radius:8px;font-size:16px;background:#fff}
.btn+.btn{margin-left:8px}
code{background:#e8ebef;padding:1px 5px;border-radius:4px;font-size:14px}
)" ;

// The navigation words and the page titles are not here: they are texts, and
// texts live in src/i18n (see Lang.h), one table per language.

} // namespace web

#endif // RCT_WEB_PAGES_H