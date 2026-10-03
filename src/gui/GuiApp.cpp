// LVGL panel UI.
//
// Page 1 mirrors the RCT Portal "Energiefluss" pane (see examples/):
// a central household node with PV / batterie / netz nodes around it,
// connector lines that light up red in the direction of the actual energy
// flow, live kW values under each node, and a status table below. The RCT
// logo is intentionally omitted (the center shows a house icon instead).
// Direction is not spelled out in words at the netz node: the arrow on the
// connector carries it, the table below repeats it as a value.
//
// Island mode (grid outage, prim_sm.island_flag) puts a red warning triangle on
// the severed grid connector between haus and netz, and the status table then
// reads "Unabhängig" for both Netz and Verbrauch - the house runs on PV and
// battery alone. The flag arrives with the 10 s device group, so the triangle
// stays hidden until the device has answered once.
//
// Data note: PV = dc_conv.dc_conv_struct[0|1].p_dc_lp plus the S0 meter
// (io_board.s0_external_power, merged as "EXT." in the portal), household =
// g_sync.p_ac_load[0..2], battery = battery.soc/current/voltage and
// g_sync.p_acc_lp (positive = charging). Nodes stay "-" until the device
// answers the respective OIDs.
//
// Pages: 1 Energiefluss, 2 Energie (accumulated energies per period as bars,
// selectable Tag/Monat/Jahr/Gesamt), 3 Heute (day summary), 4 Verlauf (24 h
// power graph; one sample every 5 minutes, PV A+B and the S0 meter as separate
// series), 5 Info (host / link / meter list), 6 Gerät (device info polled
// every 10 s), 7 Service.
//
// Layout:
//   +-----------------------------+  <- status bar (title / link badge)
//   |       PV     [haus]   NETZ   |
//   |         \      |      /      |  <- energy flow diagram (480x280)
//   |           \  [bat]  /        |
//   |  Erzeugung/Verauch/N/B table |  <- status table (2x2)
//   +------+------+------+--------+  <- nav bar (left | home | right)
//
// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <Preferences.h>
#include <WiFi.h>

#include "GuiApp.h"

#include "../Charts.h"
#include "../DataStatus.h"
#include "../NumFmt.h"
#include "../config/Configuration.h"
#include "../Diag.h"
#include "../display/Backlight.h"
#include "../display/Display.h"
#include "../display/Touch.h"
#include "../i18n/Lang.h"
#include "../output/Relay.h"
#include "../rct/RctTypes.h"
#include "../storage/sdlog.h"
#include "../web/WebServer.h"
#include "fonts/lv_font_mdi_icons_28.h"
// Montserrat with German umlauts (Latin-1 supplement), falling back to the
// LVGL built-ins for the LV_SYMBOL_* glyphs. See OFL-Montserrat.txt.
#include "fonts/lv_font_montserrat_14_uml.h"
#include "fonts/lv_font_montserrat_16_uml.h"
#include "fonts/lv_font_montserrat_20_uml.h"
#include "fonts/lv_font_montserrat_28_uml.h"

#define NAV_H 72
#define STATUS_H 44
#define CONTENT_H (480 - STATUS_H - NAV_H)

// Y of the page heading inside a page root. Every page uses this one value
// (placed centrally at page creation, see guiInit), so the heading sits at the
// same spot on all seven pages.
#define HEAD_Y 8

// First row y on the "Info" / "Gerät" list pages, below the heading.
#define ROW_Y0 40
// Row pitch there. 14 rows must fit in CONTENT_H: 40 + 13*22 + 20 = 346 < 364
// (16 px font has a 20 px line box, so 22 leaves 2 px of air per row).
#define ROW_PITCH 22

// ---------------------------------------------------------------------------
// Palette (from the RCT Portal Energiefluss: white nodes, red active flows)
// ---------------------------------------------------------------------------
// The page background and the text on it are the two colours that follow the
// theme; everything else is one fixed value. Which two depends on s_hell, and
// that is the whole mechanism of the switch - see uiBg() and themeWechseln().
static const lv_color_t COL_BG_DUNKEL = lv_color_hex(0x101418);
static const lv_color_t COL_BG_HELL = lv_color_hex(0xFFFFFF);
static const lv_color_t COL_TEXT_DUNKEL = lv_color_hex(0xE8ECF1);
// Not pure black: the dark theme's background serves well as the light theme's
// text, and it is easier on the eyes at night without costing any contrast.
static const lv_color_t COL_TEXT_HELL = lv_color_hex(0x101418);
static bool s_hell = false; // from NVS, read before the first colour is used
static const lv_color_t COL_CARD = lv_color_hex(0x1C222A);
static const lv_color_t COL_BAR = lv_color_hex(0x3A4550); // top / bottom bars (clearly brighter than cards)
static const lv_color_t COL_ACCENT = lv_color_hex(0x2E93E5);
static const lv_color_t COL_MUTED = lv_color_hex(0x8A94A0);
// Green for "on" and "no faults". Ready to follow the theme like the two
// colours above - the walk compares and swaps it like them - but deliberately
// the same value in both modes for now: #3EC97A on white is the one weak spot
// of the light theme (about 1.9:1 contrast), and a darker green for it is a
// decision to make with the colours side by side, not one to guess here.
static const lv_color_t COL_OK = lv_color_hex(0x3EC97A);
static const lv_color_t COL_ERR = lv_color_hex(0xE5484D);
static const lv_color_t COL_WARN = lv_color_hex(0xEBD300); // waiting, not broken
static const lv_color_t COL_BORDER = lv_color_hex(0x2A3038); // stat card ring

// The page background, and the text that sits directly on it. Everything with a
// background of its own - cards, the code row, the mode row, the buttons, the
// status and navigation bars - keeps its colour in both modes.
static inline lv_color_t uiBg() {
  return s_hell ? COL_BG_HELL : COL_BG_DUNKEL;
}
static inline lv_color_t uiText() {
  return s_hell ? COL_TEXT_HELL : COL_TEXT_DUNKEL;
}
// Green, ready to differ between the themes. One function, so the switch has
// nothing to know about it later: the day the light mode gets its own green, it
// is a second constant and a ternary here, and nothing else in the file moves.
static inline lv_color_t uiOk() { return COL_OK; }

// ---------------------------------------------------------------------------
// The switch: what it writes, and the walk that writes it
// ---------------------------------------------------------------------------
// The pages are built once at boot, so a change at runtime has to go over the
// objects that already exist. What is written and what is compared against,
// kept as pairs - the walk then needs no knowledge of which colour is which.
static lv_color_t s_altBg = COL_BG_DUNKEL, s_neuBg = COL_BG_DUNKEL;
static lv_color_t s_altText = COL_TEXT_DUNKEL, s_neuText = COL_TEXT_DUNKEL;
static lv_color_t s_altOk = COL_OK, s_neuOk = COL_OK;

// One object and everything below it.
//
// `aufBg` says that nothing between here and the page has a background of its
// own - which is what "this text sits on the background" means. It falls out of
// the same colour test as the swap, because both are the same question: a card,
// a row and a bar are exactly the objects whose background is not the page
// background.
//
// Two details of LVGL make that test necessary in this form. The getters return
// the *resolved* value, inheritance included, so a label inside a card reports
// the card's colour and is correctly treated as being on it. And an object
// without a background of its own reports the default colour (white, which in
// the light theme even collides with the new background) - so opacity has to
// be asked as well, otherwise the first plain label on a page would end the
// chain and leave everything below it untouched.
static void themeDurchlaufen(lv_obj_t *o, bool aufBg) {
  const bool undurchsichtig =
      lv_obj_get_style_bg_opa(o, LV_PART_MAIN) != LV_OPA_TRANSP;
  const bool eigeneFlaeche =
      undurchsichtig &&
      !lv_color_eq(lv_obj_get_style_bg_color(o, LV_PART_MAIN), s_altBg);
  if (aufBg && !eigeneFlaeche && undurchsichtig) {
    lv_obj_set_style_bg_color(o, s_neuBg, LV_PART_MAIN);
  } else if (eigeneFlaeche) {
    aufBg = false; // a panel of its own: nothing below it is on the background
  }
  const lv_color_t txt = lv_obj_get_style_text_color(o, LV_PART_MAIN);
  if (aufBg && lv_color_eq(txt, s_altText)) {
    lv_obj_set_style_text_color(o, s_neuText, LV_PART_MAIN);
  } else if (lv_color_eq(txt, s_altOk)) {
    // The green follows the theme wherever it stands - on the background and on
    // the cards - so it is swapped by colour and not by place.
    lv_obj_set_style_text_color(o, s_neuOk, LV_PART_MAIN);
  }
  const uint32_t n = lv_obj_get_child_count(o);
  for (uint32_t i = 0; i < n; i++) {
    themeDurchlaufen(lv_obj_get_child(o, i), aufBg);
  }
}

// The theme is the only thing of ours in this namespace, which keeps the question
// "what does the panel remember" answerable by looking at two places.
static Preferences prefs;

static void themeLaden() {
  prefs.begin("gui", false);
  s_hell = prefs.getUChar("theme", 0) != 0;
  prefs.end();
}

static void themeSpeichern() {
  prefs.begin("gui", true);
  prefs.putUChar("theme", s_hell ? 1 : 0);
  prefs.end();
}

// Energiefluss palette (reference values)
static const lv_color_t FLOW_RED = lv_color_hex(0xCA0C0F);   // active flow / value
static const lv_color_t FLOW_GRAY = lv_color_hex(0x555658);  // icon inside nodes
static const lv_color_t FLOW_BORDER = lv_color_hex(0x6E6F72); // node ring
static const lv_color_t FLOW_LINE = lv_color_hex(0xCBCBCD);  // idle connector
static const lv_color_t FLOW_WHITE = lv_color_hex(0xFFFFFF); // node fill

// The two flow-diagram icons, from Material Design Icons (Community),
// Apache-2.0, https://github.com/pictogrammers/material-design-icons - see
// NOTICE. They replace the two glyphs lifted from the portal's own embedded
// icon font, whose origin could not be established: U+E039 and U+E044 do not
// match today's Google Material Icons at those code points, so nothing could be
// attributed. MDI carries the same Apache-2.0 that ESP-IDF and the ported RCT
// client already bring, so no new licence enters the tree.
//
// Two glyphs baked into lv_font_mdi_icons_28 (see src/gui/fonts/). The built-in
// Montserrat has neither a pylon nor a solar panel, and its symbols stop at the
// LV_SYMBOL_* glyphs.
//
// Written as UTF-8 bytes, not as \uXXXX: a universal character name in a
// narrow string literal consumed only four hex digits here, so "\uF0D3E"
// became U+F0D3 followed by a literal 'E' - the panel then drew a
// placeholder box and the letter. LVGL declares its LV_SYMBOL_* glyphs the
// same way for the same reason.
static const char kFlowGridIcon[] =
    "\xF3\xB0\xB4\xBE"; // transmission-tower U+F0D3E, Netz node
static const char kFlowSolarIcon[] =
    "\xF3\xB0\xA9\xB2"; // solar-power U+F0A72, PV node

// ---------------------------------------------------------------------------
// Page model
// ---------------------------------------------------------------------------
// Page order. PAGE_GRAPH and PAGE_INFO sit in the order they are read: the
// 24 h history first, the verbose Info list behind it.
enum PageId {
  PAGE_OVERVIEW = 0,
  PAGE_ENERGY,   // accumulated energies per period (bars, portal colors)
  PAGE_HEUTE,    // current-day summary cards
  PAGE_GRAPH,    // 24 h power history
  PAGE_INFO,     // host / link / meter detail list
  PAGE_DEVICE,   // device info (Gerät)
  PAGE_SERVICE,  // battery status, faults, provisioning
  PAGE_COUNT,
};

static const int MAX_PAGE_LABELS = 20;

// Overview page label indices.
enum OvLabel {
  OV_GRID_VAL = 0, // netz kW value (red)
  OV_HOUSE_VAL,    // haus kW value
  OV_PV_VAL,       // pv kW value
  OV_BAT_VAL,      // batterie kW value
  OV_BAT_SOC,      // SOC % inside the battery node
  OV_GRID_ARROW,   // direction arrow on the grid connector
  OV_PV_ARROW,     // direction arrow on the PV connector
  OV_BAT_ARROW,    // direction arrow on the battery connector
  OV_ISLAND,       // warning triangle on the grid connector (island mode)
  OV_T_ERZ,        // status table: Erzeugung
  OV_T_VERB,       // status table: Verbrauch
  OV_T_NETZ,       // status table: Netz
  OV_T_BAT,        // status table: Batterie
  OV_LABEL_COUNT,
};

// Info page label indices.
enum InfoLabel {
  INF_NAME = 0, // device name (rows 1+2: identity first, then connection)
  INF_SW,       // control software version
  INF_HOST,
  INF_PORT,
  INF_LINK,
  INF_LAST,
  INF_UPTIME,
  INF_L1,   // per-phase grid power
  INF_L2,
  INF_L3,
  INF_PV,   // PV total (A + B + S0)
  INF_CORE, // core temperature
  INF_HTEMP, // heat sink temperature
  INF_FREQ, // grid frequency L1
  INF_LABEL_COUNT,
};

// Heute (day summary) page label indices. Mirrors the portal "Übersicht"
// boxes (Erzeugt / Eigenverbrauch / Eingespeist) and "Energiestatistiken"
// (Autarkie / Eigenverbrauch). Value and caption share alternating slots.
enum EnLabel {
  EN_GEN_VAL = 0, EN_GEN_LBL, // Erzeugt (kWh)
  EN_SELF_VAL, EN_SELF_LBL,   // Eigenverbrauch from PV (kWh)
  EN_FEED_VAL, EN_FEED_LBL,   // Eingespeist (kWh)
  EN_VERB_VAL, EN_VERB_LBL,   // Verbrauch / household (kWh)
  EN_BEZU_VAL, EN_BEZU_LBL,   // Bezug / grid draw (kWh)
  EN_AUT_VAL, EN_AUT_LBL,     // Autarkie (%)
  EN_EVB_VAL, EN_EVB_LBL,     // Eigenverbrauch (%)
  EN_LABEL_COUNT,
};

// "Energie" page. Five bar rows in the portal's series colors. Each label sits
// above its own bar, left aligned, with the value right aligned on the same
// line - that makes the colored bar itself the legend entry, so no separate
// color chips and no legend block are needed. Only the value labels are
// refreshed by the 1 Hz timer, the bars are resized there too.
enum EbLabel {
  EB_VAL_PV = 0,  // PV Erzeugung
  EB_VAL_SELF,    // Eigenverbrauch (PV − Netzeinspeisung)
  EB_VAL_FEED,    // Netzeinspeisung
  EB_VAL_GRID,    // Netzbezug
  EB_VAL_LOAD,    // Verbrauch
  EB_LABEL_COUNT,
};

static const int ENERGY_ROWS = 5;
static const int ENERGY_PERIODS = 4; // Tag | Monat | Jahr | Gesamt
// Portal palette (examples/RCT Portal _ ...-page2.html).
static const uint32_t kEnergyColor[ENERGY_ROWS] = {0xEBD300, 0x12A40A, 0xF48756,
                                                   0xCA0C0F, 0x3CBCD4};
// The five energy rows and the four periods, as IDs: the wording comes from
// src/i18n, so the names here cannot drift apart from the web page.
static const LangId kEnergyId[ENERGY_ROWS] = {
    T_D_EN_PV, T_D_EN_SELFUSE, T_D_EN_EXPORT, T_D_EN_IMPORT, T_D_EN_LOAD};
static const LangId kPeriodId[ENERGY_PERIODS] = {
    T_D_PER_DAY, T_D_PER_MONTH, T_D_PER_YEAR, T_D_PER_TOTAL};
static lv_obj_t *s_ebarFill[ENERGY_ROWS] = {nullptr}; // bar fills, resized live
static lv_obj_t *s_ebarBtn[ENERGY_PERIODS] = {nullptr};
static int s_energyPeriod = 0; // selected period, 0 = Tag

// 24 h history (graph) page label indices. The page heading is not one of
// them: it is created centrally for every page (see guiInit).
enum GhLabel {
  GH_GAPS = 0, // summary of missing samples in the 24 h window
  GH_LABEL_COUNT,
};

// Akku (battery) page label indices: everything battery-specific moved here
// from the old "Gerät" page (name/software/temperatures went the other way to
// Info). Renamed "Gerät" -> "Akku" as requested.
enum DevLabel {
  DEV_SOC = 0,   // battery SOC (from Info)
  DEV_BAT,       // battery power / current / voltage (from Info)
  DEV_BTEMP,     // battery temperature
  DEV_CALIB,     // next battery calibration
  DEV_CYCLES,    // charge/discharge cycles
  DEV_SOH,       // battery state of health
  DEV_ISLAND,    // island (grid-separated) mode
  DEV_LABEL_COUNT,
};

