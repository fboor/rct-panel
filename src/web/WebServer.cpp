// Web interface for normal operation - see docs/web-interface.md and
// src/web/WebServer.h for the design.
//
// Four routes, served by one WebServer on port 80 that is pumped from loop():
//
//   /            overview
//   /daten       the logged CSV files, tail or whole
//   /bilder      the screenshots taken on the panel
//   /update      firmware upload (OTA)
//
// Four rules shape the code:
//
// 1. The card belongs to the SD worker. A download asks the worker for one chunk
//    per loop iteration and writes that chunk to the socket. Nothing here ever
//    blocks on the card, so a download cannot freeze the panel.
// 2. Routes that only read are open; everything that changes the panel (update,
//    restart, provisioning) needs the 4-digit code, which only exists on the
//    panel's own display.
// 3. Every download goes out with the real Content-Length, so the browser shows
//    a true progress bar and knows when it is complete.
// 4. One open stream at a time (a single card handle, a single chunk buffer).
//    A second request gets a 409 rather than a corrupted download.
//
// SPDX-License-Identifier: MIT
#include "web/WebServer.h"

#include "../Diag.h"
#include "../NumFmt.h"
#include "../config/Configuration.h"
#include "../output/Relay.h"
#include "../rct/RctTypes.h"
#include "../storage/sdlog.h"
#include "pages.h"

#include <ESPmDNS.h>
#include <math.h>
#include <string.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>
#include <uri/UriBraces.h>

#include <stdlib.h>

// Shown on the overview so a user can tell which panel build they are looking
// at (and so a support case can be pinned to a firmware).
static const char kPanelVersion[] = "1.0 (2026-09)";

// Upper bound for a firmware image: the app slots in partitions/16mb_app.csv are
// 7 MB. Checked before writing, so a wrong file cannot waste ten minutes of
// upload time and then fail.
static const uint32_t kMaxFirmware = 7340032u;

// "tail=..." is clamped to this. A month of CSV at 5-minute rows is about
// 300 kB, so 1 MB covers every file this panel can produce.
static const uint32_t kMaxTail = 1048576u;

// The functions the switched output can follow, in the order of RelayMode and
// therefore in the order the panel cycles through them. The same text is used
// for the read-out and for the select, so the two can never disagree.
static const char *const kRelayModeName[kRelayModeCount] = {
    "Aus (schaltet nie)", "Netzbezug &uuml;ber Schwelle",
    "&Uuml;berschuss &uuml;ber Schwelle", "St&ouml;rung am Wechselrichter",
    "Inselbetrieb (Netz getrennt)"};

