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
.chart svg{display:block;width:100%;height:auto}
.legend{font-size:13px;color:#5a6672;margin:6px 2px 0}
.legend i{display:inline-block;width:11px;height:3px;border-radius:2px;margin:0 4px 3px 10px;vertical-align:middle}
.legend i:first-child{margin-left:2px}
.note.bad{background:#fdecea;border-color:#f0b8b3}
)" ;

// The navigation words and the page titles are not here: they are texts, and
// texts live in src/i18n (see Lang.h), one table per language.
//
// ---------------------------------------------------------------------------
// The chart script. It is only sent with the pages that have a chart (%J).
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
}
if(document.readyState==='loading'){
  document.addEventListener('DOMContentLoaded',rpBoot);
}else{
  rpBoot();
}
)";

} // namespace web

#endif // RCT_WEB_PAGES_H