// Service page label indices.
enum SvLabel {
  // Decoded battery status with the raw register value in brackets behind it
  // ("Unterspannung (0x00000200)"): the hex belongs to the state, so it reads
  // as one line instead of a second one that has to be matched up.
  SV_BAT_STATUS = 0,
  SV_FLT_LIST,   // decoded faults, one per line
  SV_SD,         // SD history log status
  SV_SHOT,       // screenshot state ("geschrieben" / "wird geschrieben")
  SV_WEB,        // panel's own address (web interface, normal operation)
  SV_CODE,       // 4-digit code for the web interface's write actions
  SV_RELAY,      // function the switched output follows (tappable)
  SV_RELAY_ST,   // what it is doing now, with the value it watches
  SV_LABEL_COUNT,
};

// --- 24 h power history ----------------------------------------------------
// One sample every 5 minutes while the device is running; the ring buffer
// holds 288 samples (= exactly 24 h). Values are W, mirrored as float for the
// Y autoscale and fed to the LVGL chart as int32. S0 is kept as its own series
// (not merged into the PV A+B total), mirroring the portal's separate "+EXT."
// node. Battery/grid may be negative (discharge / feed-in). The last series,
// SOC, is a percent value on its own chart axis (0..100), scaled to fill the
// whole chart height.
static const int HIST_POINTS = 288;              // 288 * 5 min = 24 h
static const int HIST_SERIES = 6;                // grid, house(+ext), PV, EXT, battery, SOC
static const uint32_t HIST_INTERVAL_MS = 300000; // 5 min
static const uint32_t HIST_SEED_WINDOW_MS = 60000; // boot grace without a card
// The colours and the number of series come from src/Charts.h: the web
// interface draws the same six lines, and two written-down colour lists would
// mean the battery is orange on the panel and yellow on the phone.
static const uint32_t kHistColor[HIST_SERIES] = {kChartColor[0], kChartColor[1],
                                                 kChartColor[2], kChartColor[3],
                                                 kChartColor[4], kChartColor[5]};
static const LangId kHistId[HIST_SERIES] = {
    T_D_SER_GRID, T_D_SER_CONSUMPTION, T_D_SER_PV,
    T_D_SER_EXT,  T_D_SER_BATTERY,      T_D_SER_SOC};
static const int LEGEND_GAP = 24; // space between two legend entries
// Chart frame on the Verlauf page. The card starts near the left edge because
// the min/0/max scale markers sit inside it (see applyScaleMarkers) instead of
// in a gutter beside it: the width that went into those labels goes into the
// plot area instead.
static const int kHistChartX = 10, kHistChartY = 52;
static const int kHistChartW = 458, kHistChartH = 280;
static const int kHistChartPad = 10;
static lv_obj_t *s_chart = nullptr;
static lv_chart_series_t *s_chartSer[HIST_SERIES] = {nullptr};
// Scale markers of the primary (power) axis, drawn over the left end of the
// plot area, refreshed by updateChartRange(). The SOC series gets no markers:
// its own fixed 0..100 axis spans the whole chart height by construction.
static lv_obj_t *s_scaleMax = nullptr, *s_scaleZero = nullptr,
                *s_scaleMin = nullptr;
static float s_hist[HIST_POINTS * HIST_SERIES] = {0.0f}; // packed [pt][ser]
static uint32_t s_histTs[HIST_POINTS] = {0};   // unix s per slot, 0 = unknown
static uint8_t s_histOk[HIST_POINTS] = {0};    // 1 = measured, 0 = gap marker
static int s_histCount = 0; // slots stored so far (measured + gap markers)
static int s_histNext = 0;  // next write slot (ring cursor)
static uint32_t s_lastHistMs = 0; // time of the last stored sample
static uint32_t s_lastHistTs = 0; // unix s of the last stored sample
static bool s_histSeeded = false; // history seeded once from the SD log
static uint32_t s_histSeedStartMs = 0; // boot time used for the no-card grace
static int s_gapCount = 0;          // gaps in the current 24 h window
static uint32_t s_gapSeconds = 0;   // total time missing from them

// --- Access for the web interface -------------------------------------------
// The history ring lives here, so the web interface asks for its points instead
// of keeping a second copy of the numbers. It reads point by point: a copy for
// one request would be 288 * 6 floats plus timestamps, about 8 kB of RAM, and
// the answer is thrown away as soon as it has been sent.

int guiHistoryPoints() { return s_histCount; }

bool guiHistoryPoint(int idx, uint32_t *ts, float *w) {
  if (idx < 0 || idx >= s_histCount) {
    return false;
  }
  // The ring is a ring: the oldest point is not at index 0 but wherever the
  // write cursor is now that many slots ahead. Counting from there is the same
  // order the chart gets (see updateChartRange).
  const int slot = (s_histNext - s_histCount + idx + HIST_POINTS) % HIST_POINTS;
  if (!s_histOk[slot] || s_histTs[slot] == 0) {
    return false; // gap marker, nothing was measured
  }
  *ts = s_histTs[slot];
  memcpy(w, &s_hist[slot * HIST_SERIES], sizeof(float) * HIST_SERIES);
  return true;
}

// Defined below refreshCb (which calls it): ring + chart share one writer so
// restored rows and live samples land in identical state.
static void histPush(const float v[HIST_SERIES], uint32_t ts);

struct AppPage {
  const char *title;
  lv_obj_t *root;                          // page container in the content area
  lv_obj_t *labels[MAX_PAGE_LABELS];       // labels refreshed by the 1 Hz timer
  size_t labelCount;
  void (*build)(AppPage *);
};

// Connector lines of the flow diagram; updated per second.
static lv_obj_t *s_lineGrid = nullptr;   // haus <-> netz
static lv_obj_t *s_linePv = nullptr;     // haus <-> pv
static lv_obj_t *s_lineBat = nullptr;    // haus <-> batterie

static lv_obj_t *s_statusLabel = nullptr; // link/status badge top-right
static lv_obj_t *s_splashLabel = nullptr; // splash page label
static int s_page = PAGE_OVERVIEW;
static AppPage s_pages[PAGE_COUNT];
static lv_timer_t *s_refreshTimer = nullptr;

// Wi-Fi setup overlay (full-screen QR + SSID), shown while the provisioning
// AP is up and during a short boot test window.
static lv_obj_t *s_apOverlay = nullptr;
static lv_obj_t *s_apQr = nullptr;
static lv_obj_t *s_apSsid = nullptr;
static uint32_t s_apTestUntil = 0; // boot test deadline (ms), 0 = none

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
// Format a label. Uses the C library vsnprintf instead of lv_vsnprintf:
// LVGL's built-in printf gates %f behind LV_USE_FLOAT (which we keep off to
// avoid switching lv_value_precise_t/lv_point_precise_t to float), so every
// "%.2f kW" would otherwise print as a literal 'f'.
//
// The sign rule from NumFmt.h applies here too: a value that rounds to zero is
// shown without its minus.
static void setText(lv_obj_t *label, const char *fmt, ...) {
  char buf[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  numDropNegZero(buf);
  lv_label_set_text(label, buf);
}

// Same, but for rows that print numbers: the decimal separator is a comma, as
// on the Energie page and in the portal. snprintf always writes a point, so it
// is swapped afterwards. Never use this for text rows - a date like
// "29.09.2026" would come out as "29,09,2026".
static void setNum(lv_obj_t *label, const char *fmt, ...) {
  char buf[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  for (char *q = buf; *q; q++) {
    if (*q == '.') {
      *q = ',';
    }
  }
  numDropNegZero(buf);
  lv_label_set_text(label, buf);
}

static lv_obj_t *makeLabel(lv_obj_t *parent, const char *text,
                           const lv_font_t *font, lv_color_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  return l;
}

// Corner radius of everything that can be pressed. One value for the whole
// interface: it was 8 for the buttons and 6 for the three rows on the Service
// page, and next to each other the two radii read as two different kinds of
// control rather than as one. 8 px is what the buttons already had, and it still
// looks right on a 26 px row (half the height would be 13).
#define ROW_RADIUS 8

static lv_obj_t *makeButton(lv_obj_t *parent, const char *symbol,
                            lv_event_cb_t cb, void *userData) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_style_bg_color(btn, COL_BAR, 0);
  lv_obj_set_style_bg_color(btn, COL_ACCENT, LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, ROW_RADIUS, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, COL_MUTED, 0);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);
  lv_obj_t *l = lv_label_create(btn);
  lv_label_set_text(l, symbol);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_28_uml, 0);
  lv_obj_set_style_text_color(l, uiText(), 0);
  lv_obj_center(l);
  return btn;
}

// White circular node with centered inner text (reference "flow-item__icon").
// Returns the inner label so callers can update it (battery SOC).
static lv_obj_t *makeNode(lv_obj_t *parent, int x, int y, int d,
                          const char *inner, const lv_font_t *font) {
  lv_obj_t *node = lv_obj_create(parent);
  lv_obj_set_size(node, d, d);
  lv_obj_set_pos(node, x - d / 2, y - d / 2);
  lv_obj_set_style_radius(node, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(node, FLOW_WHITE, 0);
  lv_obj_set_style_border_width(node, 2, 0);
  lv_obj_set_style_border_color(node, FLOW_BORDER, 0);
  lv_obj_set_style_pad_all(node, 0, 0);
  lv_obj_set_style_shadow_width(node, 0, 0);
  lv_obj_t *lbl = makeLabel(node, inner, font, FLOW_GRAY);
  lv_obj_center(lbl);
  return lbl;
}

// Value label under a node: red number when live, gray when "no data".
static lv_obj_t *makeValueLabel(lv_obj_t *parent, int x, int y) {
  lv_obj_t *l = makeLabel(parent, "--", &lv_font_montserrat_16_uml, FLOW_LINE);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(l, 120);
  return l;
}

// Position a label so its centre sits exactly on (cx, cy). Used for the flow
// arrows so the glyph sits precisely on the connector line.
static void placeArrow(lv_obj_t *label, lv_coord_t cx, lv_coord_t cy) {
  lv_obj_update_layout(label);
  lv_obj_set_pos(label, cx - lv_obj_get_width(label) / 2,
                  cy - lv_obj_get_height(label) / 2);
}

// Summary card (portal "info-box"): one red value line with the unit appended
// inline ("12,3 kWh", "87 %") and a muted caption at the bottom. The value is
// refreshed from data; the caption stays static.
static void makeStatCard(lv_obj_t *parent, int x, int y, int w, int h,
                         const char *caption, lv_obj_t **valOut,
                         lv_obj_t **capOut) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, w, h);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_style_bg_color(card, COL_CARD, 0);
  lv_obj_set_style_radius(card, 10, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, COL_BORDER, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);

  lv_obj_t *val = makeLabel(card, "--", &lv_font_montserrat_28_uml, FLOW_RED);
  lv_obj_align(val, LV_ALIGN_TOP_MID, 0, 17);
  lv_obj_t *cap = makeLabel(card, caption, &lv_font_montserrat_16_uml, COL_MUTED);
  lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, -11);

  if (valOut) {
    *valOut = val;
  }
  if (capOut) {
    *capOut = cap;
  }
}

// ---------------------------------------------------------------------------
// "Energie" page helpers
// ---------------------------------------------------------------------------
// Bar geometry: 480 px content, 20 px margin. Per row a label line (name left,
// value right) over a full-width bar, so the label doubles as the legend and
// the bar can use the whole width.
static const int EB_BAR_X = 20;
static const int EB_BAR_W = 440;
static const int EB_BAR_H = 16;
static const int EB_LABEL_GAP = 24; // label line (20 px) + 4 px air to the bar
static const int EB_ROW0_Y = 82;   // first label line (below heading + selector)
static const int EB_ROW_H = 58;     // label (20) + gap (4) + bar (16) + air (18)

// "< 1000 kWh" prints as "12,4 kWh", above that in MWh ("1,23 MWh"). The
// decimal separator is a comma, as in the portal.
static void setEnergyValue(lv_obj_t *label, float wh) {
  char buf[32];
  if (wh < 1000000.0f) {
    fmtNumComma(buf, sizeof(buf), "%.1f kWh", (double)(wh / 1000.0f));
  } else {
    fmtNumComma(buf, sizeof(buf), "%.2f MWh", (double)(wh / 1000000.0f));
  }
  lv_label_set_text(label, buf);
}

// The four meter values of the selected period, in Wh.
static void energyPeriodValues(const RctSnapshot &s, int period,
                               float out[ENERGY_ROWS]) {
  float pv, feed, load, grid, ext;
  switch (period) {
    case 1: // Monat
      pv = s.monthPvWh;   feed = s.monthFeedInWh;
      load = s.monthLoadWh; grid = s.monthGridLoadWh;
      ext = s.monthExtWh;
      break;
    case 2: // Jahr
      pv = s.yearPvWh;    feed = s.yearFeedInWh;
      load = s.yearLoadWh;  grid = s.yearGridLoadWh;
      ext = s.yearExtWh;
      break;
    case 3: // Gesamt
      // The two lifetime grid meters are the ones already tracked as
      // feedInEnergyWh / gridDrawTotalWh.
      pv = s.totalPvWh;   feed = s.feedInEnergyWh;
      load = s.totalLoadWh; grid = s.gridDrawTotalWh;
      ext = s.totalExtWh;
      break;
    default: // Tag
      pv = s.dayPvWh;     feed = s.dayFeedInWh;
      load = s.dayLoadWh;   grid = s.dayGridLoadWh;
      ext = s.dayExtWh;
      break;
  }
  // Externe Energie (S0-Generator). Das Geraet fuehrt dafuer eine eigene
  // Zaehlerfamilie, e_ext_*: in den e_dc_* (Erzeugung) kommt der S0-Ertrag
  // nicht hinein, und im Lastzaehler taucht er ebenfalls nicht auf - sonst
  // waere er bereits in e_load enthalten und braeuchte hier nichts ergaenzt.
  // Die Rechnung des Geraets ist insofern eigenartig, und genau deshalb geht
  // derselbe Betrag auf beide Seiten:
  //
  //   Erzeugung = PV (DC) + extern
  //   Verbrauch = Haus + extern
  //
  // Ohne das waere der externe Ertrag weder in "PV Erzeugung" noch in
  // "Verbrauch" und "Eigenverbrauch" sichtbar, obwohl er das Haus versorgt.
  // Der Eigenverbrauch unten bleibt damit die Differenz aus dem, was im Haus
  // ankam, und dem, was dafuer aus dem Netz kam.
  pv += ext;
  load += ext;
  out[EB_VAL_PV] = pv;
  // The feed-in counters arrive negative on the real device (measured: -20,1 kWh
  // on a day with 32,5 kWh production). Shown as reported, the "Netzeinspeisung"
  // bar grew leftwards and the value read -549,7 kWh, which is not an amount of
  // energy that was fed in. The magnitude is the fed-in energy, so the sign is
  // dropped here - once, for every period.
  out[EB_VAL_FEED] = feed < 0.0f ? -feed : feed;
  out[EB_VAL_GRID] = grid;
  out[EB_VAL_LOAD] = load;
  // Eigenverbrauch = Erzeugung minus Einspeisung: alles, was das Geraet
  // erzeugt hat und nicht eingespeist wurde, ist im eigenen Haus genutzt worden
  // - direkt, ueber den externen Generator oder als Ladung in den Akku. Die
  // Akkuladung zaehlt dazu, weil dieser Akku ausschliesslich das eigene Haus
  // versorgt und nie zur Einspeisung dient; ihre Entnahme erscheint in einem
  // anderen Zeitraum als Verbrauch, und genau deshalb ist hier nichts doppelt
  // gezaehlt. Geklammert bei 0: die Zaehler laufen nach einem Geraete-Neustart
  // kurz auseinander, und ein negativer Balken waere sinnlos.
  const float selfUse =
      pv - out[EB_VAL_FEED] > 0.0f ? pv - out[EB_VAL_FEED] : 0.0f;
  out[EB_VAL_SELF] = selfUse;
}

// The five figures of one period plus the two percentages, for whoever needs
// them as numbers instead of as bars. The web interface asks here, so its JSON
// and the Energie page cannot report different numbers for the same period.
//
// period is 0 day, 1 month, 2 year, 3 total. The percentages follow the Heute
// page: autarky is what the household covered itself, ownShare is how much of
// the generation stayed here.
void guiEnergyPeriod(int period, float wh[5], float *autarky,
                     float *ownShare) {
  energyPeriodValues(rctState, period, wh);
  const float pv = wh[EB_VAL_PV];
  const float grid = wh[EB_VAL_GRID];
  const float load = wh[EB_VAL_LOAD];
  *autarky = load > 0.0f ? (1.0f - grid / load) * 100.0f : 100.0f;
  if (*autarky < 0.0f) {
    *autarky = 0.0f;
  }
  *ownShare = pv > 0.0f ? wh[EB_VAL_SELF] / pv * 100.0f : 0.0f;
  if (*ownShare > 100.0f) {
    *ownShare = 100.0f;
  }
}

// Highlight the active period button (portal dashboard style).
//
// The text of the active button is one fixed colour, not the page background:
// the button has a background of its own, and the theme walk deliberately leaves
// the text on such objects alone - a themed colour here would keep whatever the
// theme was when the pages were built and disagree with the rest of the page
// after a switch. Dark on the blue is also the better of the two (about 5.4:1
// against white on it).
static const lv_color_t COL_ON_ACCENT = lv_color_hex(0x101418);

