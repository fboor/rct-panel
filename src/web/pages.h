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
// %R by a refresh tag, %S by the style block, %J by the chart logic, %K by the
// chart script, %B by the body. %J and %K stay empty on the pages without a
// chart.
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
<script>%J</script>
<script>%K</script>
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
/* Five entries no longer fit on a narrow phone in the longer of the two
   languages. Tighter padding keeps them all on one line; the strip still
   scrolls sideways if a build spells them even longer. */
@media(max-width:400px){nav a{padding:12px 11px;font-size:14px}}
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
/* --- the charts -----------------------------------------------------------
   The panel sends numbers, the browser draws them (see kScript). A bar row is
   label and value on one line over a full-width bar, like on the panel: the
   label is the legend, so the colours need no separate key. The bar colours are
   the portal palette, in the order pv, own, feed, draw, load. */
.seg{display:flex;gap:6px;margin:0 0 12px}
.seg button{flex:1;padding:9px 4px;border:1px solid #dfe3e8;background:#fff;border-radius:8px;font-size:14px;color:#5a6672;cursor:pointer}
.seg button.on{background:#2f6fb5;border-color:#2f6fb5;color:#fff;font-weight:600}
/* The navigator of the history page: two buttons and the period they move over.
   The text in between takes what is left and gives up with an ellipsis rather
   than pushing a button off the edge of a narrow phone. */
.nav{display:flex;align-items:center;gap:10px;margin:0 0 12px}
.nav span{flex:1;min-width:0;text-align:center;font-size:14px;color:#5a6672;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.nav .btn{flex:0 0 auto}
.bar{background:#fff;border:1px solid #dfe3e8;border-radius:8px;padding:9px 12px;margin-bottom:7px}
.bar .l{display:flex;justify-content:space-between;gap:10px;font-size:14px}
.bar .l b{font-weight:600;font-variant-numeric:tabular-nums;white-space:nowrap}
.bar .t{height:9px;background:#eef1f4;border-radius:5px;margin-top:6px}
.bar .f{height:9px;border-radius:5px;width:0}
.b0 .f{background:#ebd300}.b1 .f{background:#12a40a}.b2 .f{background:#f48756}
.b3 .f{background:#ca0c0f}.b4 .f{background:#3cbcd4}
.rates{display:flex;gap:8px;margin:10px 0 0}
.rates div{flex:1;background:#fff;border:1px solid #dfe3e8;border-radius:8px;padding:9px 11px;font-size:13px;color:#5a6672}
.rates div b{display:block;font-size:18px;font-weight:600;color:#1d2530;font-variant-numeric:tabular-nums}
.chart{background:#fff;border:1px solid #dfe3e8;border-radius:8px;padding:8px 8px 4px;margin-bottom:10px}
/* The drawing keeps its proportions, so on a wide screen it would grow to
   twice the height of the phone version and push everything below it off the
   fold. Capped, the SVG centres itself in the cap - the alternative, a
   different viewBox per width, would be a second set of numbers to keep
   straight. */
.chart svg{display:block;width:100%;height:auto;max-height:380px;margin:0 auto}
.legend{font-size:13px;color:#5a6672;margin:6px 2px 0}
.legend i{display:inline-block;width:11px;height:3px;border-radius:2px;margin:0 4px 3px 10px;vertical-align:middle}
.legend span{display:inline-block;margin:0 10px 2px 0}
.stamp{font-size:12px;color:#8a94a0;margin:8px 2px 0}
.note.bad{background:#fdecea;border-color:#f0b8b3}
.note.bad{background:#fdecea;border-color:#f0b8b3}
)" ;

// The navigation words and the page titles are not here: they are texts, and
// texts live in src/i18n (see Lang.h), one table per language.
//
// ---------------------------------------------------------------------------
// The chart logic. No DOM here: the time zone rule, the day a timestamp belongs
// to, and the formatting of a value. The drawing is in kScript, which needs
// all of this and nothing of this needs the drawing.
//
// It is a separate block for one more reason: tools/jstest runs it in node
// against check values that were computed with Python's zoneinfo, which is a
// different implementation than this one. The panel's answer (src/config
// Configuration.h) is a POSIX rule, and a POSIX rule is exactly the thing that
// is easy to get half right: one hour off for half the year, and the day
// boundaries move with it.
static const char kLogic[] PROGMEM = R"(
// "CET-1CEST,M3.5.0,M10.5.0/3" into parts. The offsets come back in hours
// east of UTC; the POSIX sign is the other way round, which is why "-1" means
// one hour ahead.
function rpTz(rule){
  var out={std:0,dst:0,hasDst:false,start:null,end:null};
  var c=rule.indexOf(',');
  var left=c<0?rule:rule.substring(0,c);
  var rest=c<0?'':rule.substring(c+1);
  var i=0;
  while(i<left.length&&/[A-Za-z]/.test(left.charAt(i))){i++}
  var a=rpOff(left,i);
  out.std=a[0];
  i=a[1];
  var n0=i;
  while(i<left.length&&/[A-Za-z]/.test(left.charAt(i))){i++}
  if(i>n0){
    // A daylight name without its own offset means one hour ahead of winter
    // time - which is what "CEST" after "CET-1" has to mean.
    out.hasDst=true;
    var b=rpOff(left,i);
    out.dst=b[1]>i?b[0]:out.std+1;
  }
  if(out.hasDst&&rest.length>0){
    var p=rest.split(',');
    out.start=rpRule(p[0]);
    out.end=rpRule(p[1]);
  }
  return out;
}
// One offset out of a POSIX zone string, as [hours east of UTC, index behind
// it]. The sign is the other way round than everywhere else, which is why
// "CET-1" is one hour ahead and not one hour behind.
function rpOff(s,i){
  var sign=1,h=0,m=0,k=i;
  if(s.charAt(k)==='-'||s.charAt(k)==='+'){
    if(s.charAt(k)==='-'){sign=-1}
    k++;
  }
  while(k<s.length&&s.charAt(k)>='0'&&s.charAt(k)<='9'){h=h*10+(s.charAt(k)-'0');k++}
  if(s.charAt(k)===':'){
    k++;
    while(k<s.length&&s.charAt(k)>='0'&&s.charAt(k)<='9'){m=m*10+(s.charAt(k)-'0');k++}
  }
  return [-sign*(h+m/60),k]; // POSIX counts west of UTC, we count east
}
// "M3.5.0/2:30" - month, week (5 = last), weekday (0 = Sunday), local time.
function rpRule(s){
  var r={m:0,w:0,d:0,h:2,min:0};
  var p=s.split('/');
  var a=p[0].split('.');
  r.m=parseInt(a[0].substring(1),10);
  r.w=parseInt(a[1],10);
  r.d=parseInt(a[2],10);
  if(p.length>1){
    var t=p[1].split(':');
    r.h=parseInt(t[0],10);
    r.min=t.length>1?parseInt(t[1],10):0;
  }
  return r;
}
// The instant of the rule in a given year, as unix seconds. Local time in the
// offset that is in force just before the switch, which is what POSIX says.
//
// Week 5 means the last one, not the fifth: "M10.5.0" is the last Sunday in
// October, and counting to the fifth Sunday would land on 32 October, which is
// the first of November - a day off for the whole of the last week of summer
// time.
function rpEpoch(r,year,off){
  var first=new Date(Date.UTC(year,r.m-1,1)).getUTCDay();
  var days=new Date(Date.UTC(year,r.m,0)).getUTCDate();
  var day;
  if(r.w>4){
    var last=new Date(Date.UTC(year,r.m-1,days)).getUTCDay();
    day=days-((last-r.d+7)%7);
  }else{
    day=1+((r.d-first+7)%7)+(r.w-1)*7;
  }
  var u=Date.UTC(year,r.m-1,day,r.h,r.min)/1000;
  return u-off*3600;
}
function rpOffsetAt(tz,t){
  if(!tz.hasDst){return tz.std}
  var base=t+tz.std*3600;
  var y=new Date(base*1000).getUTCFullYear();
  for(var k=-1;k<=1;k++){
    var yk=y+k,a=rpEpoch(tz.start,yk,tz.std),b=rpEpoch(tz.end,yk,tz.dst);
    var lo=a< b?a:b, hi=a<b?b:a;
    if(t>=lo&&t<hi){return a<b?tz.dst:tz.std}
  }
  return tz.std;
}
// The local calendar day of a timestamp, as "YYYY-MM-DD". The offset is added
// first, then the UTC getters read the local civil time - which is the whole
// trick, and the reason this is not a date object built from t directly.
function rpDayKey(tz,t){
  var d=new Date((t+rpOffsetAt(tz,t)*3600)*1000);
  var m=d.getUTCMonth()+1,y=d.getUTCFullYear();
  return y+'-'+(m<10?'0':'')+m+'-'+(d.getUTCDate()<10?'0':'')+d.getUTCDate();
}
function rpHm(tz,t){
  var d=new Date((t+rpOffsetAt(tz,t)*3600)*1000);
  var h=d.getUTCHours(),m=d.getUTCMinutes();
  return (h<10?'0':'')+h+':'+(m<10?'0':'')+m;
}
// "2026-10-25 13:00" - ISO on purpose: the page has no way of knowing which
// order the reader expects, and this one is the same in both languages.
function rpDateHm(tz,t){
  return rpDayKey(tz,t)+' '+rpHm(tz,t);
}
// --- civil dates ------------------------------------------------------------
// A day key is a calendar day, not an instant: shifting one by a day is
// arithmetic on the calendar and not on time, so no time zone enters here and
// no switch-over can move a day boundary.
function rpDayNum(k){
  var p=k.split('-');
  return Date.UTC(+p[0],+p[1]-1,+p[2])/1000;
}
function rpKeyOf(s){
  var d=new Date(s*1000),m=d.getUTCMonth()+1;
  return d.getUTCFullYear()+'-'+(m<10?'0':'')+m+'-'+(d.getUTCDate()<10?'0':'')+d.getUTCDate();
}
function rpShift(k,n){return rpKeyOf(rpDayNum(k)+n*86400)}
// One month forward or back, in months and not in days: the first of October
// minus 31 days is the 31st of August, so stepping back from a month lands in
// the wrong one - and from a February it lands in no month at all.
function rpShiftMonth(k,n){
  var p=k.split('-'),y=+p[0],m=+p[1]-1+n;
  y+=Math.floor(m/12);
  m=((m%12)+12)%12;
  return y+'-'+(m<9?'0':'')+(m+1)+'-01';
}
function rpDow(k){return new Date(rpDayNum(k)*1000).getUTCDay()}
function rpDaysInMonth(k){
  var p=k.split('-');
  return new Date(Date.UTC(+p[0],+p[1],0)).getUTCDate();
}
// {Y} {M} {D} - the pattern comes from the firmware, because the order a reader
// expects is a matter of language and not of logic.
function rpFmtDate(k,f){
  var p=k.split('-');
  return f.replace('{Y}',p[0]).replace('{M}',p[1]).replace('{D}',p[2]);
}
// --- the month file ----------------------------------------------------------
// One row out of a CSV file, as the numbers the chart needs.
//
// The six powers are the ones the panel computes in csvrow::toSample()
// (src/storage/CsvRow.h): the inverter's load meter already has the S0
// generation subtracted, so the consumption is meter plus external, and the
// production is the sum of the two strings. The same file has to give the same
// picture here as it gives on the panel.
//
// `s` is null for a row written before the sums were added to the format: the
// powers are in such a row, the seven lifetime counters are not.
function rpRows(text,names,tz){
  var ix={},i,k,out=[],lines=text.split('\n');
  for(i=0;i<names.length;i++){ix[names[i]]=i}
  for(i=0;i<lines.length;i++){
    var line=lines[i];
    if(line.length<12||line.indexOf('ts,')===0){continue}
    var f=line.split(',');
    var t=+f[ix.ts];
    if(!(t>1000000000)){continue}
    var sums=null;
    if(f[ix.grid_total_wh]!==undefined){
      sums=[+f[ix.pv_a_total_wh],+f[ix.pv_b_total_wh],+f[ix.ext_total_wh],
            +f[ix.load_total_wh],+f[ix.feed_total_wh],+f[ix.grid_total_wh]];
    }
    out.push({t:t,dk:rpDayKey(tz,t),
      v:[+f[ix.grid_l1]+ +f[ix.grid_l2]+ +f[ix.grid_l3],
         +f[ix.load_l1]+ +f[ix.load_l2]+ +f[ix.load_l3]+ +f[ix.s0],
         +f[ix.pv_a]+ +f[ix.pv_b],
         +f[ix.s0],+f[ix.bat],+f[ix.soc]],
      s:sums});
  }
  return out;
}
// First and last row of one calendar day, as indexes. Null when the day has no
// row at all - an empty day is not a zero day.
function rpSpan(rows,day){
  var a=-1,b=-1,i;
  for(i=0;i<rows.length;i++){
    if(rows[i].dk===day){if(a<0){a=i}b=i}
  }
  return a<0?null:[a,b];
}
// The energy of the rows of one period, in Wh.
//
// A difference of the lifetime counters, not a sum of momentary values: that is
// the size the device counts itself, and it stays right across a gap. The
// external generator is added to the production and to the consumption, exactly
// as the panel does (energyPeriodValues), and the own consumption is what stayed
// here: generated minus fed in.
//
// The feed-in counter arrives negative on the real device, so the magnitude of
// the difference is what went in - the same one place where the panel drops the
// sign.
//
// The difference is taken between the first and the last row that carries the
// sums. Rows without them are the ones written before the firmware added them,
// and a month that begins in that format still gets a figure instead of none;
// what such a period misses at its front end is named on the page.
//
// Decided per row and not per file: a month file created before the update
// carries the old header with the new rows behind it - on the development card
// 231 rows with 16 values and 333 with 23 in the same file - and a note taken
// from the header would claim missing sums for days that have them.
function rpEnergy(rows){
  var first=-1,last=-1,i,missing=0;
  for(i=0;i<rows.length;i++){
    if(!rows[i].s){missing=1;continue}
    if(first<0){first=i}
    last=i;
  }
  if(last-first<1){
    // Fewer than two rows with sums: there is no difference to take. The sums
    // are then 0, which is what the panel's own reader says (csvrow::parse()),
    // and the two rates stay empty - a period without counters has no share, and
    // "100 % self-sufficiency" would be an answer nobody asked for.
    return {values:{pv:0,own:0,feed:0,draw:0,load:0},autarky:null,
            ownShare:null,sums:false,missing:missing};
  }
  var s0=rows[first].s,s1=rows[last].s;
  var ext=Math.max(0,s1[2]-s0[2]);
  var pv=Math.max(0,(s1[0]-s0[0])+(s1[1]-s0[1]))+ext;
  var load=Math.max(0,s1[3]-s0[3])+ext;
  var feed=Math.abs(s1[4]-s0[4]);
  var grid=Math.max(0,s1[5]-s0[5]);
  var own=Math.max(0,pv-feed);
  return {values:{pv:pv,own:own,feed:feed,draw:grid,load:load},
          autarky:load>0?Math.max(0,1-grid/load)*100:100,
          ownShare:pv>0?Math.min(100,own/pv*100):0,
          sums:true,missing:missing};
}

// The gaps of a period, counted the way the panel counts them
// (histPush in src/gui/GuiApp.cpp): a sample that is more than one and a half
// intervals late means the slots in between were never written, and the time
// they stand for is what the panel reports under the chart. Without it a pause
// in the recording reads like a collapse.
function rpGaps(rows){
  var count=0,seconds=0,i,step=300;
  for(i=1;i<rows.length;i++){
    var dt=rows[i].t-rows[i-1].t;
    if(dt>step+step/2){
      var missing=Math.round(dt/step)-1;
      if(missing>0){count+=missing;seconds+=missing*step}
    }
  }
  return {count:count,minutes:Math.round(seconds/60)};
}
// The days a period covers, as day keys. The week starts on Monday, the way the
// manual names it; the month is the calendar month of the anchor day.
function rpDays(range,anchor){
  var out=[],i,k;
  if(range==='day'){return [anchor]}
  if(range==='week'){
    var start=rpShift(anchor,-((rpDow(anchor)+6)%7));
    for(i=0;i<7;i++){out.push(rpShift(start,i))}
    return out;
  }
  var y=anchor.substring(0,4),m=anchor.substring(5,7);
  for(i=1;i<=rpDaysInMonth(anchor);i++){out.push(y+'-'+m+'-'+(i<10?'0':'')+i)}
  return out;
}
// A band per day: the lowest and the highest value of each series, which is what
// five-minute samples over a week or a month can honestly be drawn as. A band
// is not a line through points that were never plotted.
function rpBands(rows,days){
  var out=[],i,k;
  for(i=0;i<days.length;i++){
    var sp=rpSpan(rows,days[i]),lo=null,hi=null;
    if(sp){
      for(k=sp[0];k<=sp[1];k++){
        var v=rows[k].v,j;
        if(lo===null){
          // One row starts both edges, and each of the six with its own value:
          // filling all six with the first would clamp every other series to
          // whatever the grid happened to be on that sample.
          lo=[v[0],v[0],v[0],v[0],v[0],v[0]];
          hi=[v[0],v[0],v[0],v[0],v[0],v[0]];
          for(j=0;j<6;j++){lo[j]=v[j];hi[j]=v[j]}
        }
        for(j=0;j<6;j++){
          if(v[j]<lo[j]){lo[j]=v[j]}
          if(v[j]>hi[j]){hi[j]=v[j]}
        }
      }
    }
    if(lo!==null){out.push({t:rpDayNum(days[i]),lo:lo,hi:hi})}
  }
  return out;
}
// The chart of one day: one point per sample, chronological. A sample that is
// not there is a point that is not there, and the line breaks over it.
function rpDayPoints(rows,day){
  var sp=rpSpan(rows,day),out=[],k;
  if(!sp){return out}
  for(k=sp[0];k<=sp[1];k++){out.push({t:rows[k].t,v:rows[k].v})}
  return out;
}
)";

// ---------------------------------------------------------------------------
// The chart script. It is only sent with the pages that have a chart (%J),
// together with the logic above it.
//
// The panel sends numbers, this draws them. Two rules shape it:
//
// 1. No library, no CDN. The page lives in the local network, and a download
//    from the internet would break the whole thing the moment the router has no
//    uplink - which is the normal case for a panel nobody looks at.
//
// 2. No text in here. Labels, headings and the failure message come from the
//    firmware as data-* attributes on the container, so a word can never be
//    spelled one way on the page and another way in the language table.
//
// The drawing is hand-made SVG and plain DOM: about 2 kB of flash for the whole
// thing, which is cheaper than the drawing library it would replace.
static const char kScript[] PROGMEM = R"(
function rpSep(el,n){var v=el.getAttribute(n);return v?v.charAt(0):'.'}
function rpSplit(s){return s?s.split('|'):[]}
function rpEsc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;')}
function rpNum(v,d,sep){var s=Number(v).toFixed(d);return sep==='.'?s:s.replace('.',sep)}
// kWh below a megawatt hour, MWh above - the same split the panel's energy bars
// make, so a value reads the same in both places.
function rpWh(wh,sep){
  if(wh===null||wh===undefined||wh!==wh)return '--';
  if(Math.abs(wh)<1000000)return rpNum(wh/1000,1,sep)+' kWh';
  return rpNum(wh/1000000,2,sep)+' MWh';
}
// A percentage, or a dash when there is none.
function rpRate(x,sep){
  if(x===null||x===undefined||x!==x)return '--';
  return rpNum(x,1,sep)+' %';
}
function rpFail(el){
  el.innerHTML='<div class="note bad">'+rpEsc(el.getAttribute('data-err'))+'</div>';
}
// --- the 24 h chart -----------------------------------------------------------
// The same layout as the panel's own chart: five power lines on one axis with
// the zero line and the two extremes marked, and the state of charge on its own
// 0..100 axis over the full height. The viewBox is 360 wide, so at a phone's
// width the scale is about 1:1 and the axis labels stay as big as the rest of
// the text; on a wide screen the whole thing scales up, which is what a vector
// drawing is for.
// The plot area inside the viewBox. The gutters are for the axis labels, which
// are written the way they would be written in running text: the number, then
// the unit behind it ("10,0 kW"), and on the right the scale of the state of
// charge from 0 % to 100 %.
var rpVX0=48,rpVY0=10,rpVW=272,rpVH=176;
function rpChart(el,j){
  var tz=rpTz(j.tz);
  var labs=rpSplit(el.getAttribute('data-lab'));
  // The colours come as a comma-separated list, the labels as one; two
  // separators, because a label may not contain a bar and a colour may not.
  var cols=(el.getAttribute('data-col')||'').split(',');
  var sep=rpSep(el,'data-sep');
  var pts=j.data||[];
  // Two shapes. "line" is one point per sample (24 h, one day); "band" is one
  // lowest and one highest value per day (a week, a month), because five-minute
  // samples over a month cannot honestly be drawn as a line through points.
  var band=j.mode==='band';
  var n=pts.length,first=null,last=null,i,k,p;
  var lo=0,hi=0,seen=false;
  var look=function(w){
    if(w===null||w===undefined||!(w===w)){return}
    if(!seen){lo=hi=w;seen=true}
    if(w<lo){lo=w}
    if(w>hi){hi=w}
  };
  for(i=0;i<n;i++){
    p=pts[i];
    if(!p){continue}
    if(first===null){first=p.t}
    last=p.t;
    for(k=0;k<5;k++){ // the first five are powers; the sixth is the SOC
      if(band){look(p.lo[k]);look(p.hi[k])}else{look(p.v[k])}
    }
  }
  if(first===null){
    el.innerHTML='<div class="note">'+rpEsc(el.getAttribute('data-none'))+'</div>';
    return;
  }
  // The zero line stays in view whatever the data does, and the steps are the
  // panel's own (250 W steps up to 2 kW, then 500, 1000, 2000).
  if(lo>0){lo=0}
  if(hi<0){hi=0}
  var span=hi-lo;
  var step=span<2000?250:(span<4000?500:(span<10000?1000:2000));
  var mlo=Math.floor(lo/step)*step,mhi=Math.ceil(hi/step)*step;
  if(mlo===mhi){mhi=mlo+step}
  // A band stands for a whole day, so it is drawn in the middle of it; a point
  // sits at its own timestamp.
  var t0=band?first-12*3600:first,t1=last+(band?12*3600:1);
  var xOf=function(t){return rpVX0+(t-t0)/(t1-t0)*rpVW};
  var yOf=function(v){return rpVY0+(mhi-v)/(mhi-mlo)*rpVH};
  // The SOC has its own axis, 0..100 over the full height.
  var ySoc=function(v){return rpVY0+(100-v)/100*rpVH};
  var h='<svg viewBox="0 0 360 208">';
  // The unit of the state of charge comes from the answer, so it is the one the
  // panel sent.
  var socUnit=(j.unit&&j.unit[5])?j.unit[5]:'%';
  // The three scale markers, inside the plot on the left, like on the panel -
  // with the unit behind the number, the way it would be written in text.
  var marks=[mhi,0,mlo];
  for(i=0;i<marks.length;i++){
    var ym=yOf(marks[i]);
    h+='<line x1="'+rpVX0+'" y1="'+ym.toFixed(1)+'" x2="'+(rpVX0+rpVW)+'" y2="'+ym.toFixed(1)+
       '" stroke="#e3e7eb" stroke-width="1"/>';
    h+='<text x="'+(rpVX0-5)+'" y="'+(ym+3).toFixed(1)+'" text-anchor="end" font-size="11" fill="#5a6672">'+
       rpEsc(rpNum(marks[i]/1000,1,sep)+' kW')+'</text>';
  }
  // The scale of the state of charge at the right edge, where its series runs.
  h+='<text x="'+(rpVX0+rpVW+6)+'" y="'+(rpVY0+7)+'" font-size="11" fill="#5a6672">100 '+rpEsc(socUnit)+'</text>';
  h+='<text x="'+(rpVX0+rpVW+6)+'" y="'+(rpVY0+rpVH)+'" font-size="11" fill="#5a6672">0 '+rpEsc(socUnit)+'</text>';
  if(band){
    // The days along the bottom, thinned out so the labels cannot collide. The
    // short pattern drops the year: a row of days either all share one or none
    // has it.
    var sf=el.getAttribute('data-sfmt'),every=Math.ceil(n/6);
    for(i=0;i<n;i++){
      if(i%every!==0){continue}
      var day=rpKeyOf(pts[i].t+12*3600);
      h+='<text x="'+xOf(pts[i].t+12*3600).toFixed(1)+'" y="202" text-anchor="middle" font-size="11" fill="#5a6672">'+
         rpEsc(rpFmtDate(day,sf||fmt))+'</text>';
    }
  }else{
    // The time of day, at the panel's own zone and not at the browser's: the
    // same installation, but a phone abroad would otherwise put the labels an
    // hour or two off.
    var stepT=6*3600;
    for(var t=Math.ceil(t0/stepT)*stepT;t<=t1;t+=stepT){
      h+='<text x="'+xOf(t).toFixed(1)+'" y="202" text-anchor="middle" font-size="11" fill="#5a6672">'+
         rpEsc(rpHm(tz,t))+'</text>';
    }
  }
  // The lines. A point without a sample lifts the pen: the line is broken there
  // instead of closing the gap over a period that was never measured. A band
  // is the shortest line that says "between these two values", one per day -
  // and the six are put side by side, because six bands on the same day would
  // otherwise hide each other.
  var dayW=n>1?rpVW/(n-1):rpVW;
  var gap=Math.min(4,dayW/6);
  for(k=0;k<6;k++){
    var d='',pen=false,yFn=(k===5)?ySoc:yOf;
    if(band){
      for(i=0;i<n;i++){
        p=pts[i];
        if(!p){continue}
        var a=yFn(p.lo[k]),b=yFn(p.hi[k]);
        if(!(a===a)||!(b===b)){continue}
        var xb=(xOf(p.t+12*3600)+(k-2.5)*gap).toFixed(1);
        d+='M'+xb+' '+Math.min(a,b).toFixed(1)+'L'+xb+' '+Math.max(a,b).toFixed(1);
      }
    }else{
      for(i=0;i<n;i++){
        p=pts[i];
        if(!p){pen=false;continue}
        var val=p.v[k];
        if(val===null||val===undefined||!(val===val)){pen=false;continue}
        d+=(pen?'L':'M')+xOf(p.t).toFixed(1)+' '+yFn(val).toFixed(1);
        pen=true;
      }
    }
    if(d){
      h+='<path d="'+d+'" fill="none" stroke="#'+cols[k]+'" stroke-width="1.3"'+
         (k===5?' stroke-dasharray="3 2"':'')+' stroke-linejoin="round"/>';
    }
  }
  h+='</svg>';
  // The legend below the drawing, where it can wrap instead of running into the
  // edge of the plot.
  var lg='<div class="legend">';
  for(k=0;k<labs.length;k++){
    lg+='<span><i style="background:#'+cols[k]+'"></i>'+rpEsc(labs[k])+'</span>';
  }
  lg+='</div>';
  // When the newest sample was taken. A chart that keeps itself up to date says
  // so with a number rather than only with a moving line.
  var sf=el.getAttribute('data-stampfmt');
  if(sf){
    // The newest sample, not the newest shape: in the band view the last point
    // stands for a whole day and its own timestamp would be midday.
    lg+='<p class="stamp">'+rpEsc(sf.replace('%s',rpDateHm(tz,(j.stamp||last))))+'</p>';
  }
  el.innerHTML='<div class="chart">'+h+'</div>'+lg;
}
var rpTick=0;
function rpLoadChart(el){
  // The counter keeps the answer out of a cache: this runs every 5 s for as
  // long as the page is open.
  fetch('/api/verlauf.json?k='+(rpTick++)).then(function(r){
    if(!r.ok){throw new Error(r.status)}
    return r.json();
  }).then(function(j){
    rpChart(el,j);
    el.setAttribute('data-stamp',j.to||0);
    // The gaps of the window: the ring keeps a slot per sample and leaves the
    // ones empty that were never filled, so counting the empty slots counts
    // the gaps - the same number the panel shows under its own chart.
    var n=0,pts=j.data||[],i;
    for(i=0;i<pts.length;i++){if(!pts[i]){n++}}
    rpShowGaps(el,n,n*5);
  }).catch(function(){
    rpFail(el);
  });
}
// The sentence about the gaps, in the wording of the panel (T_D_GAP_ONE /
// T_D_GAP_MANY). Nothing at all when there were none: a line saying "0 gaps" is
// noise on a day that was recorded completely.
function rpShowGaps(el,count,minutes){
  var box=document.getElementById('luecken');
  if(!box){return}
  if(!count){box.innerHTML='';return}
  var t=(count===1)?(el.getAttribute('data-gap1')||''):(el.getAttribute('data-gapn')||'');
  box.innerHTML='<p class="stamp">'+rpEsc(t.replace('%lu',minutes).replace('%d',count))+'</p>';
}
// --- the history from the CSV -------------------------------------------------
// A month file, loaded once and then kept in memory under its full file name -
// the month included, because a file called RCT-202609.csv is a different thing
// from one called RCT-202610.csv and a cache keyed on the month alone would
// hand out the wrong one.
//
// One file at a time: the panel serves one download at a time and answers a
// second request with "busy", so asking for two months side by side would fail
// on one of them.
var rpFiles={};
function rpFile(key,names,tz,cb){
  var have=rpFiles[key];
  if(have){cb(have);return}
  fetch('/daten/'+encodeURIComponent(key)).then(function(r){
    if(!r.ok){throw new Error(r.status)}
    return r.text();
  }).then(function(txt){
    var f={key:key,rows:rpRows(txt,names,tz)};
    rpFiles[key]=f;
    cb(f);
  }).catch(function(){
    cb(null);
  });
}
function rpEnergyBars(el,j){
  var sep=rpSep(el,'data-sep');
  var keys=rpSplit(el.getAttribute('data-key'));
  var labs=rpSplit(el.getAttribute('data-lab'));
  var rates=rpSplit(el.getAttribute('data-r'));
  var h='',i,v,mx=0,vals=[];
  for(i=0;i<keys.length;i++){
    v=j.values[keys[i]];
    if(typeof v!=='number'){v=0}
    vals.push(v);
    if(v>mx){mx=v}
  }
  if(mx<=0){mx=1}
  if(rates.length>=2){
    // A rate that is not there is written as a dash, not as a number: the
    // period has no counters, and "100 %" would be an answer to a question
    // nobody asked.
    h+='<div class="rates"><div><b>'+rpRate(j.autarky,sep)+'</b>'+rpEsc(rates[0])+'</div>'
      +'<div><b>'+rpRate(j.ownShare,sep)+'</b>'+rpEsc(rates[1])+'</div></div>';
  }
  for(i=0;i<vals.length;i++){
    h+='<div class="bar b'+i+'"><div class="l"><span>'+rpEsc(labs[i]||'')+'</span>'
      +'<b>'+rpWh(j.values[keys[i]],sep)+'</b></div><div class="t"><div class="f" '
      +'style="width:'+Math.round(vals[i]/mx*100)+'%"></div></div></div>';
  }
  el.innerHTML=h;
}
function rpLoadEnergy(el){
  var p=el.getAttribute('data-period')||'tag';
  fetch('/api/energie.json?zeitraum='+encodeURIComponent(p)).then(function(r){
    if(!r.ok){throw new Error(r.status)}
    return r.json();
  }).then(function(j){
    rpEnergyBars(el,j);
  }).catch(function(){
    rpFail(el);
  });
}
function rpBoot(){
  var el=document.getElementById('energie');
  if(el){
    var seg=document.getElementById('per');
    if(seg){
      seg.addEventListener('click',function(ev){
        var b=ev.target;
        if(!b||b.tagName!=='BUTTON'){return}
        var bs=seg.getElementsByTagName('button');
        for(var i=0;i<bs.length;i++){
          bs[i].className=(bs[i]===b?'on':'');
        }
        el.setAttribute('data-period',b.getAttribute('data-p'));
        rpLoadEnergy(el);
      });
    }
    rpLoadEnergy(el);
  }
  rpVerlauf();
}
// The 24 h chart and the history browser. One page, one state: which period is
// shown and which day it is anchored to.
function rpVerlauf(){
  var el=document.getElementById('verlauf');
  if(!el){return}
  var seg=document.getElementById('range');
  var nav=document.getElementById('nav');
  var lab=document.getElementById('rangetext');
  var per=document.getElementById('periode');
  // The column names come from the firmware as the header line of the CSV,
  // which is comma-separated: one string, split once, read by name.
  var names=(el.getAttribute('data-cols')||'').split(',');
  var fmt=el.getAttribute('data-dfmt');
  var rule=null;                    // the POSIX rule, from the first answer
  var st={range:'live',day:null,follow:true}; // the period, its day, and whether
                                            // it still follows the day
  var live=null;                    // the timer of the live view

  function stopLive(){
    if(live){clearInterval(live);live=null}
  }
  // The time zone rule comes with every answer; one is enough, and it is needed
  // before a day can be named at all.
  function needRule(cb){
    if(rule){cb();return}
    fetch('/api/verlauf.json').then(function(r){return r.json()}).then(function(j){
      rule=j.tz;
      cb();
    }).catch(function(){
      rpFail(el);
    });
  }
  function rday(){
    // Without the rule there is no day to name: the first answer brings it, and
    // until then the switch does nothing. A click in that moment used to throw.
    if(!rule){return ''}
    return rpDayKey(rpTz(rule),Math.floor(Date.now()/1000));
  }
  function setButtons(){
    if(!seg){return}
    var bs=seg.getElementsByTagName('button'),i;
    for(i=0;i<bs.length;i++){
      bs[i].className=(bs[i].getAttribute('data-r')===st.range?'on':'');
    }
  }
  // The 24 h ring, from the panel itself: it is in the panel's memory, and it
  // keeps itself up to date. Nothing on this page is being typed into, so a
  // drawing that never refreshes would be stale almost by definition.
  function liveView(){
    rpLoadChart(el);
    live=setInterval(function(){rpLoadChart(el);},5000);
    if(lab){lab.textContent=el.getAttribute('data-live')}
    if(nav){nav.style.display='none'}
    if(per){per.innerHTML=''}
    var box=document.getElementById('luecken');
    if(box){box.innerHTML=''}
  }
  // One period out of the CSV files. The files it needs arrive one after the
  // other and then stay in memory, so walking through the history costs no
  // further access to the panel.
  function fromFile(){
    var days=rpDays(st.range,st.day),months=[],byMonth={},all=[],i,a,found=0;
    for(i=0;i<days.length;i++){
      var m=days[i].substring(0,7);
      if(months.indexOf(m)<0){months.push(m)}
    }
    el.innerHTML='<div class="note">'+rpEsc(el.getAttribute('data-load'))+'</div>';
    var step=function(k){
      if(k<months.length){
        rpFile('RCT-'+months[k].replace('-','')+'.csv',names,rpTz(rule),function(f){
          byMonth[months[k]]=f?f.rows:null;
          if(f){found=1}
          step(k+1);
        });
        return;
      }
      // The rows of the period in chronological order. A week can cross a month
      // and a month can cross a year, which is why there is more than one file.
      for(i=0;i<days.length;i++){
        var rows=byMonth[days[i].substring(0,7)];
        if(!rows){continue}
        var sp=rpSpan(rows,days[i]);
        if(sp){for(a=sp[0];a<=sp[1];a++){all.push(rows[a])}}
      }
      all.sort(function(x,y){return x.t-y.t});
      draw(all,days,found);
    };
    step(0);
  }
  function draw(rows,days,found){
    var band=st.range!=='day',pts=[],i;
    // Two different "nothing here": the card has no file for this period at
    // all, or the file has no rows in it. One sentence each, because "no
    // measurements" for a month that was never recorded would read as a gap in
    // the recording.
    var empty=found?(el.getAttribute('data-none')):(el.getAttribute('data-nofile'));
    if(band){
      pts=rpBands(rows,days);
    }else{
      for(i=0;i<rows.length;i++){pts.push({t:rows[i].t,v:rows[i].v})}
    }
    if(!pts.length){
      el.innerHTML='<div class="note">'+rpEsc(empty)+'</div>';
    }else{
      rpChart(el,{tz:rule,mode:band?'band':'line',data:pts,
                  stamp:rows.length?rows[rows.length-1].t:0});
    }
    if(lab){lab.textContent=rangeText(days)}
    if(nav){nav.style.display='flex'}
    var nx=document.getElementById('next');
    if(nx){nx.disabled=days[days.length-1]>=rday()}
    // The energy of the period, and with it the answer to the one question the
    // period cannot answer by itself: are there rows in it without the sums?
    var e=rows.length?rpEnergy(rows):null;
    if(per){
      if(e){rpEnergyBars(per,e)}
      else{per.innerHTML='<div class="note">'+rpEsc(empty)+'</div>'}
    }
    var nt=document.getElementById('hinweis');
    if(nt){
      nt.innerHTML=(e&&e.missing)
        ?'<div class="note">'+rpEsc(nt.getAttribute('data-old'))+'</div>':'';
    }
    var g=rpGaps(rows);
    rpShowGaps(el,g.count,g.minutes);
  }
  function rangeText(days){
    if(st.range==='day'){return rpFmtDate(days[0],fmt)}
    // A week and a month are shown as the span they cover. The year is written
    // once, at the front, and dropped at the back when it is the same year -
    // which also keeps the line inside a narrow phone.
    var first=rpFmtDate(days[0],fmt),last=days[days.length-1];
    var tail=(last.substring(0,4)===days[0].substring(0,4))
      ?rpFmtDate(last,el.getAttribute('data-sfmt')):rpFmtDate(last,fmt);
    return first+' - '+tail;
  }
  function show(){
    stopLive();
    needRule(function(){
      setButtons();
      // A page left open across midnight follows the day - but only while the
      // reader has not stepped back, or every step would be undone by the next
      // redraw.
      var t=rday();
      if(t===''){return}
      if(st.day===null||st.follow){st.day=t}
      if(st.range==='live'){liveView();return}
      fromFile();
    });
  }
  function step(n){
    if(st.day===''||!st.day){return}
    if(st.range==='month'){
      st.day=rpShiftMonth(st.day,n);
    }else{
      st.day=rpShift(st.day,n<0?-(st.range==='day'?1:7):(st.range==='day'?1:7));
    }
    st.follow=(st.day===rday());
    show();
  }
  if(seg){
    seg.addEventListener('click',function(ev){
      var b=ev.target;
      if(!b||b.tagName!=='BUTTON'){return}
      var t=rday();
      if(t===''){return}
      st.range=b.getAttribute('data-r');
      st.day=t;
      st.follow=true;
      show();
    });
  }
  var pv=document.getElementById('prev');
  var nx=document.getElementById('next');
  if(pv){pv.addEventListener('click',function(){step(-1)})}
  if(nx){nx.addEventListener('click',function(){step(1)})}
  show();
}
if(document.readyState==='loading'){
  document.addEventListener('DOMContentLoaded',rpBoot);
}else{
  rpBoot();
}
)";

} // namespace web

#endif // RCT_WEB_PAGES_H