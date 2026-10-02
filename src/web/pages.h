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
// %R by a refresh tag, %S by the style block, %J by the chart script (empty on
// the pages without a chart), %B by the body.
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
var rpVX0=34,rpVY0=8,rpVW=320,rpVH=182;   // plot area inside the viewBox
function rpChart(el,j){
  var tz=rpTz(j.tz);
  var labs=rpSplit(el.getAttribute('data-lab'));
  // The colours come as a comma-separated list, the labels as one; two
  // separators, because a label may not contain a bar and a colour may not.
  var cols=(el.getAttribute('data-col')||'').split(',');
  var sep=rpSep(el,'data-sep');
  var pts=j.data||[];
  var n=pts.length,first=null,last=null,i,k,p;
  var lo=0,hi=0,seen=false;
  for(i=0;i<n;i++){
    p=pts[i];
    if(!p){continue}
    if(first===null){first=p.t}
    last=p.t;
    for(k=0;k<5;k++){ // the first five are powers; the sixth is the SOC
      var v=p.v[k];
      if(v===null||v===undefined){continue}
      if(!seen){lo=hi=v;seen=true}
      if(v<lo){lo=v}
      if(v>hi){hi=v}
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
  var t0=first,t1=last>first?last:first+1;
  var xOf=function(t){return rpVX0+(t-t0)/(t1-t0)*rpVW};
  var yOf=function(v){return rpVY0+(mhi-v)/(mhi-mlo)*rpVH};
  var h='<svg viewBox="0 0 360 208">';
  // The three scale markers, inside the plot on the left, like on the panel.
  var marks=[mhi,0,mlo];
  for(i=0;i<marks.length;i++){
    var ym=yOf(marks[i]);
    h+='<line x1="'+rpVX0+'" y1="'+ym.toFixed(1)+'" x2="'+(rpVX0+rpVW)+'" y2="'+ym.toFixed(1)+
       '" stroke="#e3e7eb" stroke-width="1"/>';
    h+='<text x="'+(rpVX0-4)+'" y="'+(ym+3).toFixed(1)+'" text-anchor="end" font-size="9" fill="#5a6672">'+
       rpEsc(rpNum(marks[i]>=0?marks[i]/1000:marks[i]/1000,1,sep))+'</text>';
  }
  // Hour marks along the bottom: the time of day at the panel's own zone, not
  // the browser's - they are the same installation, but a phone abroad would
  // otherwise put the labels an hour or two off.
  var stepT=6*3600;
  for(var t=Math.ceil(t0/stepT)*stepT;t<=t1;t+=stepT){
    h+='<text x="'+xOf(t).toFixed(1)+'" y="202" text-anchor="middle" font-size="9" fill="#5a6672">'+
       rpEsc(rpHm(tz,t))+'</text>';
  }
  // The lines. A point without a sample lifts the pen: the line is broken there
  // instead of closing the gap over a period that was never measured.
  for(k=0;k<6;k++){
    var d='',pen=false,yFn;
    if(k===5){
      // The SOC has its own axis, 0..100 over the full height.
      yFn=function(v){return rpVY0+(100-v)/100*rpVH};
    }else{
      yFn=yOf;
    }
    for(i=0;i<n;i++){
      p=pts[i];
      if(!p){pen=false;continue}
      var val=p.v[k];
      if(val===null||val===undefined){pen=false;continue}
      d+=(pen?'L':'M')+xOf(p.t).toFixed(1)+' '+yFn(val).toFixed(1);
      pen=true;
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
    lg+='<p class="stamp">'+rpEsc(sf.replace('%s',rpDateHm(tz,last)))+'</p>';
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
  }).catch(function(){
    rpFail(el);
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
    h+='<div class="rates"><div><b>'+rpNum(j.autarky,1,sep)+' %</b>'+rpEsc(rates[0])+'</div>'
      +'<div><b>'+rpNum(j.ownShare,1,sep)+' %</b>'+rpEsc(rates[1])+'</div></div>';
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
  // The 24 h chart keeps itself up to date. Unlike the overview this page has
  // nothing in it that someone could be halfway through typing, and the newest
  // sample arrives every five minutes - so a static drawing would be stale
  // almost by definition.
  var ch=document.getElementById('verlauf');
  if(ch){
    rpLoadChart(ch);
    setInterval(function(){rpLoadChart(ch);},5000);
  }
}
if(document.readyState==='loading'){
  document.addEventListener('DOMContentLoaded',rpBoot);
}else{
  rpBoot();
}
)";

} // namespace web

#endif // RCT_WEB_PAGES_H