static void energySelectStyle() {
  for (int i = 0; i < ENERGY_PERIODS; i++) {
    if (!s_ebarBtn[i]) {
      continue;
    }
    lv_obj_set_style_bg_color(s_ebarBtn[i],
                              i == s_energyPeriod ? COL_ACCENT : COL_CARD, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(s_ebarBtn[i], 0),
                                i == s_energyPeriod ? COL_ON_ACCENT : COL_MUTED,
                                0);
  }
}

static void energyPeriodCb(lv_event_t *e) {
  s_energyPeriod = (int)(intptr_t)lv_event_get_user_data(e);
  energySelectStyle();
  // The values follow on the next 1 Hz tick; no immediate redraw needed.
}

// ---------------------------------------------------------------------------
// Page builders
// ---------------------------------------------------------------------------

// Energiefluss diagram (reference layout: PV left, haus center, batterie
// bottom-centre below haus, netz right).
static void pageBuildOverview(AppPage *p) {
  lv_obj_t *root = p->root;

  // Node circles.
  makeNode(root, 240, 82, 92, LV_SYMBOL_HOME, &lv_font_montserrat_28_uml); // haus
  makeNode(root, 60, 80, 60, kFlowSolarIcon, &lv_font_mdi_icons_28); // pv
  // Battery node: battery icon on top, SOC % below (inside the node).
  lv_obj_t *bat = lv_obj_create(root);
  lv_obj_set_size(bat, 60, 60);
  lv_obj_set_pos(bat, 240 - 30, 210 - 30); // centred below the house
  lv_obj_set_style_radius(bat, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(bat, FLOW_WHITE, 0);
  lv_obj_set_style_border_width(bat, 2, 0);
  lv_obj_set_style_border_color(bat, FLOW_BORDER, 0);
  lv_obj_set_style_pad_all(bat, 0, 0);
  lv_obj_set_style_shadow_width(bat, 0, 0);
  // Battery icon + SOC % grouped near the node centre (icon just above the
  // centre line, percentage just below it). BATTERY_3 is the three-quarter
  // glyph: a completely full battery next to a "71 %" reading below it just
  // looks wrong. Resolved through the font fallback chain (uml -> montserrat,
  // which covers the 0xF240 symbol block).
  lv_obj_t *batIco =
      makeLabel(bat, LV_SYMBOL_BATTERY_3, &lv_font_montserrat_20_uml, FLOW_GRAY);
  lv_obj_align(batIco, LV_ALIGN_CENTER, 0, -7);
  lv_obj_t *batSoc =
      makeLabel(bat, "--", &lv_font_montserrat_14_uml, FLOW_GRAY);
  lv_obj_align(batSoc, LV_ALIGN_CENTER, 0, 9);
  p->labels[OV_BAT_SOC] = batSoc;

  // NETZ node: the transmission tower.
  makeNode(root, 420, 80, 60, kFlowGridIcon, &lv_font_mdi_icons_28);

  // Connector lines (haus <-> node), animated later via color/style.
  static const lv_point_precise_t ptsGrid[2] = {{240, 82}, {420, 80}};
  static const lv_point_precise_t ptsPv[2] = {{240, 82}, {60, 80}};
  static const lv_point_precise_t ptsBat[2] = {{240, 82}, {240, 210}};
  s_lineGrid = lv_line_create(root);
  s_linePv = lv_line_create(root);
  s_lineBat = lv_line_create(root);
  lv_line_set_points(s_lineGrid, ptsGrid, 2);
  lv_line_set_points(s_linePv, ptsPv, 2);
  lv_line_set_points(s_lineBat, ptsBat, 2);
  lv_obj_set_style_line_width(s_lineGrid, 3, 0);
  lv_obj_set_style_line_width(s_linePv, 2, 0);
  lv_obj_set_style_line_width(s_lineBat, 2, 0);
  lv_obj_set_style_line_rounded(s_lineGrid, true, 0);
  lv_obj_set_style_line_rounded(s_linePv, true, 0);
  lv_obj_set_style_line_rounded(s_lineBat, true, 0);
  lv_obj_set_style_line_color(s_lineGrid, FLOW_LINE, 0);
  lv_obj_set_style_line_color(s_linePv, FLOW_LINE, 0);
  lv_obj_set_style_line_color(s_lineBat, FLOW_LINE, 0);
  lv_obj_move_background(s_lineGrid);
  lv_obj_move_background(s_linePv);
  lv_obj_move_background(s_lineBat);

  // Values under each node. The netz direction ("Bezug" / "Einspeisung") is not
  // spelled out: the sign is already visible in the value, the arrow on the
  // connector shows where the power goes, and the status table below names it.
  p->labels[OV_GRID_VAL] = makeValueLabel(root, 360, 118);

  // Haus value sits right of the vertical battery line (x=240) so the line no
  // longer runs through the text.
  // House consumption: nudged up and left (10 up / 5 right, then 5 up / 5 left)
  // so the number visually belongs to the house node above it.
  p->labels[OV_HOUSE_VAL] = makeValueLabel(root, 250, 120);
  p->labels[OV_PV_VAL] = makeValueLabel(root, 0, 116);
  p->labels[OV_BAT_VAL] = makeValueLabel(root, 180, 247);

  // Direction arrows on the connectors (point toward the flow source),
  // centred exactly on the line: grid/PV lines run at y=81 at these x
  // positions, the battery line is vertical at x=240.
  p->labels[OV_GRID_ARROW] =
      makeLabel(root, LV_SYMBOL_RIGHT, &lv_font_montserrat_16_uml, FLOW_RED);
  placeArrow(p->labels[OV_GRID_ARROW], 322, 81);
  lv_obj_add_flag(p->labels[OV_GRID_ARROW], LV_OBJ_FLAG_HIDDEN);
  p->labels[OV_PV_ARROW] =
      makeLabel(root, LV_SYMBOL_RIGHT, &lv_font_montserrat_16_uml, FLOW_RED);
  placeArrow(p->labels[OV_PV_ARROW], 150, 81);
  lv_obj_add_flag(p->labels[OV_PV_ARROW], LV_OBJ_FLAG_HIDDEN);
  p->labels[OV_BAT_ARROW] =
      makeLabel(root, LV_SYMBOL_DOWN, &lv_font_montserrat_16_uml, FLOW_RED);
  placeArrow(p->labels[OV_BAT_ARROW], 240, 146);
  lv_obj_add_flag(p->labels[OV_BAT_ARROW], LV_OBJ_FLAG_HIDDEN);

  // Island mode (grid outage): a red warning triangle between haus and netz,
  // just above the grid connector. Centred on the connector's midpoint (x=330)
  // and 25 px above the line, so it reads as a marker for that link rather than
  // as something attached to a node. Hidden while the grid is connected.
  p->labels[OV_ISLAND] =
      makeLabel(root, LV_SYMBOL_WARNING, &lv_font_montserrat_20_uml, FLOW_RED);
  placeArrow(p->labels[OV_ISLAND], 330, 56);
  lv_obj_add_flag(p->labels[OV_ISLAND], LV_OBJ_FLAG_HIDDEN);

  // Status table (2x2): Erzeugung / Verbrauch / Netz / Batterie.
  struct {
    int x, y;
    LangId id;
    int labelIdx;
  } cells[4] = {
      {12, 284, T_D_ROW_PRODUCTION, OV_T_ERZ},
      {248, 284, T_D_ROW_CONSUMPTION, OV_T_VERB},
      {12, 324, T_D_ROW_GRID, OV_T_NETZ},
      {248, 324, T_D_ROW_BATTERY, OV_T_BAT},
  };
  for (int i = 0; i < 4; i++) {
    // Names muted, values white: the value is what the eye should land on.
    lv_obj_t *nm =
        makeLabel(root, tr(cells[i].id), &lv_font_montserrat_14_uml, COL_MUTED);
    lv_obj_set_pos(nm, cells[i].x, cells[i].y);
    lv_obj_t *st = makeLabel(root, "--", &lv_font_montserrat_14_uml, FLOW_WHITE);
    lv_obj_set_pos(st, cells[i].x + 110, cells[i].y);
    p->labels[cells[i].labelIdx] = st;
  }
  p->labelCount = OV_LABEL_COUNT;
}

// "Energie": accumulated energies of the selected period as bars, mirroring
// the portal's "Auswahl Messungen" block. The five chip + name rows double as
// the color legend; bars are normalized to the largest value of the period.
static void pageBuildEnergy(AppPage *p) {
  lv_obj_t *root = p->root;

  // Period selector: Tag | Monat | Jahr | Gesamt. Sits just below the page
  // heading (8..28), which took the top row this page used before.
  for (int i = 0; i < ENERGY_PERIODS; i++) {
    lv_obj_t *btn = lv_button_create(root);
    lv_obj_set_size(btn, 108, 34);
    lv_obj_set_pos(btn, 12 + i * 114, 40);
    lv_obj_set_style_bg_color(btn, COL_CARD, 0);
    lv_obj_set_style_bg_color(btn, COL_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, COL_BORDER, 0);
    lv_obj_add_event_cb(btn, energyPeriodCb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);
    lv_obj_t *l = makeLabel(btn, tr(kPeriodId[i]), &lv_font_montserrat_14_uml,
                            COL_MUTED);
    lv_obj_center(l);
    s_ebarBtn[i] = btn;
  }

  // One row per series: label line (name left, value right) above a full-width
  // bar. The name takes the series color, the bar below it the same one - that
  // is the whole legend.
  for (int i = 0; i < ENERGY_ROWS; i++) {
    const int y = EB_ROW0_Y + i * EB_ROW_H;
    const lv_color_t c = lv_color_hex(kEnergyColor[i]);

    // Label white: the bar underneath already carries the series color, a
    // colored name on top of a colored bar was just noise.
    lv_obj_t *name =
        makeLabel(root, tr(kEnergyId[i]), &lv_font_montserrat_16_uml, uiText());
    lv_obj_set_pos(name, EB_BAR_X, y);

    p->labels[i] = makeLabel(root, "--", &lv_font_montserrat_16_uml, uiText());
    lv_obj_set_width(p->labels[i], 120);
    lv_obj_set_style_text_align(p->labels[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(p->labels[i], 340, y);

    lv_obj_t *track = lv_obj_create(root);
    lv_obj_set_size(track, EB_BAR_W, EB_BAR_H);
    lv_obj_set_pos(track, EB_BAR_X, y + EB_LABEL_GAP);
    lv_obj_set_style_bg_color(track, COL_CARD, 0);
    lv_obj_set_style_radius(track, 4, 0);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_set_style_pad_all(track, 0, 0);
    lv_obj_set_style_shadow_width(track, 0, 0);

    lv_obj_t *fill = lv_obj_create(track);
    lv_obj_set_size(fill, 0, EB_BAR_H);
    lv_obj_set_pos(fill, 0, 0);
    lv_obj_set_style_bg_color(fill, c, 0);
    lv_obj_set_style_radius(fill, 4, 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_pad_all(fill, 0, 0);
    lv_obj_set_style_shadow_width(fill, 0, 0);
    s_ebarFill[i] = fill;
  }

  p->labelCount = EB_LABEL_COUNT;
  energySelectStyle();
}

// Current-day summary page, mirroring the portal "Übersicht" (Erzeugt /
// Eigenverbrauch / Eingespeist kWh boxes) and "Energiestatistiken" (Autarkie /
// Eigenverbrauch percentage gauges). All energy values are scaled ÷1000 → kWh.
static void pageBuildHeute(AppPage *p) {
  lv_obj_t *root = p->root;

  struct {
    int x, y, w, h;
    LangId caption;
    int valIdx, capIdx;
  } cards[] = {
      // Top row: three cards over the same 16..464 span as the two rows below.
      // 448 px for three cards leaves 5 px between them (three times 4 would ask
      // for 440, which no integer width divides) - and the third card used to be
      // 144 px wide, which left the row 8 px short on the right and made this
      // card look off-centre against the two below it.
      {16, 36, 146, 96, T_D_CARD_PRODUCED, EN_GEN_VAL, EN_GEN_LBL},
      {167, 36, 146, 96, T_D_CARD_SELFUSE, EN_SELF_VAL, EN_SELF_LBL},
      {318, 36, 146, 96, T_D_CARD_FEDIN, EN_FEED_VAL, EN_FEED_LBL},
      // Lower rows: two cards with the same 4 px gap as the first row, so
      // 222 px wide starting at 16 and 242.
      {16, 146, 222, 88, T_D_CARD_CONSUMED, EN_VERB_VAL, EN_VERB_LBL},
      {242, 146, 222, 88, T_D_CARD_IMPORTED, EN_BEZU_VAL, EN_BEZU_LBL},
      {16, 248, 222, 100, T_D_CARD_SELF, EN_AUT_VAL, EN_AUT_LBL},
      {242, 248, 222, 100, T_D_CARD_SELFRATE, EN_EVB_VAL, EN_EVB_LBL},
  };
  for (unsigned i = 0; i < sizeof(cards) / sizeof(cards[0]); i++) {
    makeStatCard(root, cards[i].x, cards[i].y, cards[i].w, cards[i].h,
                 tr(cards[i].caption), &p->labels[cards[i].valIdx],
                 &p->labels[cards[i].capIdx]);
  }
  p->labelCount = EN_LABEL_COUNT;
}

// Info and device pages share one row layout: a fixed row name on the left and
// a value column at ROW_VAL_X. Name and value are separate labels, so the
// values start at the same x no matter how long they are. One padded string per
// row did align them, but right-aligned - and a negative battery power would
// have shifted its own unit by one character.
static const int ROW_VAL_X = 156;

// Add one "name / value" row. Only the value label is stored in the page: the
// names never change.
static void makeRow(AppPage *p, lv_obj_t *root, int i, const char *name,
                    const char *value) {
  lv_obj_t *n = makeLabel(root, name, &lv_font_montserrat_16_uml, uiText());
  lv_obj_align(n, LV_ALIGN_TOP_LEFT, 24, ROW_Y0 + i * ROW_PITCH);
  p->labels[i] = makeLabel(root, value, &lv_font_montserrat_16_uml, uiText());
  lv_obj_align(p->labels[i], LV_ALIGN_TOP_LEFT, ROW_VAL_X, ROW_Y0 + i * ROW_PITCH);
}

static void pageBuildInfo(AppPage *p) {
  static const LangId names[INF_LABEL_COUNT] = {
      T_D_IF_NAME,     T_D_IF_SOFTWARE, T_D_IF_HOST,   T_D_IF_PORT,
      T_D_IF_LINK,     T_D_IF_LASTDATA, T_D_IF_UPTIME, T_D_IF_L1,
      T_D_IF_L2,       T_D_IF_L3,       T_D_IF_PV,     T_D_IF_CORE,
      T_D_IF_HEATSINK, T_D_IF_FREQ,
  };
  static const char *const values[INF_LABEL_COUNT] = {
      "--",     "--",     "--",     "--",     "--",     "-- s",   "-- s",
      "-- kW",  "-- kW",  "-- kW",  "-- kW",  "-- °C",  "-- °C",  "-- Hz",
  };
  for (int i = 0; i < INF_LABEL_COUNT; i++) {
    makeRow(p, p->root, i, tr(names[i]), values[i]);
  }
  p->labelCount = INF_LABEL_COUNT;
}

// Battery ("Akku") page: SOC / battery power / temperature / next calibration
// / cycles / SOH / island mode - everything battery-specific, moved here from
// the old "Gerät" page. Same two-column row layout as the Info page.
static void pageBuildDevice(AppPage *p) {
  static const LangId names[DEV_LABEL_COUNT] = {
      T_D_BA_SOC,   T_D_BA_POWER, T_D_BA_TEMP, T_D_BA_CALIB,
      T_D_BA_CYCLES, T_D_BA_SOH, T_D_BA_ISLAND,
  };
  static const char *const values[DEV_LABEL_COUNT] = {
      "-- %", "--", "-- °C", "--",
      "--", "-- %", "--",
  };
  for (int i = 0; i < DEV_LABEL_COUNT; i++) {
    makeRow(p, p->root, i, tr(names[i]), values[i]);
  }
  p->labelCount = DEV_LABEL_COUNT;
}

// ---------------------------------------------------------------------------
// Service page: decoded battery status, a "back to provisioning" button and
// the decoded inverter faults.
// ---------------------------------------------------------------------------

// battery.bat_status (OID 0x70A2AF4F) decode.
//
// A gesetztes Bit bedeutet den Zustand, ein leeres Register ist der normale
// Leerlauf. Die verbreitete rctclient-Zeile "if (value & 1032 == 0) =>
// calibration" liest das genau verkehrt herum und meldet bei v == 0
// "Kalibrierung laeuft" plus "Balancing laeuft" - auf echter Hardware also bei
// jedem unauffaelligen Geraet. Die Bitbelegung ist durch Beobachtung am
// Geraet belegt (HA-Integration Issue #264, python-rctclient Issue #39):
//
//   Bit 3  /     8 = Kalibrierung, Ladephase   (SOC-Ziel 100 %)
//   Bit 10 /  1024 = Kalibrierung, Entladephase (SOC-Ziel 0 %)
//   Bit 11 /  2048 = Balancing der Batteriezellen
//   Bit 9  /   512 = Unterspannung / leer
//   alle anderen = undokumentiert
//
// Laden/Entladen steht in diesem Register nicht zuverlaessig; dafuer wird das
// Vorzeichen der Batterieleistung verwendet (positiv = Ladung), das unabhaengig
// davon ist und ohnehin schon angezeigt wird.
static void serviceBatteryDecode(uint32_t v, float batPower, char *out,
                                 size_t n) {
  char state[40] = "";
  bool namesPhase = false; // Zustand nennt die Lade-/Entladerichtung selbst
  if (v == 0) {
    strlcpy(state, tr(T_D_ST_READY), sizeof(state));
  } else if (v & (1u << 3)) {
    strlcpy(state, tr(T_D_ST_CALCHARGE), sizeof(state));
    namesPhase = true;
  } else if (v & (1u << 10)) {
    strlcpy(state, tr(T_D_ST_CALDISCHARGE), sizeof(state));
    namesPhase = true;
  } else if (v & (1u << 11)) {
    strlcpy(state, tr(T_D_ST_BALANCING), sizeof(state));
  } else if (v & (1u << 9)) {
    strlcpy(state, tr(T_D_ST_UNDERVOLT), sizeof(state));
  } else if (v & 1u) {
    strlcpy(state, tr(T_D_ST_OFF), sizeof(state));
  } else {
    // Unbekannte Bits: nicht raten, der Rohwert steht darunter.
    snprintf(state, sizeof(state), tr(T_D_ST_RAW), (unsigned long)v);
  }
  // Zusatz, der unabhaengig vom Register gilt. Mehrere Zustaende koennen
  // gleichzeitig gesetzt sein, deshalb werden sie verknuepft statt als else-if.
  if (v & (1u << 11) && !(v & ((1u << 3) | (1u << 10)))) {
    strncat(state, tr(T_D_ST_BALPLUS), sizeof(state) - strlen(state) - 1);
  }
  // Lade-/Entladerichtung aus der Leistung. Entfaellt, wenn der Zustand sie
  // bereits nennt - "Kalibrierung (Entladephase) (entlaedt)" waere doppelt.
  // p_acc_lp is positive while discharging (measured), so the comparison is
  // the other way round than one would guess from the register name.
  if (!namesPhase) {
    if (batPower > 50.0f) {
      strncat(state, tr(T_D_ST_DISCHARGING), sizeof(state) - strlen(state) - 1);
    } else if (batPower < -50.0f) {
      strncat(state, tr(T_D_ST_CHARGING), sizeof(state) - strlen(state) - 1);
    }
  }
  strlcpy(out, state, n);
}

// Fault descriptions from rctclient "Faults": the index is the bit number in
// fault[0..3].flt (bit n -> "F<n>"). The 128 texts live in src/i18n, one per
// bit, as T_FAULT_0 + n - so the wording is a table entry like every other text
// on the display, and the German build shows exactly what the list has always
// shown.
//
// Active faults -> "F<n> <description>" lines, capped so the list stays inside
// the text area reserved for it; surplus faults are summarized. Returns the
// total number of active faults (0 = none).
//
// The cap is a *character* budget, not a row count, because the label is
// narrower than the page (280 px next to the web block) and a fault text of
// 40-odd characters therefore wraps onto a second line. kCharsPerLine is
// measured from the 14 px font: 280 px fit about 38 characters. kMaxLines is
// derived from the space the block actually has - 142 to 236 px at 18 px per
// line - so the list can never reach the "SD-Log" heading below it.
static int serviceFaultText(const uint32_t *bits, char *out, size_t n) {
  const size_t kCharsPerLine = 38;
  const size_t kMaxLines = 5;
  const size_t budget = kCharsPerLine * kMaxLines;
  out[0] = '\0';
  size_t used = 0;
  int count = 0;
  int shown = 0;
  for (int bit = 0; bit < 128; bit++) {
    if (bits[bit / 32] & (1u << (bit % 32))) {
      const char *fault = tr((LangId)(T_FAULT_0 + bit));
      count++;
      if (used + 4 + strlen(fault) > budget) {
        continue; // would push the list into the heading below
      }
      int w = snprintf(out + used, n - used, tr(T_D_FAULT_LINE), bit, fault);
      if (w <= 0 || (size_t)w >= n - used) {
        break;
      }
      used += (size_t)w;
      shown++;
    }
  }
  if (count > shown) {
    if (used > 0) {
      used--; // drop the trailing newline ...
    }
    snprintf(out + used, n - used, tr(T_D_MORE_FAULTS), count - shown);
  }
  return count;
}

// "WLAN-Setup starten": re-open the provisioning access point.
static void serviceSetupCb(lv_event_t *e) {
  (void)e;
  restartProvisioning();
}

// Tap on the web code: draw a new one and show it right away. The 1 s refresh
// would get there a refresh later, and whoever taps and then reads the display
// would still see the old code - the new one only appears after the next tick,
// which is exactly the moment the code is wanted. Only useful in normal
// operation (without a web server there is no code and nothing to guard), so the
// tap does nothing visible then - the page shows "--" in that state.
static void webCodeNewCb(lv_event_t *e) {
  lv_obj_t *chip = (lv_obj_t *)lv_event_get_target(e);
  if (!webRunning()) {
    return;
  }
  setText(chip, tr(T_D_CODE), webNewCode());
}

// Tap on the output's function: the next one in the list. Stored immediately,
// because a function that silently came back after the next power cut would be
// the more surprising behaviour.
static void relayModeCb(lv_event_t *e) {
  (void)e;
  relayCycleMode();
}

// Forward declaration: the callbacks above sit next to the widgets they belong
// to, the update function further down where it can be read on its own.
static void serviceRelayState();

// The test button, so the layout below can move it with the line above it.
static lv_obj_t *s_testBtn = nullptr;

// Air between the function row and the state line, and between that line and the
// test button under it.
#define RELAY_ROW_GAP 11
#define RELAY_BTN_GAP 21

// Put the state line and the test button under the function row, whatever height
// that row has at the moment. At 160 px wide the row needs two lines for
// "Inselbetrieb > 5000 W" and for "Island mode > 5000 W" and one line for
// everything else, so a fixed pair of positions either overlaps the longer ones
// or leaves a hole under the shorter ones - and the hole is what a page looks
// like when something is sitting 30 px too low without anything being wrong with
// it. Called after the row's text is set, so the height is the current one.
static void serviceRelayLayout() {
  AppPage &sv = s_pages[PAGE_SERVICE];
  lv_obj_t *row = sv.labels[SV_RELAY];
  lv_obj_t *st = sv.labels[SV_RELAY_ST];
  if (row == nullptr || st == nullptr) {
    return;
  }
  lv_obj_update_layout(row); // the new text has to be measured before it is used
  const int ySt = (int)lv_obj_get_y(row) + (int)lv_obj_get_height(row) +
                  RELAY_ROW_GAP;
  lv_obj_set_y(st, ySt);
  if (s_testBtn != nullptr) {
    lv_obj_set_y(s_testBtn, ySt + RELAY_BTN_GAP);
  }
}

// "Test 5 s an / 5 s aus": the check that this really is the right pin and the
// right polarity, without a browser and without data from the inverter. Pressed
// again while it runs, it does nothing.
static void relayTestCb(lv_event_t *e) {
  (void)e;
  if (relayStartTest()) {
    // Show "Test: AN" in the same tap, not on the next refresh - the whole point
    // of the button is to watch it switch every 5 s.
    serviceRelayState();
  }
}

// Function name plus the threshold it compares against, for the one row that
// shows both. The threshold only means something for the two modes that have
// one; the other two show their name alone.
static void relayModeText(char *out, size_t n) {
  const RelayMode m = relayMode();
  if (m == RelayMode::GridDraw || m == RelayMode::PvSurplus) {
    snprintf(out, n, tr(T_D_OUT_THRESHOLD), relayModeName(m),
             relayThreshold());
  } else {
    snprintf(out, n, "%s", relayModeName(m));
  }
}

// What the output is doing right now, next to what it follows. Separate from the
// 1 s page refresh because the state it shows changes on its own schedule: the
// 5 s test flips every 5 s, and a line that lags a refresh behind a relay that
// has already switched says the opposite of what is happening. relayUpdate()
// calls guiRelayStateChanged() the moment it switches, so the panel is up to date
// within the same pass; the 1 s refresh still runs, for the measured value.
static void serviceRelayState() {
  AppPage &sv = s_pages[PAGE_SERVICE];
  lv_obj_t *st = sv.labels[SV_RELAY_ST];
  if (st == nullptr) {
    return;
  }
  const RelayMode m = relayMode();
  // "AN" / "AUS" in beiden Zeilen darunter: der Test und die Funktion zeigen
  // fuer denselben Zustand immer dasselbe Wort.
  const char *on = tr(T_D_OUT_ON);
  const char *off = tr(T_D_OUT_OFF);
  if (relayTestRunning()) {
    // While the test runs, what it is doing is the point: "AN" and "AUS" every
    // 5 s, not the mode it is temporarily overriding.
    setText(st, tr(T_D_OUT_TEST), relayIsOn() ? on : off);
    lv_obj_set_style_text_color(st, relayIsOn() ? uiOk() : COL_MUTED, 0);
    return;
  }
  // Only the two threshold modes show a number, and only they can say how old
  // that number is. The threshold for "old" is the same one the badge uses, so
  // "wartet" above and "(letzte Messung)" here always mean the same moment.
  const bool stale = dataAgeMs(millis(), rctState.lastUpdateMs) > kDataStaleMs;
  if (m == RelayMode::Off) {
    setText(st, "%s", tr(T_D_OUT_NOTHING));
  } else if (m == RelayMode::GridDraw || m == RelayMode::PvSurplus) {
    setText(st, tr(stale ? T_D_OUT_NOW_STALE : T_D_OUT_NOW), relayIsOn() ? on : off,
            (int)lroundf(relayTriggerValue()));
  } else {
    setText(st, tr(T_D_OUT_MODE), relayIsOn() ? on : off, relayModeName(m));
  }
  // Stale and therefore no news: dimmed, whatever the contact is doing. The
  // rule may well be acting on this number for several more minutes
  // (DATA_MAX_AGE_MS in Relay.cpp), so saying so is not decoration.
  lv_obj_set_style_text_color(st,
                              stale ? COL_MUTED
                                    : (relayIsOn() ? uiOk() : COL_MUTED),
                              0);
}

void guiRelayStateChanged() {
  // relayUpdate() only ever switches on its own 1 Hz pass, so this is called
  // from the loop task just after it did - the same task and the same LVGL
  // context the refresh timer uses. Nothing here may block.
  serviceRelayState();
}

// ---------------------------------------------------------------------------
// Screenshot
//
// The whole point is to be able to see the panel without someone having to
// describe it, so the capture happens through LVGL's own snapshot API rather
// than by reading the panel's framebuffer: the driver keeps that pointer to
// itself, and LVGL's version is a defined interface.
//
// Buffer: 480 x 480 RGB565 = 460 800 B, so it has to be in PSRAM. Held for the
// duration of the card write, which is seconds - far too long to sit on the
// 8 kB GUI stack and much longer than a transient allocation should live. It is
// therefore a single static buffer that the free() below hands out, and the
// service page ignores further requests while a shot is in flight (see
// sdScreenshot(), which refuses a second one rather than reading a buffer the
// worker has already finished with).
// ---------------------------------------------------------------------------
static uint16_t *s_shotBuf = nullptr;

// Delay between pressing the button and the capture. The button lives on the
// Service page, so an immediate shot could only ever photograph the Service
// page - the delay is what makes the button useful at all: press it, switch to
// the page that misbehaves, and the picture is taken there. The deadline lives
// outside the callback so it survives the page switch.
#define SHOT_DELAY_MS 5000
static uint32_t s_shotDueMs = 0; // capture armed, due at this tick; 0 = none

// Was the web interface running on the previous tick? Used to clear the code
// when it stops (provisioning took the radio) without blinking the two fields
// on every tick where the answer is unchanged.
static bool s_webWasUp = false;

// The actual capture, run SHOT_DELAY_MS after the button was pressed.
static void takeShotNow() {
  // Resolution from the display, not the literal 480: the panel is 480x480
  // today, and a hardcoded size here would quietly truncate a future panel
  // instead of failing. lv_display_t is opaque in LVGL 9, hence the accessors.
  lv_display_t *disp = dispGetHandle();
  if (disp == nullptr) {
    return;
  }
  const int w = (int)lv_display_get_horizontal_resolution(disp);
  const int h = (int)lv_display_get_vertical_resolution(disp);
  const size_t need = (size_t)w * (size_t)h * sizeof(uint16_t);
  if (!s_shotBuf) {
    // Not a local array and not malloc: this outlives the call by seconds and
    // has to be 460 kB, which is PSRAM territory.
    s_shotBuf = (uint16_t *)heap_caps_malloc(need, MALLOC_CAP_SPIRAM);
    if (s_shotBuf == nullptr) {
      s_shotBuf = (uint16_t *)heap_caps_malloc(need, MALLOC_CAP_8BIT);
    }
    if (s_shotBuf == nullptr) {
      Serial.printf("Screenshot: %lu B nicht freier\n", (unsigned long)need);
      lv_label_set_text(s_pages[PAGE_SERVICE].labels[SV_SHOT],
                        tr(T_D_SHOT_NOMEM));
      return;
    }
  }

  // RGB565 is what LVGL renders into here, so the conversion on the card side
  // is a pure format change. Applying the panel's colour correction here as
  // well is what makes the file match the physical panel rather than the
  // uncorrected values LVGL holds.
  //
  // Not lv_snapshot_take_to_buf(): that deprecated wrapper takes an
  // lv_image_dsc_t* to write the result descriptor into, and on success it
  // does so unconditionally - so passing NULL, which is right here because
  // there is no descriptor to hand out and the pixels go straight to the card,
  // made it memcpy to address zero. lv_draw_buf_init() + the current API does
  // the same work on the same buffer without that trap.
  lv_draw_buf_t shot;
  if (lv_draw_buf_init(&shot, (uint32_t)w, (uint32_t)h, LV_COLOR_FORMAT_RGB565,
                       0, s_shotBuf, (uint32_t)need) != LV_RESULT_OK) {
    Serial.printf("Screenshot: draw_buf %dx%d, %lu B abgelehnt\n", w, h,
                  (unsigned long)need);
    lv_label_set_text(s_pages[PAGE_SERVICE].labels[SV_SHOT],
                      tr(T_D_SHOT_FAILED));
    return;
  }
  lv_result_t rc =
      lv_snapshot_take_to_draw_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB565, &shot);
  if (rc != LV_RESULT_OK) {
    // The snapshot reshapes the buffer to the screen size plus twice the
    // screen's extra draw size (shadow, outline); every shadow in this UI is
    // explicitly 0, so the two match - this line is what tells them apart if
    // one ever appears.
    Serial.printf("Screenshot: Snapshot fehlgeschlagen (%d), Bildschirm %dx%d, "
                  "Puffer %dx%d\n",
                  (int)rc, (int)lv_obj_get_width(lv_screen_active()),
                  (int)lv_obj_get_height(lv_screen_active()), w, h);
    lv_label_set_text(s_pages[PAGE_SERVICE].labels[SV_SHOT],
                      tr(T_D_SHOT_FAILED));
    return;
  }
  const size_t px = (size_t)w * (size_t)h;
  for (size_t i = 0; i < px; i++) {
    s_shotBuf[i] = dispCorrectPixel(s_shotBuf[i]);
  }

  Serial.printf("Screenshot: %dx%d, %lu B an den SD-Worker\n", w, h,
                (unsigned long)(px * sizeof(uint16_t)));
  sdScreenshot(s_shotBuf, w, h);
  lv_label_set_text(s_pages[PAGE_SERVICE].labels[SV_SHOT],
                    tr(T_D_SHOT_WRITING));
}

static void shotTimerCb(lv_timer_t *t) {
  (void)t;
  s_shotDueMs = 0;
  takeShotNow();
}

static void shotCb(lv_event_t *e) {
  (void)e;
  AppPage &sp = s_pages[PAGE_SERVICE];
  if (s_shotDueMs != 0 || s_shotBuf != nullptr) {
    // A countdown is running, or the card worker still has the buffer from the
    // previous shot. A second capture would hand it memory it is finished with.
    setText(sp.labels[SV_SHOT], "%s", tr(T_D_SHOT_BUSY));
    return;
  }
  if (!sdMounted()) {
    setText(sp.labels[SV_SHOT], "%s", tr(T_D_SHOT_NOCARD));
    return;
  }
  s_shotDueMs = millis() + SHOT_DELAY_MS;
  setText(sp.labels[SV_SHOT], tr(T_D_SHOT_COUNT), SHOT_DELAY_MS / 1000);
  // One-shot: LVGL itself deletes the timer once its repeat count reaches 0, so
  // there is no handle to keep and nothing to free here.
  lv_timer_t *timer = lv_timer_create(shotTimerCb, SHOT_DELAY_MS, nullptr);
  lv_timer_set_repeat_count(timer, 1);
}

bool guiShotRunning() {
  // Both states count: while the countdown ticks, and afterwards while the card
  // worker still holds the image buffer of a running capture.
  return s_shotDueMs != 0 || s_shotBuf != nullptr;
}

bool guiRequestShot() {
  // The same two checks the panel's own button does - one capture at a time, and
  // no capture without a card - but without the countdown, because the caller is
  // the web interface: the browser is already showing the page that is to be
  // photographed, which is the whole reason the panel waits 5 s.
  AppPage &sp = s_pages[PAGE_SERVICE];
  if (guiShotRunning()) {
    return false;
  }
  if (!sdMounted()) {
    if (sp.labels[SV_SHOT] != nullptr) {
      setText(sp.labels[SV_SHOT], "%s", tr(T_D_SHOT_NOCARD));
    }
    return false;
  }
  takeShotNow();
  return true;
}

// The row on the Service page. A label with a background and a click flag, like
// the code row above it - not a button, because a button would bring its own
// states and the row is the same thing the other two are.
static lv_obj_t *s_themeBtn = nullptr;

// Change the theme and go over what exists. One pass over the object tree, once,
// on a tap - and not a rebuild: the pages carry state (the ring, the selected
// period, the code, the values that only change when the device answers), and a
// rebuild would throw all of that away to change two colours.
static void themeWechseln() {
  s_hell = !s_hell;
  themeSpeichern();
  // What the walk compares against is what is on screen now, which after the
  // flag has flipped is the theme we are leaving.
  if (s_hell) {
    s_altBg = COL_BG_DUNKEL;   s_neuBg = COL_BG_HELL;
    s_altText = COL_TEXT_DUNKEL; s_neuText = COL_TEXT_HELL;
  } else {
    s_altBg = COL_BG_HELL;     s_neuBg = COL_BG_DUNKEL;
    s_altText = COL_TEXT_HELL;   s_neuText = COL_TEXT_DUNKEL;
  }
  s_altOk = uiOk();
  s_neuOk = s_altOk; // one value in both modes today; see uiOk()
  themeDurchlaufen(lv_screen_active(), true);
  if (s_themeBtn != nullptr) {
    lv_label_set_text(s_themeBtn,
                      tr(s_hell ? T_D_BTN_THEME_DARK : T_D_BTN_THEME_LIGHT));
  }
  lv_obj_invalidate(lv_screen_active());
}

static void themeCb(lv_event_t *e) {
  (void)e;
  themeWechseln();
}

static void pageBuildService(AppPage *p) {
  lv_obj_t *root = p->root;

  auto sectionHead = [&](const char *text, lv_coord_t y, lv_coord_t x = 20) {
    // Same size as the page heading: on this page the section titles carry the
    // information ("was steht hier"), the values are the small print. The
    // column is a parameter because the right one is used as well - see the web
    // block further down.
    lv_obj_t *h = makeLabel(root, text, &lv_font_montserrat_16_uml, COL_MUTED);
    lv_obj_set_pos(h, x, y);
    return h;
  };

  // --- Back to the provisioning portal: compact button, top right ---
  lv_obj_t *btn = lv_button_create(root);
  lv_obj_set_pos(btn, 300, 8); // 20 px gap to the right/top edge of the page
  lv_obj_set_size(btn, 160, 34);
  lv_obj_set_style_bg_color(btn, COL_ACCENT, 0);
  lv_obj_set_style_bg_color(btn, lv_color_darken(COL_ACCENT, 40),
                            LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_hor(btn, 8, 0);
  lv_obj_add_event_cb(btn, serviceSetupCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *bl = lv_label_create(btn);
  // Symbol plus Text. Das Symbol ist ein Zeichen der Schrift und kein Wort, es
  // kann also nicht Teil des Tabelleneintrags sein - es wird hier vorgesetzt.
  // LVGL kopiert den Text in das Label, der Puffer darf ein lokaler sein.
  char blBuf[48];
  snprintf(blBuf, sizeof(blBuf), LV_SYMBOL_WIFI "%s", tr(T_D_BTN_SETUP));
  lv_label_set_text(bl, blBuf);
  lv_obj_set_style_text_font(bl, &lv_font_montserrat_16_uml, 0);
  lv_obj_set_style_text_color(bl, FLOW_WHITE, 0);
  lv_obj_center(bl);

  // --- Battery status (decoded from battery.bat_status) ---
  sectionHead(tr(T_D_SV_HEAD_BAT), 36);
  // Value deliberately one step smaller than the section title, so the decoded
  // state reads as data under a heading rather than competing with it. The raw
  // register value rides along in brackets ("Unterspannung (0x00000200)"):
  // same line, so the two cannot be read apart, and free of a line of its own.
  // Full page width, because the longest state plus the hex still fits
  // (40 characters, ~290 px) and a wrap would push everything below down.
  p->labels[SV_BAT_STATUS] =
      makeLabel(root, "--", &lv_font_montserrat_14_uml, uiText());
  lv_obj_set_pos(p->labels[SV_BAT_STATUS], 20, 58);
  lv_obj_set_width(p->labels[SV_BAT_STATUS], 440);
  // The panel's own address on the second line: it is the one thing needed to
  // open the web interface, and the top left is where the eye starts. Without a
  // network it says so - a bare dash looks like an idle reading.
  p->labels[SV_WEB] =
      makeLabel(root, "-", &lv_font_montserrat_14_uml, uiText());
  lv_obj_set_pos(p->labels[SV_WEB], 20, 88);
  lv_obj_set_width(p->labels[SV_WEB], 200);

  // --- SD history log (status only; the writer lives in storage/sdlog.cpp) ---
  // Above the faults, not below them: SD-Log is a single line, the fault list
  // runs over five, and next to each other they read as one field with two
  // headings. This order also puts the two things most often looked at ("is the
  // card working", "is something wrong") above the detail.
  (void)sectionHead(tr(T_D_SV_HEAD_SD), 116);
  p->labels[SV_SD] =
      makeLabel(root, sdStatusText(), &lv_font_montserrat_16_uml, uiText());
  lv_obj_set_pos(p->labels[SV_SD], 20, 138);
  lv_obj_set_width(p->labels[SV_SD], 440);

  // --- Faults (decoded, multi-line; several can be active at once) ---
  sectionHead(tr(T_D_SV_HEAD_FAULTS), 184);
  p->labels[SV_FLT_LIST] =
      makeLabel(root, "--", &lv_font_montserrat_14_uml, uiText());
  lv_obj_set_pos(p->labels[SV_FLT_LIST], 20, 208);
  // 280 px, not the full 440: the right column carries the web and output
  // blocks down to the bottom, and a fault text running under them is worse than
  // one that wraps. The list is capped in characters accordingly
  // (serviceFaultText).
  lv_obj_set_width(p->labels[SV_FLT_LIST], 280);
  p->labelCount = SV_LABEL_COUNT;

  // --- Screenshot to the card ---
  // On demand and nowhere automatic: the point is to see what the panel shows
  // when something is wrong, which means a person has to press it while the
  // wrong thing is on screen. Automatic shots would be the wrong direction -
  // they would only ever photograph the states we already understand.
  //
  // Stacked under the setup button and in the same colour as it: the two are
  // the only actions on this page, and they read as one group of controls
  // instead of a button that hides among the read-outs. The capture itself runs
  // SHOT_DELAY_MS later so the target page can be selected after pressing.
  lv_obj_t *shot = lv_button_create(root);
  lv_obj_set_pos(shot, 300, 48);
  lv_obj_set_size(shot, 160, 34);
  lv_obj_set_style_bg_color(shot, COL_ACCENT, 0);
  lv_obj_set_style_bg_color(shot, lv_color_darken(COL_ACCENT, 40),
                            LV_STATE_PRESSED);
  lv_obj_set_style_radius(shot, ROW_RADIUS, 0);
  lv_obj_set_style_border_width(shot, 0, 0);
  lv_obj_set_style_shadow_width(shot, 0, 0);
  lv_obj_set_style_pad_hor(shot, 8, 0);
  lv_obj_add_event_cb(shot, shotCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *sl = lv_label_create(shot);
  // LVGL 9's symbol set has no camera; IMAGE reads closer to "capture" here
  // than SAVE, which suggests the file rather than taking it.
  char slBuf[48];
  snprintf(slBuf, sizeof(slBuf), LV_SYMBOL_IMAGE "%s", tr(T_D_BTN_SHOT));
  lv_label_set_text(sl, slBuf);
  lv_obj_set_style_text_font(sl, &lv_font_montserrat_16_uml, 0);
  lv_obj_set_style_text_color(sl, FLOW_WHITE, 0);
  lv_obj_center(sl);
  // State ("Aufnahme in 3 s ...", "wird geschrieben", "auf /shot gespeichert")
  // directly under its own button, left-aligned with it. The user is on another
  // page by the time the countdown ends, so this is read after coming back.
  p->labels[SV_SHOT] =
      makeLabel(root, "", &lv_font_montserrat_14_uml, COL_MUTED);
  lv_obj_set_pos(p->labels[SV_SHOT], 300, 86);
  lv_obj_set_width(p->labels[SV_SHOT], 160);

  // --- Web interface: the code that guards its write actions ---
  // In the right column, under the two buttons. The address itself sits at the
  // top left with the battery status (that is where it is looked for); what is
  // left of this block is the code, and the code belongs next to the thing it
  // unlocks. It is not on the Info page either - that one is full at 14 rows,
  // and this is maintenance information in any case.
  //
  // The code is a value, not a setting, so it is shown as text and tappable:
  // pressing it draws a new one, which is the answer to "someone read it over
  // my shoulder" (the code changes per boot anyway, so this is a convenience
  // rather than a security measure - what it really protects is a network
  // neighbour who guessed the address).
  //
  // Drawn as a button-like row: filled in the bar colour, in the accent colour
  // while pressed. The obvious earlier attempt - a light chip on this dark
  // page - had to be undone: light text on a near-white field is white on
  // white, so the code was there and could not be read. The pressed colour is
  // there for the same reason: a tap that changes nothing visible reads as a
  // field that does not work.
  (void)sectionHead(tr(T_D_SV_HEAD_WEB), 118, 300);
  p->labels[SV_CODE] = makeLabel(root, tr(T_D_CODE_EMPTY),
                                 &lv_font_montserrat_14_uml, uiText());
  lv_obj_set_pos(p->labels[SV_CODE], 300, 140);
  lv_obj_set_width(p->labels[SV_CODE], 160);
  lv_obj_set_style_text_align(p->labels[SV_CODE], LV_TEXT_ALIGN_CENTER, 0);
  // Vertical padding instead of a fixed height: the box grows with the text and
  // is 26 px tall either way.
  lv_obj_set_style_pad_ver(p->labels[SV_CODE], 4, 0);
  lv_obj_set_style_bg_color(p->labels[SV_CODE], COL_BAR, 0);
  lv_obj_set_style_bg_opa(p->labels[SV_CODE], LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(p->labels[SV_CODE], COL_ACCENT, LV_STATE_PRESSED);
  lv_obj_set_style_radius(p->labels[SV_CODE], ROW_RADIUS, 0);
  lv_obj_add_flag(p->labels[SV_CODE], LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(p->labels[SV_CODE], webCodeNewCb, LV_EVENT_CLICKED,
                      nullptr);
  lv_obj_t *hint = makeLabel(root, tr(T_D_CODE_HINT),
                             &lv_font_montserrat_14_uml, COL_MUTED);
  lv_obj_set_pos(hint, 300, 172);

  // --- Switched output ("Ausgang") ---
  // The function it follows is a setting, but the setting that is changed most
  // often is "which of these do I actually want" - so it is a tap here rather
  // than a form in the web interface. Both are available; the tap is the quick
  // one, the web page is where the threshold in watts is entered.
  //
  // The whole block sits in the right column under the code, so the page reads
  // as two columns that each begin with their heading instead of one full-width
  // row that runs down into the navigation bar. Function and threshold share one
  // tappable row for the same reason they sit side by side on the web page:
  // the threshold belongs to the function.
  (void)sectionHead(tr(T_D_SV_HEAD_OUTPUT), 198, 300);
  // "Aus" ist nur, was die Zeile vor dem ersten Durchlauf zeigt, in dem die
  // gespeicherte Funktion gelesen wurde; danach traegt sie die Funktion selbst.
  p->labels[SV_RELAY] = makeLabel(root, relayModeName(RelayMode::Off),
                                   &lv_font_montserrat_14_uml, uiText());
  lv_obj_set_pos(p->labels[SV_RELAY], 300, 220);
  lv_obj_set_width(p->labels[SV_RELAY], 160);
  lv_obj_set_style_text_align(p->labels[SV_RELAY], LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_ver(p->labels[SV_RELAY], 4, 0);
  lv_obj_set_style_bg_color(p->labels[SV_RELAY], COL_BAR, 0);
  lv_obj_set_style_bg_opa(p->labels[SV_RELAY], LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(p->labels[SV_RELAY], COL_ACCENT, LV_STATE_PRESSED);
  lv_obj_set_style_radius(p->labels[SV_RELAY], ROW_RADIUS, 0);
  lv_obj_add_flag(p->labels[SV_RELAY], LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(p->labels[SV_RELAY], relayModeCb, LV_EVENT_CLICKED,
                      nullptr);
  // What the output is doing, and the value it compares against its threshold -
  // without that number a threshold in watts is a number nobody can set sensibly.
  p->labels[SV_RELAY_ST] =
      makeLabel(root, "", &lv_font_montserrat_14_uml, COL_MUTED);
  lv_obj_set_pos(p->labels[SV_RELAY_ST], 300, 257);
  lv_obj_set_width(p->labels[SV_RELAY_ST], 160);

  // Test button, on its own line under the state it overrules. 5 s on, 5 s off,
  // twice: long enough to hear or see, short enough not to leave a load running
  // if nobody is watching. It ignores the rule, which is the point - the rule
  // needs data from the inverter, the test must work without it.
  //
  // 26 px tall. Its position is not fixed here: serviceRelayLayout() puts it
  // under the state line, whose place depends on the height of the function row
  // above it. It ends at 304 with the usual one-line name, and the content area
  // reaches 364 - so the theme row below it keeps its air, and a two-line name
  // still ends 4 px above it.
  s_testBtn = lv_button_create(root);
  lv_obj_t *test = s_testBtn;
  lv_obj_set_pos(test, 300, 278);
  lv_obj_set_size(test, 160, 26);
  lv_obj_set_style_bg_color(test, COL_BAR, 0);
  lv_obj_set_style_bg_color(test, COL_ACCENT, LV_STATE_PRESSED);
  lv_obj_set_style_radius(test, ROW_RADIUS, 0);
  lv_obj_set_style_border_width(test, 0, 0);
  lv_obj_set_style_shadow_width(test, 0, 0);
  lv_obj_set_style_pad_hor(test, 8, 0);
  lv_obj_add_event_cb(test, relayTestCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *tl = lv_label_create(test);
  lv_label_set_text(tl, tr(T_D_BTN_TEST));
  lv_obj_set_style_text_font(tl, &lv_font_montserrat_14_uml, 0);
  lv_obj_set_style_text_color(tl, FLOW_WHITE, 0);
  lv_obj_center(tl);

  // --- Background: dark or light ---
  // Under the output block, at the right, because it is the one setting on this
  // page that belongs to the panel itself and not to the device: everything
  // above it is either read from the inverter or stored as a setting of the
  // output. It says the theme it switches TO, like the code row above says what
  // tapping it gets you ("ein neuer Code"), so the wording is the action.
  //
  // Its own text stays white in both themes - the row has a background, and the
  // theme walk leaves everything with a background alone on purpose.
  s_themeBtn =
      makeLabel(root, tr(s_hell ? T_D_BTN_THEME_DARK : T_D_BTN_THEME_LIGHT),
                &lv_font_montserrat_14_uml, FLOW_WHITE);
  lv_obj_set_pos(s_themeBtn, 300, 326);
  lv_obj_set_width(s_themeBtn, 160);
  lv_obj_set_style_text_align(s_themeBtn, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_ver(s_themeBtn, 4, 0);
  lv_obj_set_style_bg_color(s_themeBtn, COL_BAR, 0);
  lv_obj_set_style_bg_opa(s_themeBtn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_themeBtn, COL_ACCENT, LV_STATE_PRESSED);
  lv_obj_set_style_radius(s_themeBtn, ROW_RADIUS, 0);
  lv_obj_add_flag(s_themeBtn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_themeBtn, themeCb, LV_EVENT_CLICKED, nullptr);

  // Once here so the first frame is already right, and then on every 1 s tick
  // that rewrites the function row (see serviceRelayLayout).
  serviceRelayLayout();
}

// Format one scale marker value: "0" or kW with comma decimal ("2,5",
// "-0,5"). The kW unit comes from the legend, so the numbers stay short.
static void setScaleVal(lv_obj_t *l, float v) {
  if (v == 0.0f) {
    lv_label_set_text(l, "0");
    return;
  }
  char b[16];
  fmtNumComma(b, sizeof(b), "%.1f", (double)(v / 1000.0f));
  lv_label_set_text(l, b);
}

// Move the min / 0 / max markers to the power-axis positions of loW / 0 / hiW
// over the left end of the plot area. When loW == 0 the min marker coincides
// with the zero one and stays hidden. The SOC series needs no markers: its fixed
// 0..100 axis spans the full chart height by construction.
static void applyScaleMarkers(float loW, float hiW) {
  if (s_scaleMax == nullptr) return;
  const int plotTop = kHistChartY + kHistChartPad;
  const int plotBot = kHistChartY + kHistChartH - kHistChartPad;
  const int plotH = plotBot - plotTop;
  const float span = hiW - loW;
  auto yOf = [plotBot, plotH, span, loW](float w) -> int {
    return plotBot - (int)lroundf((w - loW) / span * (float)plotH);
  };
  const int yMax = yOf(hiW), yZero = yOf(0.0f), yMin = yOf(loW);
  // Inside the card, two pixels left of the plot area's edge: the digits cover
  // the first few pixels of every series, so the gap they cut out of the lines
  // sits as far left as it goes without clipping at the card border.
  const int labelX = kHistChartX + kHistChartPad - 2;
  // 14 px font, ~18 px line box plus 2 px padding per side. Both outer labels
  // ride above their line: the top one would be clipped by the card edge, the
  // bottom one by the plot edge, and a label hanging over a grid line reads
  // better than one centred on it.
  auto place = [&](lv_obj_t *l, int yv, float v, int dy) {
    setScaleVal(l, v);
    lv_obj_set_pos(l, labelX, yv - 9 + dy);
  };
  place(s_scaleMax, yMax, hiW, -2);
  place(s_scaleZero, yZero, 0.0f, 0);
  if (loW < 0.0f) {
    place(s_scaleMin, yMin, loW, -5);
  } else {
    lv_obj_add_flag(s_scaleMin, LV_OBJ_FLAG_HIDDEN);
  }
}

// Recompute the chart Y range from the stored history ring (kW = W / 1000 on
// the axis is implied by the legend; the range itself stays in W). Runs on
// every new sample and after the SD seed, so the axis grows the moment a new
// peak arrives and shrinks again once that peak leaves the 24 h window. A
// small air margin keeps the extremes off the exact plot edges.
static void updateChartRange() {
  float loW = 0.0f, hiW = 0.0f;
  bool first = true;
  int start = (s_histNext - s_histCount + HIST_POINTS) % HIST_POINTS;
  for (int p = 0; p < s_histCount; p++) {
    const int slot = (start + p) % HIST_POINTS;
    if (!s_histOk[slot]) continue; // gap marker, nothing was measured
    const float *row = &s_hist[slot * HIST_SERIES];
    for (int i = 0; i < HIST_SERIES - 1; i++) { // SOC: own 0..100 axis
      float v = row[i];
      if (first) {
        loW = hiW = v;
        first = false;
      } else if (v < loW) {
        loW = v;
      } else if (v > hiW) {
        hiW = v;
      }
    }
  }
  // Always keep the zero line visible.
  loW = loW < 0.0f ? loW : 0.0f;
  hiW = hiW > 0.0f ? hiW : 0.0f;

  float span = hiW - loW;
  float step = span < 2000.0f  ? 250.0f
               : span < 4000.0f  ? 500.0f
               : span < 10000.0f ? 1000.0f
                                 : 2000.0f;
  // Air margin above/below the data so the max line never touches the exact
  // top edge; 5 % of the span, at least one step.
  float margin = span > 0.0f ? span * 0.05f : step;
  loW = floorf((loW - margin) / step) * step;
  hiW = ceilf((hiW + margin) / step) * step;
  if (hiW - loW < step) hiW = loW + step;

  lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)loW,
                          (int32_t)hiW);
  applyScaleMarkers(loW, hiW);
}

// Graph page: all power values of the last 24 hours (legend above, chart
// below). One sample lands every 5 minutes in refreshCb.
static void pageBuildGraph(AppPage *p) {
  lv_obj_t *root = p->root;

  // Legend: small color dot + series name, packed left to right. The names
  // differ a lot in width ("Verbrauch" is 77 px, "PV" only 20), so a fixed 90 px
  // pitch ran "Verbrauch" into the next dot by a pixel. A cursor keeps the
  // entries apart for whatever the names are.
  int lx = 20;
  for (int i = 0; i < HIST_SERIES; i++) {
    lv_obj_t *dot = lv_obj_create(root);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_pos(dot, lx, 32);
    lv_obj_set_style_bg_color(dot, lv_color_hex(kHistColor[i]), 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_shadow_width(dot, 0, 0);
    lv_obj_t *nm =
        makeLabel(root, tr(kHistId[i]), &lv_font_montserrat_14_uml, uiText());
    lv_obj_set_pos(nm, lx + 14, 29);
    lv_obj_update_layout(nm);
    lx += 14 + lv_obj_get_width(nm) + LEGEND_GAP;
  }

  // Chart. Points are seeded with LV_CHART_POINT_NONE so nothing is drawn
  // until real 5-minute samples arrive (no fake zero history after boot).
  s_chart = lv_chart_create(root);
  lv_obj_set_pos(s_chart, kHistChartX, kHistChartY);
  // kHistChartY + kHistChartH = 332, and the gap summary sits directly under
  // the chart at 336..~354 so it stays inside CONTENT_H (364) - no scrolling
  // to read it.
  lv_obj_set_size(s_chart, kHistChartW, kHistChartH);
  lv_obj_set_style_bg_color(s_chart, COL_CARD, 0);
  lv_obj_set_style_radius(s_chart, 10, 0);
  lv_obj_set_style_border_width(s_chart, 1, 0);
  lv_obj_set_style_border_color(s_chart, COL_BORDER, 0);
  lv_obj_set_style_pad_all(s_chart, 10, 0);
  lv_obj_set_style_line_width(s_chart, 1, LV_PART_MAIN); // divider grid
  lv_obj_set_style_line_color(s_chart, COL_BORDER, LV_PART_MAIN);
  lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS); // series stroke
  // No point markers (dots only draw when the indicator size is nonzero).
  lv_obj_set_style_width(s_chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_height(s_chart, 0, LV_PART_INDICATOR);

  lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(s_chart, HIST_POINTS);
  lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_div_line_count(s_chart, 4, 5);
  lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 3000);
  // SOC is a percent value: it gets its own axis so 0 % maps to the bottom and
  // 100 % to the top of the chart, independent of the power autoscale.
  lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_SECONDARY_Y, 0, 100);
  for (int i = 0; i < HIST_SERIES; i++) {
    s_chartSer[i] = lv_chart_add_series(
        s_chart, lv_color_hex(kHistColor[i]),
        i == HIST_SERIES - 1 ? LV_CHART_AXIS_SECONDARY_Y
                             : LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_values(s_chart, s_chartSer[i], LV_CHART_POINT_NONE);
  }

  // --- Scale markers, inside the card ---
  // Min / 0 / max of the power axis, drawn over the left end of the plot area
  // instead of in a gutter beside the card: the chart is 480 px wide and the
  // gutter cost 48 px of plot. Each label carries the card colour as its own
  // background, so a series crossing the left edge runs behind the digits
  // rather than through them. Their positions and values are refreshed by
  // updateChartRange(); they are only meaningful once a range was computed, so
  // the initial text stays empty.
  lv_obj_t *scaleLabels[3] = {nullptr, nullptr, nullptr};
  scaleLabels[0] = s_scaleMax =
      makeLabel(root, "", &lv_font_montserrat_14_uml, COL_MUTED);
  scaleLabels[1] = s_scaleZero =
      makeLabel(root, "", &lv_font_montserrat_14_uml, COL_MUTED);
  scaleLabels[2] = s_scaleMin =
      makeLabel(root, "", &lv_font_montserrat_14_uml, COL_MUTED);
  for (int i = 0; i < 3; i++) {
    lv_obj_set_width(scaleLabels[i], 52);
    lv_obj_set_style_text_align(scaleLabels[i], LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_bg_color(scaleLabels[i], COL_CARD, 0);
    lv_obj_set_style_bg_opa(scaleLabels[i], LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scaleLabels[i], 2, 0);
    lv_obj_set_style_radius(scaleLabels[i], 3, 0);
  }

  // --- Gap summary ---
  // The chart itself shows where the recording broke (the stroke is interrupted).
  // This line says how much is missing, because a break in a line chart is easy
  // to read as "the value dipped" if nobody states that no value was measured.
  p->labels[GH_GAPS] =
      makeLabel(root, "", &lv_font_montserrat_14_uml, COL_MUTED);
  lv_obj_set_pos(p->labels[GH_GAPS], 20, 336); // 4 px under the chart
  lv_obj_set_width(p->labels[GH_GAPS], 440);

  p->labelCount = GH_LABEL_COUNT;
}

// ---------------------------------------------------------------------------
// Page switching
// ---------------------------------------------------------------------------
static void showPage(int idx) {
  if (idx < 0) idx += PAGE_COUNT;
  idx %= PAGE_COUNT;
  s_page = idx;
  for (int i = 0; i < PAGE_COUNT; i++) {
    if (s_pages[i].root) {
      if (i == s_page) {
        lv_obj_remove_flag(s_pages[i].root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_pages[i].root);
      } else {
        lv_obj_add_flag(s_pages[i].root, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
}

static void navPrevCb(lv_event_t *e) {
  (void)e;
  showPage(s_page - 1);
}
static void navHomeCb(lv_event_t *e) {
  (void)e;
  showPage(PAGE_OVERVIEW);
}
static void navNextCb(lv_event_t *e) {
  (void)e;
  showPage(s_page + 1);
}

// ---------------------------------------------------------------------------
// Wi-Fi setup overlay: full-screen QR code (WIFI: scheme) + SSID text. Shown
// while the provisioning AP is running; a 10 s boot test window shows it even
// without an active AP so the layout can be verified on the panel.
// ---------------------------------------------------------------------------
static void buildApOverlay() {
  lv_obj_t *scr = lv_screen_active();
  s_apOverlay = lv_obj_create(scr);
  lv_obj_set_size(s_apOverlay, 480, 480);
  lv_obj_set_pos(s_apOverlay, 0, 0);
  lv_obj_set_style_bg_color(s_apOverlay, uiBg(), 0);
  lv_obj_set_style_radius(s_apOverlay, 0, 0);
  lv_obj_set_style_border_width(s_apOverlay, 0, 0);
  lv_obj_set_style_pad_all(s_apOverlay, 0, 0);
  lv_obj_set_layout(s_apOverlay, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_apOverlay, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_apOverlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_apOverlay, 12, 0);

  lv_obj_t *title = lv_label_create(s_apOverlay);
  lv_label_set_text(title, tr(T_D_AP_TITLE));
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20_uml, 0);
  lv_obj_set_style_text_color(title, uiText(), 0);

  // LVGL 9 QR widget (MIT, qrcodegen inside LVGL): 220 px canvas, white
  // background with black modules so it scans cleanly off the dark panel.
  s_apQr = lv_qrcode_create(s_apOverlay);
  lv_qrcode_set_size(s_apQr, 220);
  lv_qrcode_set_dark_color(s_apQr, lv_color_hex(0x000000));
  lv_qrcode_set_light_color(s_apQr, FLOW_WHITE);

  s_apSsid = lv_label_create(s_apOverlay);
  lv_obj_set_style_text_font(s_apSsid, &lv_font_montserrat_20_uml, 0);
  lv_obj_set_style_text_color(s_apSsid, FLOW_WHITE, 0);

  lv_obj_t *hint = lv_label_create(s_apOverlay);
  lv_label_set_text(hint, tr(T_D_AP_HINT));
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_14_uml, 0);
  lv_obj_set_style_text_color(hint, COL_MUTED, 0);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_add_flag(s_apOverlay, LV_OBJ_FLAG_HIDDEN);
}

// (Re)fill the QR + SSID text. The provisioning AP is open (no password), so
// the QR uses the WIFI: scheme's nopass type that phones scan natively.
static void updateApQr() {
  const char *ssid = provisioningApSsid();
  lv_label_set_text(s_apSsid, ssid);
  char uri[128];
  if (provisioningApOpen()) {
    snprintf(uri, sizeof(uri), "WIFI:T:nopass;S:%s;;", ssid);
  } else {
    // Reserved for a password-protected AP (P:<pass> would be added here).
    snprintf(uri, sizeof(uri), "WIFI:T:WPA;S:%s;;", ssid);
  }
  lv_qrcode_update(s_apQr, uri, (uint32_t)strlen(uri));
}

// ---------------------------------------------------------------------------
// 1 Hz data refresh
// ---------------------------------------------------------------------------
// Marks the touch read; see the wrapper installed in guiSetup(). The I2C read is
// the only blocking call inside lv_timer_handler() that has no phase marker of
// its own, and it runs between refreshCb and the first flush - exactly the place
// where a freeze reported as "gui.end" would otherwise hide.
static void touchReadMarked(lv_indev_t *indev, lv_indev_data_t *data) {
  diagPhase("touch.read");
  touchReadCb(indev, data);
  diagPhase("touch.done");
}

static void refreshCb(lv_timer_t *t) {
  (void)t;
  const RctSnapshot &s = rctState;
  diagPhase("gui.refresh");

  // Wi-Fi setup overlay: visible while the provisioning AP runs, or during
  // the boot test window (first 10 s) so the QR layout can be verified.
  if (s_apOverlay) {
    bool show = provisioningApActive() ||
                (int32_t)(millis() - s_apTestUntil) < 0;
    bool visible = !lv_obj_has_flag(s_apOverlay, LV_OBJ_FLAG_HIDDEN);
    if (show && !visible) {
      updateApQr();
      lv_obj_remove_flag(s_apOverlay, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(s_apOverlay);
    } else if (!show && visible) {
      lv_obj_add_flag(s_apOverlay, LV_OBJ_FLAG_HIDDEN);
    }
  }

  diagPhase("gui.badge");
  // Status badge. Four distinct states, because "still associating" and "the
  // data supplier is unreachable" are different problems and must not share a
  // colour: the connect phase can take up to CONNECT_BUDGET_MS and would
  // otherwise flash a red "no data" for a full minute.
  // Which of the five states applies is decided in DataStatus.h, because the
  // order of the cases is the logic: a device that never answered stays red
  // ("keine Daten") instead of being softened to a yellow "wartet", and the
  // waiting state only appears with a link that is up and data that is merely
  // old. tools/badge_test checks that order; the texts and colours are here.
  const char *badge;
  lv_color_t badgeCol;
  switch (dataStatus(networkConnecting(), s.haveData, s.connected,
                     dataAgeMs(millis(), s.lastUpdateMs))) {
  case DataStatus::Connecting:
    badge = tr(T_D_BADGE_CONNECTING);
    badgeCol = COL_WARN;
    break;
  case DataStatus::NoData:
    badge = tr(T_D_BADGE_NODATA); // link is up, but no RCT frame arrives
    badgeCol = COL_ERR;
    break;
  case DataStatus::Reconnect:
    badge = tr(T_D_BADGE_RECONNECT); // data was there, then the stream stopped
    badgeCol = COL_WARN;
    break;
  case DataStatus::Waiting:
    // Link up, but nothing new for over a minute. The values on the pages are
    // the last ones that arrived - which the switching output says so about
    // itself below.
    badge = tr(T_D_BADGE_WAITING);
    badgeCol = COL_WARN;
    break;
  default:
    badge = tr(T_D_BADGE_LIVE);
    badgeCol = uiOk();
    break;
  }
  lv_label_set_text(s_statusLabel, badge);
  lv_obj_set_style_text_color(s_statusLabel, badgeCol, 0);

  AppPage &ov = s_pages[PAGE_OVERVIEW];
  if (ov.labels[OV_GRID_VAL]) {
    // Derived quantities (all W). The sign conventions below were read off the
    // real device, not taken from the portal comments, which have them the
    // other way round. Measured at 21:18 with the real inverter:
    //
    //   PV 0 W | Haus 832 W | Netz +4 W | Batterie +810 W
    //
    // With no production at all the battery cannot be charging, and 810 + 4
    // balances the 832 W the house draws: positive is therefore the battery
    // *discharging* into the house. The day counters agree independently -
    // Einspeisung reads -20,1 kWh, i.e. the feed counter is negative, and
    // Bezug reads 0,0 kWh while the log shows the same night-time import.
    //
    //   grid  = p_ac_grid_sum_lp   + = Bezug (import), - = Einspeisung
    //   pv    = p_dc_lp[0]+[1]+S0  >= 0, production
    //   bat   = p_acc_lp           + = discharging, - = charging
    //   house = p_ac_load sum + S0 total household consumption, >= 0
    //
    // (The inverter's load meter reads demand already minus the S0 generator,
    // so the correction adds the external power back - see the S0 handling in
    // the Verlauf sampler, which follows the same rule.)
    //
    // Every consumer below derives from these two, so the direction appears in
    // exactly one place per view.
    const float gridActive = 50.0f; // W, below = Standby
    const float pvActive = 20.0f;   // W, below = no visible generation
    const float batActive = 50.0f;  // W, below = Standby
    const char *dash = "--";
    float pTot = s.gridPowerSum;
    float pvTotal = s.pvPower[0] + s.pvPower[1] + s.s0Power;
    float pBat = s.batteryPower;
    float house = s.loadPower[0] + s.loadPower[1] + s.loadPower[2] + s.s0Power;
    bool has = s.haveData;

    // --- Grid ---
    // Island mode (grid outage): warning triangle on the connector. The flag
    // arrives with the 10 s device group, so the warning stays hidden until the
    // device has answered once - a hidden warning is the safe default.
    //
    // + = Bezug, see the sign conventions above.
    //
    // Node values (Netz/PV/Batterie) share one rule, per the panel's spec: a
    // flow at or below stand-by reads the same as none - "--" in white - and
    // only a real flow gets the red number. The connector lines keep their own
    // stand-by colour regardless.
    const bool gridImport = pTot > 0.0f;
    if (s.islandMode && s.islandKnown) {
      lv_obj_remove_flag(ov.labels[OV_ISLAND], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ov.labels[OV_ISLAND], LV_OBJ_FLAG_HIDDEN);
    }
    if (has) {
      float absK = fabsf(pTot) / 1000.0f;
      bool active = fabsf(pTot) >= gridActive;
      if (active) {
        setText(ov.labels[OV_GRID_VAL], "%.2f kW", absK);
        lv_obj_set_style_text_color(ov.labels[OV_GRID_VAL], FLOW_RED, 0);
      } else {
        lv_label_set_text(ov.labels[OV_GRID_VAL], dash);
        lv_obj_set_style_text_color(ov.labels[OV_GRID_VAL], FLOW_WHITE, 0);
      }
      lv_obj_set_style_line_color(s_lineGrid, active ? FLOW_RED : FLOW_LINE, 0);
      lv_obj_set_style_line_width(s_lineGrid, active ? 4 : 3, 0);
      if (active) {
        // import: grid -> haus (arrow points left), export: haus -> grid
        lv_label_set_text(ov.labels[OV_GRID_ARROW],
                          gridImport ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT);
        lv_obj_remove_flag(ov.labels[OV_GRID_ARROW], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(ov.labels[OV_GRID_ARROW], LV_OBJ_FLAG_HIDDEN);
      }
    } else {
      lv_label_set_text(ov.labels[OV_GRID_VAL], dash);
      lv_obj_set_style_text_color(ov.labels[OV_GRID_VAL], FLOW_WHITE, 0);
      lv_obj_set_style_line_color(s_lineGrid, FLOW_LINE, 0);
      lv_obj_set_style_line_width(s_lineGrid, 3, 0);
      lv_obj_add_flag(ov.labels[OV_GRID_ARROW], LV_OBJ_FLAG_HIDDEN);
    }

    // --- PV (panel -> haus) ---
    if (has && pvTotal >= pvActive) {
      setText(ov.labels[OV_PV_VAL], "%.2f kW", pvTotal / 1000.0f);
      lv_obj_set_style_text_color(ov.labels[OV_PV_VAL], FLOW_RED, 0);
      lv_obj_set_style_line_color(s_linePv, FLOW_RED, 0);
      lv_obj_set_style_line_width(s_linePv, 4, 0);
      lv_label_set_text(ov.labels[OV_PV_ARROW], LV_SYMBOL_RIGHT);
      lv_obj_remove_flag(ov.labels[OV_PV_ARROW], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_label_set_text(ov.labels[OV_PV_VAL], dash);
      lv_obj_set_style_text_color(ov.labels[OV_PV_VAL], FLOW_WHITE, 0);
      lv_obj_set_style_line_color(s_linePv, FLOW_LINE, 0);
      lv_obj_set_style_line_width(s_linePv, 2, 0);
      lv_obj_add_flag(ov.labels[OV_PV_ARROW], LV_OBJ_FLAG_HIDDEN);
    }

    // --- Battery (haus <-> batterie) ---
    if (s.haveBattery) {
      setText(ov.labels[OV_BAT_SOC], "%.0f %%", s.batterySoc);
      bool active = has && fabsf(pBat) >= batActive;
      if (active) {
        float absK = fabsf(pBat) / 1000.0f;
        setText(ov.labels[OV_BAT_VAL], "%.2f kW", absK);
        lv_obj_set_style_text_color(ov.labels[OV_BAT_VAL], FLOW_RED, 0);
        lv_obj_set_style_line_color(s_lineBat, FLOW_RED, 0);
        lv_obj_set_style_line_width(s_lineBat, 4, 0);
        // pBat > 0 = discharging (measured), so the arrow points up into the
        // house; charging (pBat < 0) draws down into the battery.
        lv_label_set_text(ov.labels[OV_BAT_ARROW],
                          pBat > 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
        lv_obj_remove_flag(ov.labels[OV_BAT_ARROW], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_label_set_text(ov.labels[OV_BAT_VAL], dash);
        lv_obj_set_style_text_color(ov.labels[OV_BAT_VAL], FLOW_WHITE, 0);
        lv_obj_set_style_line_color(s_lineBat, FLOW_LINE, 0);
        lv_obj_set_style_line_width(s_lineBat, 2, 0);
        lv_obj_add_flag(ov.labels[OV_BAT_ARROW], LV_OBJ_FLAG_HIDDEN);
      }
    } else {
      lv_label_set_text(ov.labels[OV_BAT_VAL], dash);
      lv_obj_set_style_text_color(ov.labels[OV_BAT_VAL], FLOW_WHITE, 0);
      lv_obj_set_style_line_color(s_lineBat, FLOW_LINE, 0);
      lv_obj_set_style_line_width(s_lineBat, 2, 0);
      lv_obj_add_flag(ov.labels[OV_BAT_ARROW], LV_OBJ_FLAG_HIDDEN);
    }

    // --- Haus (total household demand: Power Sensor + S0 generator) ---
    if (has && house >= 5.0f) {
      setText(ov.labels[OV_HOUSE_VAL], "%.2f kW", house / 1000.0f);
      lv_obj_set_style_text_color(ov.labels[OV_HOUSE_VAL], FLOW_RED, 0);
    } else {
      lv_label_set_text(ov.labels[OV_HOUSE_VAL], has ? "0.00 kW" : dash);
      lv_obj_set_style_text_color(ov.labels[OV_HOUSE_VAL], FLOW_LINE, 0);
    }

    // --- Status table (2x2, tendency words like the portal, all white) ---
    if (has) {
      bool gridFlowing = fabsf(pTot) >= gridActive;
      bool batFlowing = fabsf(pBat) >= batActive;

      if (pvTotal >= pvActive) {
        setText(ov.labels[OV_T_ERZ], "%s", tr(T_D_TEND_PRODUCING));
      } else {
        setText(ov.labels[OV_T_ERZ], "%s", tr(T_D_TEND_NONE));
      }

      // Verbrauch says where the household power comes from: "Netzstrom" as
      // soon as the grid is importing, otherwise the house runs on its own
      // (PV and/or battery), which is what "Unabhängig" names.
      if (gridImport && fabsf(pTot) >= gridActive) {
        setText(ov.labels[OV_T_VERB], "%s", tr(T_D_TEND_MAINS));
      } else {
        setText(ov.labels[OV_T_VERB], "%s", tr(T_D_TEND_SELF));
      }

      if (gridFlowing) {
        setText(ov.labels[OV_T_NETZ], "%s",
                tr(gridImport ? T_D_TEND_IMPORT : T_D_TEND_EXPORT));
      } else {
        setText(ov.labels[OV_T_NETZ], "%s", tr(T_D_TEND_SELF));
      }

      if (s.haveBattery) {
        if (batFlowing) {
          setText(ov.labels[OV_T_BAT], "%s",
                  tr(pBat > 0 ? T_D_TEND_DISCHARGE : T_D_TEND_CHARGE));
        } else {
          setText(ov.labels[OV_T_BAT], "%s", tr(T_D_TEND_STANDBY));
        }
      } else {
        setText(ov.labels[OV_T_BAT], "%s", tr(T_D_TEND_NOBAT));
      }
    } else {
      setText(ov.labels[OV_T_ERZ], dash);
      setText(ov.labels[OV_T_VERB], dash);
      setText(ov.labels[OV_T_NETZ], dash);
      setText(ov.labels[OV_T_BAT], dash);
    }
  }

  AppPage &eb = s_pages[PAGE_ENERGY];
  if (eb.labels[EB_VAL_PV]) {
    float v[ENERGY_ROWS];
    energyPeriodValues(s, s_energyPeriod, v);
    float maxV = 0.0f;
    for (int i = 0; i < ENERGY_ROWS; i++) {
      if (v[i] > maxV) {
        maxV = v[i];
      }
    }
    for (int i = 0; i < ENERGY_ROWS; i++) {
      // Bars share the scale of the largest value of the period; a small but
      // non-zero value still gets a visible stub.
      int w = 0;
      if (maxV > 0.0f) {
        w = (int)(v[i] / maxV * (float)EB_BAR_W);
        if (w == 0 && v[i] > 0.0f) {
          w = 3;
        }
      }
      if (s_ebarFill[i] && lv_obj_get_width(s_ebarFill[i]) != w) {
        lv_obj_set_width(s_ebarFill[i], w);
      }
      if (s.haveData) {
        setEnergyValue(eb.labels[i], v[i]);
      } else {
        lv_label_set_text(eb.labels[i], "--");
      }
    }
  }

  diagPhase("gui.energy");
  AppPage &en = s_pages[PAGE_HEUTE];
  if (en.labels[EN_GEN_VAL]) {
    // Portal "Heute" day counters (all Wh). Eigenverbrauch = Erzeugt minus
    // Eingespeist, wie auf der Energie-Seite: alles, was erzeugt und nicht
    // eingespeist wurde, ist im eigenen Haus genutzt worden - auch der Teil, der
    // als Akkuladung auf Vorrat liegt. Der Akku versorgt ausschliesslich das
    // eigene Haus und wird nie zur Einspeisung benutzt, deshalb zaehlt die
    // Ladung mit; ihre Entnahme erscheint in einem anderen Zeitraum als
    // Verbrauch, und genau deshalb ist hier nichts doppelt gezaehlt. NICHT
    // Verbrauch minus Bezug: das laesst die Akkuladung ausser vor und waere an
    // einem Tag mit Ladebetrieb zu niedrig (live geprueft: nachts 569 Wh Last
    // bei 0,1 Wh Bezug = 569 Wh aus dem Akku).
    float gen = s.dayPvWh + s.dayExtWh; // Erzeugt: PV-DC plus externer Generator
    float feed = s.dayFeedInWh;     // Eingespeist, kommt negativ vom Geraet
    if (feed < 0.0f) feed = -feed;  // Betrag, nicht Vorzeichen
    float consumed = s.dayLoadWh + s.dayExtWh; // Verbrauch: Last plus extern
    float gridIn = s.dayGridLoadWh;     // Bezug (grid draw)
    float selfUse = gen - feed;         // Eigenverbrauch, nie negativ
    if (selfUse < 0.0f) selfUse = 0.0f; // Zaehler kurz nach Geraete-Neustart versetzt
    // Autarkie = 1 - Bezug / Verbrauch. No load consumes nothing from the
    // grid, so the day is fully independent.
    float autarkie =
        consumed > 0.0f ? (1.0f - gridIn / consumed) * 100.0f : 100.0f;
    if (autarkie < 0.0f) autarkie = 0.0f;
    // Eigenverbrauchsquote = Eigenverbrauch / Erzeugung, begrenzt auf
    // [0, 100]: mit Erzeugung minus Einspeisung ist das der Anteil der
    // erzeugten Energie, der im eigenen Haus genutzt wurde, also
    // 1 - Einspeisung/Erzeugung. Nach einem Geraete-Neustart laufen die
    // Zaehler kurz phasenversetzt (daher die Klammer oben).
    float evb = gen > 0.0f ? selfUse / gen * 100.0f : 0.0f;
    if (evb > 100.0f) evb = 100.0f;

    if (s.haveData) {
      // Portal "Übersicht" boxes: Erzeugt / Eigenverbrauch / Eingespeist
      // rounded to one decimal, the Verbrauch/Bezug counters to two.
      setText(en.labels[EN_GEN_VAL], "%.1f kWh", gen / 1000.0f);
      setText(en.labels[EN_SELF_VAL], "%.1f kWh", selfUse / 1000.0f);
      setText(en.labels[EN_FEED_VAL], "%.1f kWh", feed / 1000.0f);
      setText(en.labels[EN_VERB_VAL], "%.2f kWh", consumed / 1000.0f);
      setText(en.labels[EN_BEZU_VAL], "%.2f kWh", gridIn / 1000.0f);
      setText(en.labels[EN_AUT_VAL], "%.0f %%", autarkie);
      setText(en.labels[EN_EVB_VAL], "%.0f %%", evb);
    } else {
      // Before the first frame of data every value shows "--".
      for (int i = EN_GEN_VAL; i <= EN_EVB_VAL; i += 2) {
        lv_label_set_text(en.labels[i], "--");
      }
    }
  }

  diagPhase("gui.info");
  AppPage &inf = s_pages[PAGE_INFO];
  if (inf.labels[INF_NAME]) {
    // Rows 1+2: device identity, so the page opens with what it is.
    const char *dash = "--";
    setText(inf.labels[INF_NAME], "%s",
            s.deviceName[0] ? s.deviceName : dash);
    setText(inf.labels[INF_SW], "%s",
            s.firmwareVersion[0] ? s.firmwareVersion : dash);
    setText(inf.labels[INF_HOST], "%s", rct_host);
    setText(inf.labels[INF_PORT], "%s", rct_port);
    setText(inf.labels[INF_LINK], "%s",
            s.connected ? tr(T_D_LINK_UP) : tr(T_D_LINK_DOWN));
    setText(inf.labels[INF_LAST], tr(T_D_LASTDATA_AGO),
            s.lastUpdateMs ? (millis() - s.lastUpdateMs) / 1000 : 0UL);
    setText(inf.labels[INF_UPTIME], "%lu s", millis() / 1000);
    setNum(inf.labels[INF_L1], "%.3f kW", s.gridPower[0] / 1000.0f);
    setNum(inf.labels[INF_L2], "%.3f kW", s.gridPower[1] / 1000.0f);
    setNum(inf.labels[INF_L3], "%.3f kW", s.gridPower[2] / 1000.0f);
    float pvTotal = s.pvPower[0] + s.pvPower[1] + s.s0Power;
    setNum(inf.labels[INF_PV], "%.3f kW", pvTotal / 1000.0f);
    if (s.haveData) {
      setNum(inf.labels[INF_CORE], "%.1f °C", s.coreTemp);
      setNum(inf.labels[INF_HTEMP], "%.1f °C", s.heatSinkTemp);
      setNum(inf.labels[INF_FREQ], "%.2f Hz", s.gridFrequency[0]);
    }
  }

  AppPage &dev = s_pages[PAGE_DEVICE];
  if (dev.labels[DEV_SOC]) {
    if (s.haveData) {
      setNum(dev.labels[DEV_SOC], "%.0f %%", s.batterySoc);
      // Battery-centric sign on the Akku page: charging = "+", discharging =
      // "-", the opposite of the Overview flow diagram (discharge feeds the
      // house and reads "+" there). %+ keeps the sign column stable, so A and
      // V stay put when the battery switches between charging/discharging.
      setNum(dev.labels[DEV_BAT], "%+.3f kW  %.1f A  %.1f V",
             -s.batteryPower / 1000.0f, -s.batteryCurrent, s.batteryVoltage);
      setNum(dev.labels[DEV_BTEMP], "%.1f °C", s.batteryTemp);

      // Next calibration: the inverter reports a Unix timestamp; turn it
      // into a date plus a day countdown once SNTP has a valid wall clock.
      // The date is written the way this language writes dates
      // (langDateFmt): 24.03.2026 in the German build, 2026-03-24 in the
      // English one - one format for both would put the month where the year
      // stands in English.
      if (s.nextCalibTs) {
        const time_t calib = (time_t)s.nextCalibTs;
        struct tm tmv;
        localtime_r(&calib, &tmv);
        char date[16];
        strftime(date, sizeof(date), langDateFmt(), &tmv);
        const time_t nowT = time(nullptr);
        if (nowT > 1000000000) { // synced (epoch after 2001-09-09)
          const long days = (long)((calib - nowT) / 86400);
          if (days < 0) {
            setText(dev.labels[DEV_CALIB], tr(T_D_CALIB_OVERDUE), date);
          } else if (days == 0) {
            setText(dev.labels[DEV_CALIB], tr(T_D_CALIB_TODAY), date);
          } else {
            setText(dev.labels[DEV_CALIB], tr(T_D_CALIB_DAYS), date, days);
          }
        } else {
          setText(dev.labels[DEV_CALIB], "%s", date);
        }
      } else {
        setText(dev.labels[DEV_CALIB], "--");
      }

      setNum(dev.labels[DEV_CYCLES], "%.0f", s.batteryCycles);
      setNum(dev.labels[DEV_SOH], "%.1f %%", s.batterySoh);
      setText(dev.labels[DEV_ISLAND], "%s",
              s.islandKnown ? tr(s.islandMode ? T_D_YES : T_D_NO) : "--");
    }
  }

  diagPhase("gui.service");
  AppPage &sv = s_pages[PAGE_SERVICE];
  if (sv.labels[SV_BAT_STATUS]) {
    if (!s.haveBattery) {
      lv_label_set_text(sv.labels[SV_BAT_STATUS], tr(T_D_TEND_NOBAT));
    } else if (!s.haveData) {
      lv_label_set_text(sv.labels[SV_BAT_STATUS], "--");
    } else {
      // Decoded state and raw register value on one line: "Unterspannung
      // (0x00000200)". The hex is what makes an unexplained state readable -
      // it is the register a support question can be asked about - and a
      // second line for it would have to be matched up by eye.
      char tmp[64];
      serviceBatteryDecode(s.batteryStatus, s.batteryPower, tmp, sizeof(tmp));
      setText(sv.labels[SV_BAT_STATUS], "%s (0x%08lX)", tmp,
              (unsigned long)s.batteryStatus);
    }

    if (!s.haveData) {
      lv_label_set_text(sv.labels[SV_FLT_LIST], "--");
    } else {
      char tmp[512];
      int nFlt = serviceFaultText(s.faultBits, tmp, sizeof(tmp));
      lv_label_set_text(sv.labels[SV_FLT_LIST],
                        nFlt > 0 ? tmp : tr(T_D_NO_FAULTS));
      lv_obj_set_style_text_color(sv.labels[SV_FLT_LIST],
                                  nFlt > 0 ? uiText() : uiOk(), 0);
    }
  }

  // SD card log status (1 Hz refresh; the writer module keeps it fresh).
  if (sv.labels[SV_SD]) {
    lv_label_set_text(sv.labels[SV_SD], sdStatusText());
  }

  // Screenshot countdown. Runs here rather than in the button's own callback
  // because the user is expected to leave the Service page before the shot is
  // taken; a 1 s tick is exactly the resolution the countdown needs, so this
  // costs no extra timer.
  if (s_shotDueMs != 0) {
    const int32_t leftMs = (int32_t)(s_shotDueMs - millis());
    if (sv.labels[SV_SHOT]) {
      setText(sv.labels[SV_SHOT], tr(T_D_SHOT_COUNT),
              leftMs > 0 ? (int)(leftMs / 1000) + 1 : 0);
    }
  }

  // Screenshot handoff: the card worker needs over a second for 691 kB (and up
  // to 16 s on a card that fell back to the 400 kHz clock), so the buffer stays
  // allocated until it says it is finished. Only then is it released - freeing it
  // earlier would hand the worker memory that PSRAM has already handed to
  // something else.
  if (s_shotBuf != nullptr && sdTakeShotDone()) {
    heap_caps_free(s_shotBuf);
    s_shotBuf = nullptr;
    if (sv.labels[SV_SHOT]) {
      // "fehlgeschlagen" only when the worker says the file did not make it onto
      // the card whole: an incomplete BMP is deleted rather than kept, so there is
      // nothing to point at, and the log line next to it says how far it got.
      lv_label_set_text(sv.labels[SV_SHOT],
                        sdShotOk() ? tr(T_D_SHOT_SAVED)
                                   : tr(T_D_SHOT_FAILED));
    }
  }

  // Switched output: the function it follows, and what it is doing right now.
  // Both are read on the same 1 s tick as everything else on this page. The
  // relay's own timings (20 s on-delay, 60 s minimum hold) are much longer, so
  // this refresh exists to show the measured value next to the threshold it is
  // compared against - not to switch anything.
  if (sv.labels[SV_RELAY]) {
    char mode[48];
    relayModeText(mode, sizeof(mode));
    setText(sv.labels[SV_RELAY], "%s", mode);
    serviceRelayLayout();
    serviceRelayState();
  }

  // Web interface: the panel's own address (top left, under the battery status)
  // and the code next to it on the right. Both exist only in normal operation,
  // and both are read on every 1 s tick so the code also shows up directly after
  // a tap without waiting for a page change.
  if (sv.labels[SV_WEB]) {
    if (webRunning()) {
      char ip[20];
      strlcpy(ip, WiFi.localIP().toString().c_str(), sizeof(ip));
      setText(sv.labels[SV_WEB], tr(T_D_IP), ip);
      setText(sv.labels[SV_CODE], tr(T_D_CODE), webCode(nullptr));
    } else if (s_webWasUp) {
      // The server is gone (provisioning started, or the link dropped): clear
      // the code, which is no longer valid for anything.
      setText(sv.labels[SV_WEB], "%s", tr(T_D_NO_NET));
      setText(sv.labels[SV_CODE], "%s", tr(T_D_CODE_EMPTY));
    }
  }
  s_webWasUp = webRunning();

  diagPhase("gui.hist");
  // --- 24 h history: gap summary ---
  if (s_chart) {
    AppPage &gp = s_pages[PAGE_GRAPH];
    if (s_gapCount == 0) {
      setText(gp.labels[GH_GAPS], "");
    } else {
      char g[64];
      if (s_gapCount == 1) {
        snprintf(g, sizeof(g), tr(T_D_GAP_ONE),
                 (unsigned long)((s_gapSeconds + 30) / 60));
      } else {
        snprintf(g, sizeof(g), tr(T_D_GAP_MANY), s_gapCount,
                 (unsigned long)((s_gapSeconds + 30) / 60));
      }
      setText(gp.labels[GH_GAPS], "%s", g);
    }
  }

  // --- 24 h history: seed once from the SD log, then one sample per 5 min ---
  if (s_chart) {
    if (!s_histSeeded) {
      if (s_histSeedStartMs == 0) {
        s_histSeedStartMs = millis();
      }
      const bool graceOver = (millis() - s_histSeedStartMs) >= HIST_SEED_WINDOW_MS;
      static SdHistSample seed[HIST_POINTS];
      if (sdMounted()) {
        // Card in. Ask the worker to scan the log, but only once the log file
        // can actually be named: a mount that succeeds before SNTP (usually
        // within ~2 s) would look for the pre-SNTP uptime file while the
        // writer is about to switch to RCT-YYYYMM.csv. After the grace window
        // we stop caring and take whatever is on the card.
        //
        // The scan is asynchronous: it reads whole CSV files and would freeze
        // this task (and with it the whole GUI) for over a second.
        static bool histAsked = false;
        if (!histAsked) {
          histAsked = true;
          sdRequestHistory(HIST_POINTS, !graceOver);
        }
        const int n = sdTakeHistory(seed, HIST_POINTS);
        if (n >= 0) {
          histAsked = false;
          s_histSeeded = true; // (re)mounts later are ignored on purpose
          if (n > 0) {
            for (int r = 0; r < n; r++) {
              histPush(seed[r].v, seed[r].ts);
            }
            updateChartRange();
            if (s_gapCount > 0) {
              Serial.printf("hist: %d samples restored from SD log, %d gap(s) "
                            "totalling %lu s of missing data\n",
                            n, s_gapCount, (unsigned long)s_gapSeconds);
            } else {
              Serial.printf("hist: %d samples restored from SD log\n", n);
            }
          }
          s_lastHistMs = millis(); // first live sample at the next interval
        } else if (n == -1) {
          // Deferred (clock not up yet): ask again on the next tick.
          histAsked = false;
        }
      } else if (graceOver) {
        s_histSeeded = true; // no card in the grace window: start fresh
        Serial.println("hist: no SD log, starting fresh");
        s_lastHistMs = 0; // first live sample immediately
      }
    }
    if (s_histSeeded && s.haveData) {
      uint32_t now = millis();
      if (s_lastHistMs == 0 || now - s_lastHistMs >= HIST_INTERVAL_MS) {
        s_lastHistMs = now;
        float v[HIST_SERIES] = {0.0f};
        v[0] = s.gridPowerSum;                                    // Netz
        // Verbrauch: the inverter's load meter already subtracted the S0
        // generator, so household demand is meter + external (same rule as the
        // SD restore, see sdlog.cpp parseLine).
        v[1] = s.loadPower[0] + s.loadPower[1] + s.loadPower[2] + s.s0Power;
        v[2] = s.pvPower[0] + s.pvPower[1];                      // PV A+B
        v[3] = s.s0Power;                                        // S0
        v[4] = s.batteryPower;                                   // Bat
        v[5] = s.batterySoc;                                     // SOC %
        // Wall clock if it is up, else 0. A 0 timestamp disables gap detection
        // for this sample rather than inventing a time: before SNTP there is
        // nothing to compare against, and millis() restarts every boot anyway.
        const time_t nowT = time(nullptr);
        histPush(v, nowT > 1000000000 ? (uint32_t)nowT : 0u);
        updateChartRange();
      }
    }
  }
  // End of the last section. If a freeze is ever reported from a phase before
  // this one, refreshCb returned and the hang is in what runs after it - that
  // distinction is worth one string, because the two have completely different
  // causes.
  diagPhase("gui.end");
}

// Store one sample in the ring and feed the chart. Ring cursor and the
// chart's per-series cursor advance in lockstep, so replaying restored rows
// through this same call keeps both views identical to a live recording.
// The sample interval is a design constant, but what actually arrives is not:
// the panel can be off, the SD card can be out, the device can stay silent for
// minutes. Those are the gaps this detects, and they are drawn rather than
// hidden - a line interpolated across three missing samples invents data that
// was never measured, which is exactly the failure a history view must not
// have. A gap marker is an empty chart point (LV_CHART_POINT_NONE), so the
// stroke breaks and the reader sees where the recording stopped.
static void histPush(const float v[HIST_SERIES], uint32_t ts) {
  // Gap markers are the unmeasured slots between the previous sample and this
  // one. The threshold absorbs a sample landing a few seconds late, and the
  // missing count is the elapsed time expressed in sample intervals, rounded.
  if (s_lastHistTs != 0 && ts != 0 && ts > s_lastHistTs) {
    const uint32_t dt = ts - s_lastHistTs;
    const uint32_t kIntervalSec = HIST_INTERVAL_MS / 1000;
    if (dt > kIntervalSec + kIntervalSec / 2) {
      int missing = (int)((dt + kIntervalSec / 2) / kIntervalSec) - 1;
      if (missing > 0) {
        // Bound the markers: a card out for a week would otherwise spend the
        // whole ring on markers and evict every real sample in it.
        if (missing > HIST_POINTS) missing = HIST_POINTS;
        for (int m = 0; m < missing; m++) {
          for (int i = 0; i < HIST_SERIES; i++) {
            if (s_chart) {
              lv_chart_set_next_value(s_chart, s_chartSer[i],
                                      LV_CHART_POINT_NONE);
            }
          }
          s_hist[s_histNext * HIST_SERIES] = 0.0f; // placeholder, unread
          s_histOk[s_histNext] = 0;
          s_histTs[s_histNext] = 0;
          s_histNext = (s_histNext + 1) % HIST_POINTS;
          if (s_histCount < HIST_POINTS) s_histCount++;
        }
        s_gapCount++;
        s_gapSeconds += (uint32_t)missing * kIntervalSec;
      }
    }
  }

  float *dst = &s_hist[s_histNext * HIST_SERIES];
  for (int i = 0; i < HIST_SERIES; i++) {
    dst[i] = v[i];
    if (s_chart) {
      lv_chart_set_next_value(s_chart, s_chartSer[i], (int32_t)v[i]);
    }
  }
  s_histOk[s_histNext] = 1;
  s_histTs[s_histNext] = ts;
  s_histNext = (s_histNext + 1) % HIST_POINTS;
  if (s_histCount < HIST_POINTS) s_histCount++;
  if (ts != 0) s_lastHistTs = ts;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void guiSetup() {
  // Before the first colour is used: the theme is a property of the stored
  // settings, and the panel should come up in the theme it was left in rather
  // than flash the other one on the way.
  themeLaden();
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, uiBg(), 0);

  // Status bar.
  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_set_size(bar, 480, STATUS_H);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, COL_BAR, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  // Full-bleed bar: no rounded corners, the rounded edges would only look like
  // an unfinished panel against the screen border.
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_left(bar, 0, 0);
  lv_obj_set_style_pad_right(bar, 0, 0);
  lv_obj_set_style_pad_top(bar, 0, 0);
  lv_obj_set_style_pad_bottom(bar, 0, 0);
  lv_obj_t *title = lv_label_create(bar);
  lv_label_set_text(title, "RCT Power Panel");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16_uml, 0);
  lv_obj_set_style_text_color(title, uiText(), 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);
  s_statusLabel = lv_label_create(bar);
  lv_label_set_text(s_statusLabel, "boot");
  lv_obj_set_style_text_font(s_statusLabel, &lv_font_montserrat_16_uml, 0);
  lv_obj_align(s_statusLabel, LV_ALIGN_RIGHT_MID, -12, 0);

  // Splash label shown while provisioning happens (blocking).
  s_splashLabel = lv_label_create(scr);
  lv_label_set_text(s_splashLabel, "Starting ...");
  lv_obj_set_style_text_font(s_splashLabel, &lv_font_montserrat_20_uml, 0);
  lv_obj_set_style_text_color(s_splashLabel, uiText(), 0);
  lv_obj_center(s_splashLabel);
}

void guiSetSplashText(const char *text) {
  if (s_splashLabel) {
    lv_label_set_text(s_splashLabel, text);
    displayLooper();
  }
}

void guiStartApp() {
  // Remove splash.
  if (s_splashLabel) {
    lv_obj_delete(s_splashLabel);
    s_splashLabel = nullptr;
  }

  // Content area (pages live here).
  lv_obj_t *content = lv_obj_create(lv_screen_active());
  lv_obj_set_size(content, 480, CONTENT_H);
  lv_obj_align(content, LV_ALIGN_TOP_MID, 0, STATUS_H);
  lv_obj_set_style_bg_color(content, uiBg(), 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_left(content, 0, 0);
  lv_obj_set_style_pad_right(content, 0, 0);
  lv_obj_set_style_pad_top(content, 0, 0);
  lv_obj_set_style_pad_bottom(content, 0, 0);

  // Pages. One heading per page, drawn centrally at HEAD_Y (see below).
  static const LangId titleIds[PAGE_COUNT] = {
      T_D_HEAD_OVERVIEW, T_D_HEAD_ENERGY,  T_D_HEAD_HEUTE, T_D_HEAD_GRAPH,
      T_D_HEAD_INFO,     T_D_HEAD_BATTERY, T_D_HEAD_SERVICE};
  void (*builders[PAGE_COUNT])(AppPage *) = {
      pageBuildOverview, pageBuildEnergy, pageBuildHeute, pageBuildGraph,
      pageBuildInfo, pageBuildDevice, pageBuildService};
  for (int i = 0; i < PAGE_COUNT; i++) {
    s_pages[i].title = tr(titleIds[i]);
    s_pages[i].labelCount = 0;
    s_pages[i].root = lv_obj_create(content);
    lv_obj_set_size(s_pages[i].root, 480, CONTENT_H);
    lv_obj_set_style_bg_color(s_pages[i].root, uiBg(), 0);
    lv_obj_set_style_border_width(s_pages[i].root, 0, 0);
    lv_obj_set_style_pad_all(s_pages[i].root, 0, 0);
    s_pages[i].build = builders[i];
    for (int k = 0; k < MAX_PAGE_LABELS; k++) {
      s_pages[i].labels[k] = nullptr;
    }
    // Page heading: every page gets the same one at the top left, in the same
    // white and font as the "RCT Power Panel" text in the title bar. Created
    // here rather than per page so the seven pages cannot drift apart again.
    lv_obj_t *head = makeLabel(s_pages[i].root, s_pages[i].title,
                              &lv_font_montserrat_16_uml, uiText());
    lv_obj_set_pos(head, 20, HEAD_Y);
    builders[i](&s_pages[i]);
  }

  // Navigation bar: left | home | right.
  lv_obj_t *nav = lv_obj_create(lv_screen_active());
  lv_obj_set_size(nav, 480, NAV_H);
  lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(nav, COL_BAR, 0);
  lv_obj_set_style_border_width(nav, 0, 0);
  lv_obj_set_style_radius(nav, 0, 0); // full-bleed like the status bar
  lv_obj_set_style_pad_left(nav, 6, 0);
  lv_obj_set_style_pad_right(nav, 6, 0);
  lv_obj_set_style_pad_top(nav, 6, 0);
  lv_obj_set_style_pad_bottom(nav, 6, 0);
  lv_obj_set_style_pad_column(nav, 6, 0);
  lv_obj_set_layout(nav, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *bLeft = makeButton(nav, LV_SYMBOL_LEFT, navPrevCb, nullptr);
  lv_obj_t *bHome = makeButton(nav, LV_SYMBOL_HOME, navHomeCb, nullptr);
  lv_obj_t *bRight = makeButton(nav, LV_SYMBOL_RIGHT, navNextCb, nullptr);
  lv_obj_set_flex_grow(bLeft, 1);
  lv_obj_set_flex_grow(bHome, 1);
  lv_obj_set_flex_grow(bRight, 1);
  lv_obj_set_size(bLeft, 140, NAV_H - 12);
  lv_obj_set_size(bHome, 140, NAV_H - 12);
  lv_obj_set_size(bRight, 140, NAV_H - 12);

  // Touch input device (GT911 -> LVGL pointer). Wire must be set up before
  // the read callback starts polling; failure only disables touch - and the
  // backlight, which would then have no way back from "off" (Backlight.h).
  backlightSetWakeable(touchInit());
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchReadMarked);

  showPage(PAGE_OVERVIEW);

  // Wi-Fi setup overlay: build hidden; the first refresh tick shows it for
  // the 10 s boot test window (and whenever the provisioning AP is up).
  buildApOverlay();
  s_apTestUntil = millis() + 10000;

  s_refreshTimer = lv_timer_create(refreshCb, 1000, nullptr);
  lv_timer_ready(s_refreshTimer);
}