namespace {

// Household load over the three phases - the panel's own meter, summed here so
// the overview shows one number instead of three.
float loadSum(const RctSnapshot &s) {
  return s.loadPower[0] + s.loadPower[1] + s.loadPower[2];
}

WebServer s_server(80);
bool s_serverStarted = false;
bool s_mdnsStarted = false;

// The open stream, if any. Kind says which directory it came from; the worker
// owns the file itself.
enum class StreamKind { None, Csv, Shot };
StreamKind s_streamKind = StreamKind::None;
char s_streamFile[32] = {0};
uint32_t s_streamTail = 0;
uint32_t s_streamLen = 0;
uint32_t s_streamOpenedMs = 0;
bool s_streamHeaderSent = false;
// The piece that is on its way to the socket. sendContent() can accept less than
// offered (a full TCP window), so what is left of it stays here and goes out on
// the next iterations - a partial write must not lose bytes.
// The body goes through our own copy of the client instead of
// WebServer::sendContent(), which returns void in this core and discards the byte
// count. WiFiClient is reference counted (shared_ptr socket handle), so the copy
// talks to the very same socket the server holds, and the write's return value
// says what actually got out - which is also how a browser that went away is
// noticed, instead of writing into a closed socket for minutes.
WiFiClient s_client;
uint8_t s_sendBuf[2048];
size_t s_sendLen = 0;
size_t s_sendPos = 0;
uint32_t s_lastWriteMs = 0;

// The directory listing the card worker last read, copied out of its cache.
char s_listing[2048];
size_t s_listingLen = 0;

// Firmware upload state. Update has no internal mutex, so it is used from one
// context only - the loop task, which is also where these handlers run.
bool s_otaOpen = false;  // an upload is in flight (Update.begin succeeded)
bool s_otaOk = false;    // so far no error
uint32_t s_otaBytes = 0;
uint32_t s_otaTotal = 0;
uint32_t s_otaStartMs = 0;
// The gating code, 4 digits, fresh for every boot.
char s_code[5] = {0};

// Hands the display back to the panel while a download is being pumped out of a
// handler. Installed by main.cpp, the same way rctSetYieldHook is - the web
// module stays free of LVGL, and the hook is guaranteed to run in the task that
// called the handler.
static void (*s_yieldHook)() = nullptr;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// HTML-escape into a fixed buffer. The values that reach the pages are file
// names, version strings and device names - all from the card or the device,
// none of them trusted.
//
// The longest replacement is "&quot;" (6 characters), so each step needs room for
// that plus the terminator. A buffer too small for even one character yields an
// empty string - which is legal here, and the reason the code check used to
// refuse every code: it escaped into 8 bytes, and 8 is not more than the reserve
// it keeps. Every caller here passes 20 bytes or more.
void escape(const char *in, char *out, size_t cap) {
  size_t n = 0;
  if (out == nullptr || cap == 0) {
    return;
  }
  for (size_t i = 0; in != nullptr && in[i] != '\0' && n + 1 < cap; i++) {
    switch (in[i]) {
    case '&': n += (size_t)snprintf(out + n, cap - n, "&amp;"); break;
    case '<': n += (size_t)snprintf(out + n, cap - n, "&lt;"); break;
    case '>': n += (size_t)snprintf(out + n, cap - n, "&gt;"); break;
    case '"': n += (size_t)snprintf(out + n, cap - n, "&quot;"); break;
    default: out[n++] = in[i]; break;
    }
  }
  out[n] = '\0';
}

// A file size for the two listing pages, in the unit a file manager uses:
// 1024 steps, and the step that is named is the one the number sits in - kB
// below a megabyte, MB above. Divided by 1000 instead, a 19 kB month file claims
// 19 kB and a card that is 16 GB according to its label shows 14,9 - the number
// then does not match anything the user can see anywhere else.
//
// The decimal point is written as a comma, like every other number on these
// pages.
void humanSize(uint32_t bytes, char *out, size_t cap) {
  if (bytes >= 1024u * 1024u) {
    fmtNumComma(out, cap, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
  } else if (bytes >= 1024u) {
    fmtNumComma(out, cap, "%.1f kB", (double)bytes / 1024.0);
  } else {
    snprintf(out, cap, "%u B", (unsigned)bytes);
  }
}

// A short message page, for errors and confirmations. No navigation strip: these
// are answers to an action, not somewhere to wander off to.
void sendMsg(int code, const char *msg) {
  String body;
  body.reserve(360);
  body = F("<main><div class=\"note\">");
  body += msg;
  body += F("</div><p><a class=\"lnk\" href=\"/\">Zur Startseite</a></p></main>");
  String page;
  page.reserve(strlen_P(web::kShell) + strlen_P(web::kStyle) + body.length() + 64);
  page = FPSTR(web::kShell);
  page.replace("%T", String("RCT Power Panel"));
  page.replace("%S", FPSTR(web::kStyle));
  // %B goes in last: the body carries values with percent signs and units, and
  // replacing the shell's tokens after that would read those as tokens.
  page.replace("%B", body);
  s_server.send(code, "text/html; charset=utf-8", page);
}

// Navigation strip, shared by the four real pages.
void addNav(String &page, const char *current) {
  page += F("<nav>");
  struct {
    const char *href;
    const char *label;
  } items[] = {{"/", web::kNavHome},   {"/daten", web::kNavData},
               {"/bilder", web::kNavShots}, {"/update", web::kNavFw}};
  for (const auto &it : items) {
    page += F("<a href=\"");
    page += it.href;
    page += F("\"");
    if (strcmp(current, it.href) == 0) {
      page += F(" class=\"on\"");
    }
    page += F(">");
    page += it.label;
    page += F("</a>");
  }
  page += F("</nav>");
}

// header + nav + main + body, sent as one page.
void sendNavPage(const char *title, const char *current, const String &body) {
  char t[48];
  escape(title, t, sizeof(t));
  String frame;
  frame.reserve(body.length() + 320);
  frame = F("<header><h1>");
  frame += t;
  frame += F("</h1></header>");
  addNav(frame, current);
  frame += F("<main>");
  frame += body;
  frame += F("</main>");
  String page;
  page.reserve(strlen_P(web::kShell) + strlen_P(web::kStyle) + frame.length() +
               strlen(t) + 64);
  page = FPSTR(web::kShell);
  page.replace("%T", String(t));
  page.replace("%S", FPSTR(web::kStyle));
  // %B goes in last: the body carries values with percent signs and units, and
  // replacing the shell's tokens after that would read those as tokens.
  page.replace("%B", frame);
  s_server.send(200, "text/html; charset=utf-8", page);
}

// ---------------------------------------------------------------------------
// /  Overview
// ---------------------------------------------------------------------------

void handleRoot() {
  const RctSnapshot &s = rctState;
  char v[40];
  String b;
  b.reserve(3000);

  b += F("<div class=\"big\">");
  auto card = [&b, &v](const char *label, const char *value) {
    b += F("<div class=\"card\"><div class=\"l\">");
    b += label;
    b += F("</div><div class=\"n\">");
    b += value;
    b += F("</div></div>");
  };
  // Sign convention as on the panel and in the manual: net positive = draw
  // from the grid, negative = feed-in. fmtNumComma: comma as the decimal
  // separator, and no "-0,00 kW" for a grid power that is a rounding error
  // below zero.
  fmtNumComma(v, sizeof(v), "%.2f kW", (double)s.gridPowerSum / 1000.0);
  card("Netz", v);
  fmtNumComma(v, sizeof(v), "%.2f kW",
             (double)(s.pvPower[0] + s.pvPower[1] + s.s0Power) / 1000.0);
  card("PV", v);
  fmtNumComma(v, sizeof(v), "%.0f %%", (double)s.batterySoc);
  card("Batterie", v);
  fmtNumComma(v, sizeof(v), "%.0f W", (double)loadSum(s));
  card("Verbrauch", v);
  b += F("</div>");

  b += F("<h2>Ger&auml;t</h2><table>");
  auto row = [&b, &v](const char *k, const char *value) {
    b += F("<tr><td class=\"k\">");
    b += k;
    b += F("</td><td class=\"v\">");
    b += value;
    b += F("</td></tr>");
  };
  char escName[48];
  row("Wechselrichter", s.connected ? "verbunden" : "nicht erreichbar");
  escape(s.firmwareVersion, escName, sizeof(escName));
  row("Steuerger&auml;t", escName[0] ? escName : "--");
  row("Firmware Panel", kPanelVersion);
  snprintf(v, sizeof(v), "%lu s", (unsigned long)(millis() / 1000));
  row("Laufzeit", v);
  snprintf(v, sizeof(v), "%u kB", (unsigned)(ESP.getFreeHeap() / 1024));
  row("Speicher frei", v);
  row("SD-Karte", sdStatusText());

  const uint32_t hz = sdSpiHz();
  if (hz == 0) {
    row("SD-Takt", "keine Karte");
  } else if (hz >= 1000000u) {
    snprintf(v, sizeof(v), "%u MHz", (unsigned)(hz / 1000000u));
    row("SD-Takt", v);
  } else {
    snprintf(v, sizeof(v), "%u kHz", (unsigned)(hz / 1000u));
    row("SD-Takt", v);
  }

  // The one thing the user needs and the panel display itself does not show:
  // how to reach this page.
  char ip[20] = "--";
  if (WiFi.status() == WL_CONNECTED) {
    strlcpy(ip, WiFi.localIP().toString().c_str(), sizeof(ip));
  }
  row("Adresse hier", ip);
  row("Als Name", s_mdnsStarted ? "rct-panel.local" : "-");
  b += F("</table>");

  // The switched output. Its state belongs with the other read-outs, but
  // changing the function is a write and therefore behind the code - the same
  // rule the update follows. The panel display can cycle the function by
  // tapping, which is the quicker way; this form is where the threshold in
  // watts goes, and where the test sits next to the thing it tests.
  b += F("<h2>Ausgang</h2><table>");
  {
    const RelayMode m = relayMode();
    const bool hasThreshold =
        (m == RelayMode::GridDraw || m == RelayMode::PvSurplus);
    row("Funktion", kRelayModeName[(int)m]);
    if (relayTestRunning()) {
      row("Zustand", "Test l&auml;uft");
    } else if (hasThreshold) {
      snprintf(v, sizeof(v), "%s &middot; %d W jetzt", relayIsOn() ? "ein" : "aus",
               (int)lroundf(relayTriggerValue()));
      row("Zustand", v);
    } else {
      row("Zustand", relayIsOn() ? "ein" : "aus");
    }
  }
  b += F("</table>");
  b += F("<div class=\"note\">Der Ausgang schaltet ein, wenn der Wert 20 s "
         "lang &uuml;ber der Schwelle liegt, und bleibt nach dem Einschalten "
         "mindestens 60 s an. &Uuml;berschuss hei&szlig;t PV minus "
         "Hausverbrauch (mit S0). Ist der Wechselrichter zwei Minuten lang "
         "nicht erreichbar, schaltet der Ausgang aus.</div>");
  b += F("<form action=\"/aktion\" method=\"POST\">");
  b += F("<input type=\"text\" name=\"code\" inputmode=\"numeric\" "
         "maxlength=\"4\" placeholder=\"Code\">");
  b += F("<select name=\"funktion\">");
  for (int i = 0; i < kRelayModeCount; i++) {
    b += F("<option value=\"");
    b += i;
    b += F("\"");
    if ((int)relayMode() == i) {
      b += F(" selected");
    }
    b += F(">");
    b += kRelayModeName[i];
    b += F("</option>");
  }
  b += F("</select>");
  b += F("<input type=\"number\" name=\"schwelle\" min=\"0\" max=\"5000\" "
         "step=\"50\" value=\"");
  b += relayThreshold();
  b += F("\" title=\"Schwelle in Watt\">");
  b += F("<button class=\"btn\" name=\"was\" value=\"ausgang\">"
         "&Uuml;bernehmen</button> ");
  b += F("<button class=\"btn gray\" name=\"was\" value=\"test\">"
         "Test: 5 s an, 5 s aus</button>");
  b += F("</form>");

  b += F("<h2>Wartung</h2>");
  b += F("<div class=\"note\">Update, Neustart und WLAN-Einrichtung "
         "verlangen den 4-stelligen Code. Er steht auf der Panel-Seite "
         "<em>Service</em>.</div>");
  b += F("<form action=\"/aktion\" method=\"POST\">");
  b += F("<input type=\"text\" name=\"code\" inputmode=\"numeric\" "
         "maxlength=\"4\" placeholder=\"Code\">");
  b += F("<button class=\"btn gray\" name=\"was\" value=\"neustart\">"
         "Panel neu starten</button> ");
  b += F("<button class=\"btn gray\" name=\"was\" value=\"setup\">"
         "WLAN neu einrichten</button>");
  b += F("</form>");
  sendNavPage("RCT Power Panel", "/", b);
}

// ---------------------------------------------------------------------------
// Listings
// ---------------------------------------------------------------------------

void renderList(const char *title, const char *nav, const char *dir, bool csv);

// The card belongs to the worker task, so the web task never reads it. The worker
// keeps a listing per directory up to date instead, and this handler answers from
// that cache - which is why the page appears in the same pass the request arrived
// in.
//
// The obvious alternative, "ask here, answer on the next loop pass", does not work
// with this WebServer: handleClient() drops its client as soon as the handler
// returns, and WiFiClient's assignment calls stop() on the old socket. A handler
// that sends nothing therefore has no connection left to answer on, and the
// browser gets an empty reply. Hence the cache, and a 503 while it is still cold.
void askListing(const char *title, const char *nav, const char *dir, bool csv) {
  sdRequestListing(dir);
  const int n = sdListingText(dir, s_listing, sizeof(s_listing) - 1);
  if (n == kListingUnavailable) {
    sendMsg(503, "Die SD-Karte liess sich nicht lesen.");
    return;
  }
  s_listingLen = n > 0 ? (size_t)n : 0;
  renderList(title, nav, dir, csv);
}

// Both lists look the same; only the directory, the route and the wording
// differ. csvCol != nullptr marks the CSV list (download link, tail offered).
//
// Single characters go in as `b += '/'`, never as F('/'): F() is the macro for
// a *string* literal in flash and hands on its address, so F('/') passes the
// pointer value 0x2F. That compiles, strlen() then reads from address 47, and
// the panel reboots with a LoadProhibited - which is what the first build on the
// wall did, the moment someone opened /daten or /bilder.
void renderList(const char *title, const char *nav, const char *dir,
                bool csv) {
  String b;
  b.reserve(1600);
  if (s_listingLen == 0) {
    b += F("<div class=\"note\">Auf der SD-Karte liegt nichts in "
           "<code>");
    b += dir;
    b += F("</code>.</div>");
  } else {
    b += F("<ul class=\"plain\">");
    char *line = s_listing;
    while (line != nullptr && *line != '\0') {
      char *nl = strchr(line, '\n');
      if (nl != nullptr) {
        *nl = '\0';
      }
      // "name|size|epoch" - the pipe cannot occur in a FAT name. The size is the
      // field *between* the two pipes; reading the one behind the second pipe
      // gives the timestamp, which is a nine- to ten-digit number and turns a
      // 19 kB file into "1707,8 MB" (that is what the page showed).
      char *bar1 = strchr(line, '|');
      if (bar1 != nullptr) {
        *bar1 = '\0';
        char *bar2 = strchr(bar1 + 1, '|');
        uint32_t size = 0;
        if (bar2 != nullptr) {
          *bar2 = '\0';
          size = (uint32_t)strtoul(bar1 + 1, nullptr, 10);
        }
        // A name that tries to climb out of the directory is not rendered as a
        // link. The download route checks the prefix anyway.
        const bool safeName =
            strstr(line, "..") == nullptr && line[0] != '/' &&
            strchr(line, '\\') == nullptr;
        char name[48];
        escape(line, name, sizeof(name));
        char sz[20];
        humanSize(size, sz, sizeof(sz));
        b += F("<li><div class=\"g\"><b>");
        b += name;
        b += F("</b></div><span class=\"m\">");
        b += sz;
        b += F("</span> ");
        if (safeName) {
          b += F("<a class=\"btn\" href=\"");
          b += nav;
          b += '/';
          b += name;
          b += '"';
          if (csv) {
            // Default to a tail: a whole month takes seconds at the card's
            // speed, and the last hours are what gets looked at.
            b += F("?tail=65536");
          }
          b += '>';
          b += csv ? "laden" : "anzeigen";
          b += F("</a>");
        }
        b += F("</li>");
      }
      line = nl != nullptr ? nl + 1 : nullptr;
    }
    b += F("</ul>");
    if (csv) {
      b += F("<div class=\"note\">Der Knopf &bdquo;laden&ldquo; holt die "
             "letzten 64 kB (etwa zwei Tage). Mit "
             "<code>?tail=0</code> im Link kommt die ganze Datei. W&auml;hrend "
             "ein Download l&auml;uft, bedient das Panel keine weiteren "
             "Anfragen.</div>");
    }
    // The worker fills a fixed buffer; a full one ends mid-line and means there
    // are more files than fit. Say so instead of showing a silently short list.
    if (s_listingLen > 0 && s_listing[s_listingLen - 1] != '\n') {
      b += F("<div class=\"note\">Mehr Dateien auf der Karte, als hier "
             "platzieren. &Uuml;brige Dateien lassen sich direkt &uuml;ber "
             "ihren Namen aufrufen: <code>");
      b += nav;
      b += F("/Dateiname</code>.</div>");
    }
  }
  sendNavPage(title, nav, b);
}

void handleData() { askListing("Daten", "/daten", "/hist", true); }

void handleShots() { askListing("Bilder", "/bilder", "/shot", false); }

// ---------------------------------------------------------------------------
// Downloads
// ---------------------------------------------------------------------------

static void streamStep();

// Backstop for a transfer that never finishes, in ms. A month of CSV is a few
// hundred kB and a screenshot a few hundred kB more; a minute is already far
// beyond what any of them needs.
constexpr uint32_t kStreamMaxMs = 60000;

// Opening a stream: remember what was asked for, hand it to the worker, and then
// pump the whole transfer *from inside the handler*.
//
// The pump has to be here. The obvious design - start the download in the handler
// and send it on the following loop iterations - cannot work with this WebServer:
// handleClient() assigns _currentClient = WiFiClient() as soon as the handler
// returns, and WiFiClient's assignment calls stop() on the old socket. The
// download then writes to a closed socket and the browser gets an empty reply.
//
// So the handler stays until the file is out, and the yield hook hands rendering
// back between chunks: the panel keeps drawing, and the loop task's other work
// (polling the inverter, writing the next CSV row) waits for the transfer - which
// is the same trade the earlier design made, only without a dead socket.
//
// The header goes out once the worker has reported the byte count (that count is
// the Content-Length, and it is why a progress bar works).
void openStream(const char *path, uint32_t tail, StreamKind kind) {
  if (s_streamKind != StreamKind::None) {
    sendMsg(409, "Es l&auml;uft bereits ein Download.");
    return;
  }
  if (s_otaOpen) {
    sendMsg(503, "Gerade wird eine Firmware geschrieben.");
    return;
  }
  if (!sdMounted()) {
    sendMsg(503, "Keine SD-Karte.");
    return;
  }
  // Only the two directories this page serves. A name like "../../nvs" is
  // rejected here, before it reaches the card worker.
  const bool isCsv = strncmp(path, "/hist/", 6) == 0;
  const bool isShot = strncmp(path, "/shot/", 6) == 0;
  if (!isCsv && !isShot) {
    sendMsg(404, "Unbekannte Datei.");
    return;
  }
  if (isCsv && kind != StreamKind::Csv) {
    sendMsg(404, "Unbekannte Datei.");
    return;
  }
  if (isShot && kind != StreamKind::Shot) {
    sendMsg(404, "Unbekannte Datei.");
    return;
  }
  strlcpy(s_streamFile, path, sizeof(s_streamFile));
  s_streamTail = tail;
  s_streamKind = kind;
  s_streamLen = 0;
  s_streamHeaderSent = false;
  s_streamOpenedMs = millis();
  Serial.printf("Web: Download %s (tail %u)\n", s_streamFile, (unsigned)tail);
  sdRequestStream(s_streamFile, s_streamTail);

  // One chunk per pass, with the display in between. kStreamMaxMs is the backstop
  // for a card that stops answering mid-file: better a broken transfer the
  // browser can see than a request that never ends.
  const uint32_t until = millis() + kStreamMaxMs;
  while (s_streamKind != StreamKind::None) {
    streamStep();
    if (s_yieldHook) {
      s_yieldHook();
    } else {
      delay(1);
    }
    if ((int32_t)(millis() - until) >= 0) {
      Serial.printf("Web: Download %s abgebrochen (Zeitueberschreitung)\n",
                    s_streamFile);
      sdStopStream();
      s_streamKind = StreamKind::None;
      s_streamFile[0] = '\0';
      s_streamHeaderSent = false;
      s_sendLen = 0;
      s_client = WiFiClient();
      break;
    }
  }
}

// One step of the open download. Called every loop iteration, and it is the
// *only* thing called while a download runs (see webUpdate) - which is what
// keeps the socket alive for as long as the transfer needs.
//
// Two independent steps, both without waiting:
//
//   socket out  s_sendBuf[] holds what the worker delivered; whatever
//               sendContent() did not accept (a full TCP window returns short)
//               stays in the buffer for the next iteration.
//   worker in   sdTakeStreamChunk() copies the next piece, or 0 when the worker
//               has not filled its chunk yet.
//
// A write never blocks: WiFiClient::write() uses select() with a 35 ms timeout
// and MSG_DONTWAIT, four times over - at most ~140 ms in the worst case, and
// only while a browser has stopped reading.
void streamStep() {
  if (s_streamKind == StreamKind::None) {
    return;
  }
  if (!s_streamHeaderSent) {
    const uint32_t total = sdStreamTotal();
    if (total == 0) {
      if (sdStreamFailed()) {
        s_streamKind = StreamKind::None;
        s_streamFile[0] = '\0';
        sendMsg(404, "Die Datei gibt es nicht (oder der Stick ist weg).");
        return;
      }
      if ((int32_t)(millis() - s_streamOpenedMs) > 6000) {
        // The worker did not come back with a size. Do not hold the browser.
        sdStopStream();
        s_streamKind = StreamKind::None;
        s_streamFile[0] = '\0';
        sendMsg(500, "Die Datei liess sich nicht vom Stick lesen.");
        return;
      }
      diagPhase("web.openwait");
      return;
    }
    // Header, then the body in pieces. The size is the file's own, not what we
    // managed to read: a download that is cut short is then visible as a failed
    // transfer in the browser instead of a silently truncated file.
    const bool isCsv = s_streamKind == StreamKind::Csv;
    s_server.setContentLength(total);
    s_server.sendHeader("Cache-Control", "no-store");
    if (isCsv) {
      s_server.sendHeader("Content-Disposition",
                          String("attachment; filename=\"") +
                              strrchr(s_streamFile, '/') + "\"");
    }
    // Empty body: this writes the status line and the headers and nothing else.
    s_server.send(200, isCsv ? "text/csv; charset=utf-8" : "image/bmp", "");
    // Our own handle on the same socket, for the body (see s_client).
    s_client = s_server.client();
    s_streamLen = total;
    s_streamHeaderSent = true;
    s_sendLen = 0;
    s_sendPos = 0;
    s_lastWriteMs = millis();
    diagPhase("web.stream");
  }

  // Out: whatever is still in the socket buffer.
  if (s_sendLen > 0) {
    const size_t w = s_client.write(s_sendBuf + s_sendPos, s_sendLen - s_sendPos);
    s_sendPos += w;
    if (w > 0) {
      s_lastWriteMs = millis();
    } else if ((int32_t)(millis() - s_lastWriteMs) > 4000) {
      // Nothing accepted for four seconds: the browser is gone (or the network
      // is). Give up instead of keeping the card busy for minutes.
      Serial.printf("Web: Download %s abgebrochen (Browser weg)\n",
                    s_streamFile);
      sdStopStream();
      s_streamKind = StreamKind::None;
      s_streamFile[0] = '\0';
      s_streamHeaderSent = false;
      s_sendLen = 0;
      s_client = WiFiClient();
      return;
    }
    if (s_sendPos < s_sendLen) {
      diagBeat();
      return; // window full, try again next iteration
    }
    s_sendLen = 0;
    s_sendPos = 0;
  }

  // In: the next piece from the card worker. 2048 bytes is well inside the
  // worker's 16 kB buffer, so this never waits for more than one worker pass.
  const int n = sdTakeStreamChunk(s_sendBuf, sizeof(s_sendBuf));
  if (n == 0) {
    diagBeat(); // worker still filling - expected, not a hang
    return;
  }
  if (n < 0) {
    // The card side is done and the socket buffer is empty: the file is out.
    Serial.printf("Web: Download %s fertig (%u bytes, %lu ms)\n", s_streamFile,
                  (unsigned)s_streamLen,
                  (unsigned long)(millis() - s_streamOpenedMs));
    s_streamKind = StreamKind::None;
    s_streamFile[0] = '\0';
    s_streamHeaderSent = false;
    s_streamLen = 0;
    s_client = WiFiClient(); // drop our reference; the server keeps the socket
    return;
  }
  s_sendLen = (size_t)n; // written out on the next iterations
  s_sendPos = 0;
}

void handleCsvFile() {
  // "/daten/<name>" - the name comes in as a path argument and is checked
  // against the /hist prefix in openStream() before the worker sees it.
  const String name = s_server.pathArg(0);
  char path[40];
  snprintf(path, sizeof(path), "/hist/%s", name.c_str());
  uint32_t tail = 0;
  if (s_server.hasArg("tail")) {
    const long t = strtol(s_server.arg("tail").c_str(), nullptr, 10);
    tail = t <= 0 ? 0u : (uint32_t)(t > (long)kMaxTail ? (long)kMaxTail : t);
  }
  openStream(path, tail, StreamKind::Csv);
}

void handleShotFile() {
  const String name = s_server.pathArg(0);
  char path[40];
  snprintf(path, sizeof(path), "/shot/%s", name.c_str());
  openStream(path, 0, StreamKind::Shot);
}

// ---------------------------------------------------------------------------
// /update - firmware upload
//
// The Updater erases lazily: begin() only picks the target slot and mallocs a
// 4 kB buffer; sectors and 64 kB blocks are erased per written block inside
// _writeBuffer. So the longest stall is a single block erase in tens of ms, not
// a multi-second partition erase - the panel stays on screen throughout the
// upload. A failed update cannot brick anything: the image is magic-checked
// before it is booted, and the other 7 MB slot stays intact, so the worst case
// is a reboot into the old firmware.
// ---------------------------------------------------------------------------

void handleUpdatePage() {
  String b;
  b.reserve(1100);
  b += F("<h2>Firmware aktualisieren</h2>");
  b += F("<div class=\"note\">Datei <code>firmware.bin</code> aus dem "
         "Build-Ordner w&auml;hlen. Das Panel bleibt w&auml;hrend des "
         "Schreibens bedienbar und startet danach neu. L&auml;uft ein Update "
         "schief, startet das Panel mit der bisherigen Firmware weiter.</div>");
  b += F("<form action=\"/update\" method=\"POST\" "
         "enctype=\"multipart/form-data\">");
  // The code field comes *before* the file on purpose: the WebServer parses the
  // parts in order, so at UPLOAD_FILE_START it is already known - and a wrong
  // code is refused before a single byte is written to flash.
  b += F("<input type=\"text\" name=\"code\" inputmode=\"numeric\" "
         "maxlength=\"4\" placeholder=\"Code\">");
  b += F("<input type=\"file\" name=\"fw\" accept=\".bin\">");
  b += F("<button class=\"btn\">Firmware schreiben</button>");
  b += F("</form>");
  sendNavPage("Update", "/update", b);
}

// The code check, in one place: used before the write starts and again after it
// (a hand-made request may put the file part first, in which case the field is
// not known yet when the upload begins).
//
// The code is only ever *compared*, never written into a page, so it is checked
// as four digits instead of being HTML-escaped into a buffer. That escape used to
// be the whole bug: its loop keeps 8 bytes in reserve for the worst-case entity,
// so a buffer of 8 - which is all a 4-digit code needs - came out empty, and
// every code compared against "" and was refused.
bool codeOk() {
  if (!s_server.hasArg("code")) {
    return false;
  }
  // arg() returns a String *by value*, so it has to be kept in a named variable:
  // s_server.arg("code").c_str() on its own line hands out the buffer of a
  // temporary that is already destroyed, and the comparison then runs against
  // whatever the freed heap block holds by then (an empty string, in practice).
  const String got = s_server.arg("code");
  const char *in = got.c_str();
  int n = 0;
  while (in[n] != '\0') {
    if (n >= 4 || in[n] < '0' || in[n] > '9') {
      return false; // not a 4-digit number: not our code
    }
    n++;
  }
  if (n != 4 || strncmp(in, s_code, 4) != 0) {
    // The number of digits received is worth having: it separates "the code was
    // wrong" from "the form sent something else entirely". The code itself stays
    // out of the log.
    Serial.printf("Web: Code abgelehnt, %d Ziffern erhalten\n", n);
    return false;
  }
  return true;
}

// Runs while the request body arrives, chunk by chunk.
void handleUpdateUpload() {
  HTTPUpload &up = s_server.upload();
  switch (up.status) {
  case UPLOAD_FILE_START: {
    Serial.printf("Web: Update angefordert, %s\n", up.name.c_str());
    // Reject before writing: a wrong file would otherwise cost the whole upload
    // and only then fail.
    if (!codeOk()) {
      Serial.println(F("Web: Update abgelehnt (kein passender Code)"));
      s_otaOk = false;
      return;
    }
    if (up.totalSize == 0 || up.totalSize > kMaxFirmware) {
      Serial.println(F("Web: Update abgelehnt (Groesse passt nicht)"));
      s_otaOk = false;
      return;
    }
    if (up.name.indexOf(".bin") < 0) {
      Serial.println(F("Web: Update abgelehnt (keine .bin-Datei)"));
      s_otaOk = false;
      return;
    }
    s_otaOk = Update.begin(up.totalSize, U_FLASH);
    s_otaOpen = s_otaOk;
    if (!s_otaOk) {
      Serial.printf("Web: Update.begin(): %s\n", Update.errorString());
    }
    s_otaBytes = 0;
    s_otaTotal = up.totalSize;
    s_otaStartMs = millis();
    break;
  }
  case UPLOAD_FILE_WRITE:
    if (s_otaOk) {
      s_otaOk = Update.write(up.buf, up.currentSize) == up.currentSize;
      if (!s_otaOk) {
        Serial.printf("Web: Update.write(): %s\n", Update.errorString());
      }
      s_otaBytes += up.currentSize;
    }
    break;
  case UPLOAD_FILE_ABORTED:
    if (s_otaOpen) {
      Update.abort();
    }
    s_otaOk = false;
    s_otaOpen = false;
    Serial.println(F("Web: Update abgebrochen"));
    break;
  default:
    break;
  }
}

// Runs after the body, with the form fields available.
void handleUpdateDone() {
  // Whatever happens below, the upload slot is closed again - a flag left set
  // would keep every later download from being served.
  const bool ok = s_otaOk;
  const bool code = codeOk();
  s_otaOpen = false;
  if (!code) {
    Serial.println(F("Web: Update ohne passenden Code abgelehnt"));
    sendMsg(403, "Der Code stimmt nicht. Er steht auf der Panel-Seite Service.");
    return;
  }
  if (!ok) {
    sendMsg(400, "Das Update wurde nicht ausgef&uuml;hrt. Bitte erneut "
                 "versuchen.");
    return;
  }
  const uint32_t took = millis() - s_otaStartMs;
  if (!Update.end(true)) {
    Serial.printf("Web: Update.end(): %s\n", Update.errorString());
    s_otaOpen = false;
    sendMsg(500, "Das Update liess sich nicht abschliessen. Die alte "
                 "Firmware l&auml;uft weiter.");
    return;
  }
  s_otaOpen = false;
  Serial.printf("Web: Update fertig, %u von %u bytes in %lu ms, Neustart\n",
                (unsigned)s_otaBytes, (unsigned)s_otaTotal,
                (unsigned long)took);
  String b;
  b.reserve(400);
  b += F("<div class=\"note ok\">Firmware geschrieben. Das Panel startet "
         "jetzt neu.</div>");
  sendNavPage("Update", "/update", b);
  delay(800); // let the reply reach the browser before the radio goes down
  webStop();
  ESP.restart();
}

// ---------------------------------------------------------------------------
// /aktion - restart and re-provisioning, both behind the code
// ---------------------------------------------------------------------------

void handleAction() {
  if (!codeOk()) {
    Serial.println(F("Web: Aktion abgelehnt (kein passender Code)"));
    sendMsg(403, "Der Code stimmt nicht. Er steht auf der Panel-Seite Service.");
    return;
  }
  const String was = s_server.arg("was");
  if (was == "neustart") {
    Serial.println(F("Web: Neustart angefordert"));
    sendMsg(200, "Das Panel startet neu.");
    delay(500);
    webStop();
    ESP.restart();
    return;
  }
  if (was == "setup") {
    Serial.println(F("Web: WLAN-Einrichtung angefordert"));
    sendMsg(200, "Das Panel startet das WLAN-Setup (Zugang: RCT-Panel).");
    delay(500);
    webStop(); // frees port 80 and the radio for the portal
    restartProvisioning();
    return;
  }
  if (was == "test") {
    // Drives the output, so it belongs behind the code like everything else that
    // touches the panel. The answer is sent first: the test runs 20 s and the
    // panel keeps drawing, but the browser should not wait for it.
    const bool started = relayStartTest();
    Serial.printf("Web: Ausgang-Test %s\n", started ? "gestartet" : "laeuft schon");
    sendMsg(started ? 200 : 409,
            started ? "Test laeuft: 5 s ein, 5 s aus, zweimal."
                    : "Ein Test laeuft bereits.");
    return;
  }
  if (was == "ausgang") {
    // Both fields belong to the same form; the select decides the function, the
    // number its threshold. The threshold is stored even for a function that has
    // none, so switching back to a threshold function keeps the value.
    const String f = s_server.arg("funktion");
    const int m = f.toInt();
    const int w = s_server.arg("schwelle").toInt();
    if (m < 0 || m >= kRelayModeCount) {
      sendMsg(400, "Unbekannte Funktion.");
      return;
    }
    relaySetThreshold(w);
    relaySetMode((RelayMode)m);
    Serial.printf("Web: Ausgang auf '%s', Schwelle %d W\n",
                  kRelayModeName[m], relayThreshold());
    sendMsg(200, "&Uuml;bernommen. Das Panel zeigt die neue Funktion auf der "
                 "Seite Service.");
    return;
  }
  sendMsg(400, "Unbekannte Aktion.");
}

void handleNotFound() { sendMsg(404, "Diese Seite gibt es nicht."); }

// A fresh code per boot: a code that survives a restart could be replayed from
// an old terminal log or a sticky note.
void makeCode() {
  uint32_t r = esp_random();
  for (int i = 0; i < 4; i++) {
    s_code[i] = (char)('0' + (int)(r % 10));
    r /= 10;
  }
  s_code[4] = '\0';
  Serial.printf("Web: Wartungscode %s\n", s_code);
}

} // namespace

void webSetYieldHook(void (*fn)()) { s_yieldHook = fn; }

void webStart() {
  if (s_serverStarted || provisioningApActive()) {
    return; // never both on port 80
  }
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  makeCode();
  // Plain URIs match exactly (Uri::canHandle), so the file routes need the
  // brace form: "/daten/{}" takes the name as path argument 0.
  s_server.on(Uri("/"), HTTP_GET, handleRoot);
  s_server.on(Uri("/daten"), HTTP_GET, handleData);
  s_server.on(Uri("/bilder"), HTTP_GET, handleShots);
  s_server.on(UriBraces("/daten/{}"), HTTP_GET, handleCsvFile);
  s_server.on(UriBraces("/bilder/{}"), HTTP_GET, handleShotFile);
  s_server.on(Uri("/update"), HTTP_GET, handleUpdatePage);
  s_server.on(Uri("/update"), HTTP_POST, handleUpdateDone, handleUpdateUpload);
  s_server.on(Uri("/aktion"), HTTP_POST, handleAction);
  s_server.onNotFound(handleNotFound);
  s_server.begin();
  s_serverStarted = true;

  // mDNS is a convenience: the address also appears in the overview and on the
  // panel. A network that blocks mDNS loses nothing but the name.
  if (MDNS.begin("rct-panel") == 0) {
    Serial.println(F("Web: mDNS nicht verfuegbar, Adresse ueber IP nutzen"));
  } else {
    MDNS.addService("http", "tcp", 80);
    s_mdnsStarted = true;
  }
  Serial.printf("Web: http://%s/ (mDNS: %s)\n",
                WiFi.localIP().toString().c_str(),
                s_mdnsStarted ? "rct-panel.local" : "aus");
}

void webStop() {
  if (s_streamKind != StreamKind::None) {
    sdStopStream();
    s_streamKind = StreamKind::None;
    s_streamHeaderSent = false;
    s_sendLen = 0;
    s_client = WiFiClient();
  }
  if (s_serverStarted) {
    s_server.stop();
    s_server.close();
    s_serverStarted = false;
    Serial.println(F("Web: Server gestoppt"));
  }
  if (s_mdnsStarted) {
    MDNS.end();
    s_mdnsStarted = false;
  }
}

bool webRunning() { return s_serverStarted; }

void webUpdate() {
  if (!s_serverStarted) {
    return;
  }
  diagPhase("web.handle");
  // Downloads are pumped inside their own handler (see openStream), so there is
  // nothing to keep alive here: while a file is on the wire this task is inside
  // that handler. One request at a time, which is what the page says out loud.
  s_server.handleClient();
  diagBeat();
}

// Pump the download of a stream that is already running, but not the server
// itself. Called from the provisioning path, where port 80 belongs to the
// WiFiManager portal: an in-flight download is stopped before the portal takes
// the radio, and this is the belt to that braces - it closes an open stream
// even if webStop() was not called.
void webAbortStreams() {
  if (s_streamKind != StreamKind::None) {
    sdStopStream();
    s_streamKind = StreamKind::None;
    s_streamHeaderSent = false;
    s_sendLen = 0;
    s_client = WiFiClient();
    s_streamFile[0] = '\0';
  }
}

const char *webCode(const char *newCode) {
  if (s_code[0] == '\0') {
    makeCode();
  }
  if (newCode != nullptr) {
    for (int i = 0; i < 4; i++) {
      const char c = newCode[i];
      s_code[i] = (c >= '0' && c <= '9') ? c : '0';
    }
    s_code[4] = '\0';
    Serial.printf("Web: Wartungscode jetzt %s\n", s_code);
  }
  return s_code;
}

const char *webNewCode() {
  makeCode();
  return s_code;
}