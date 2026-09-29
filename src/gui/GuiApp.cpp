// LVGL panel UI.
//
// Page 1 mirrors the RCT Portal "Energiefluss" pane (see examples/):
// a central household node with PV / batterie / netz nodes around it,
// connector lines that light up red in the direction of the actual energy
// flow, live kW values under each node, and a status table below. The RCT
// logo is intentionally omitted (the center shows a house icon instead).
//
// Data note: PV = dc_conv.dc_conv_struct[0|1].p_dc_lp plus the S0 meter
// (io_board.s0_external_power, merged as "EXT." in the portal), household =
// g_sync.p_ac_load[0..2], battery = battery.soc/current/voltage and
// g_sync.p_acc_lp (positive = charging). Nodes stay "-" until the device
// answers the respective OIDs.
//
// Pages: 1 Energiefluss, 2 Heute (day summary), 3 Info, 4 Verlauf (24 h
// power graph; one sample every 5 minutes, PV A+B and S0 as separate series),
// 5 Gerät (device info polled every 10 s).
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

#include "GuiApp.h"

#include "../config/Configuration.h"
#include "../display/Display.h"
#include "../display/Touch.h"
#include "../rct/RctTypes.h"
#include "../storage/sdlog.h"
#include "fonts/lv_font_portal_icons_20.h"
// Montserrat with German umlauts (Latin-1 supplement), falling back to the
// LVGL built-ins for the LV_SYMBOL_* glyphs. See OFL-Montserrat.txt.
#include "fonts/lv_font_montserrat_14_uml.h"
#include "fonts/lv_font_montserrat_16_uml.h"
#include "fonts/lv_font_montserrat_20_uml.h"
#include "fonts/lv_font_montserrat_28_uml.h"

#define NAV_H 72
#define STATUS_H 44
#define CONTENT_H (480 - STATUS_H - NAV_H)

// ---------------------------------------------------------------------------
// Palette (from the RCT Portal Energiefluss: white nodes, red active flows)
// ---------------------------------------------------------------------------
static const lv_color_t COL_BG = lv_color_hex(0x101418);
static const lv_color_t COL_CARD = lv_color_hex(0x1C222A);
static const lv_color_t COL_BAR = lv_color_hex(0x3A4550); // top / bottom bars (clearly brighter than cards)
static const lv_color_t COL_ACCENT = lv_color_hex(0x2E93E5);
static const lv_color_t COL_TEXT = lv_color_hex(0xE8ECF1);
static const lv_color_t COL_MUTED = lv_color_hex(0x8A94A0);
static const lv_color_t COL_OK = lv_color_hex(0x3EC97A);
static const lv_color_t COL_ERR = lv_color_hex(0xE5484D);
static const lv_color_t COL_BORDER = lv_color_hex(0x2A3038); // stat card ring

// Energiefluss palette (reference values)
static const lv_color_t FLOW_RED = lv_color_hex(0xCA0C0F);   // active flow / value
static const lv_color_t FLOW_GRAY = lv_color_hex(0x555658);  // icon inside nodes
static const lv_color_t FLOW_BORDER = lv_color_hex(0x6E6F72); // node ring
static const lv_color_t FLOW_LINE = lv_color_hex(0xCBCBCD);  // idle connector
static const lv_color_t FLOW_WHITE = lv_color_hex(0xFFFFFF); // node fill

// RCT Portal icons (U+E039 "provided" = grid/Netz node, U+E044 "solar" = PV
// node) from the portal's embedded icon font. Baked into
// lv_font_portal_icons_20 (see src/gui/fonts/); the built-in Montserrat
// symbol fonts do not cover PUA/Unicode beyond the LV_SYMBOL_* set.
static const char kPortalGridIcon[] = "\uE039";
static const char kPortalSolarIcon[] = "\uE044";

// ---------------------------------------------------------------------------
// Page model
// ---------------------------------------------------------------------------
enum PageId {
  PAGE_OVERVIEW = 0,
  PAGE_ENERGY,
  PAGE_INFO,
  PAGE_GRAPH,   // 24 h power history
  PAGE_DEVICE,  // device info (Gerät)
  PAGE_SERVICE, // battery status, faults, provisioning
  PAGE_COUNT,
};

static const int MAX_PAGE_LABELS = 20;

// Overview page label indices.
enum OvLabel {
  OV_GRID_VAL = 0, // netz kW value (red)
  OV_GRID_DIR,     // "Bezug" / "Einspeisung"
  OV_HOUSE_VAL,    // haus kW value
  OV_PV_VAL,       // pv kW value
  OV_BAT_VAL,      // batterie kW value
  OV_BAT_SOC,      // SOC % inside the battery node
  OV_GRID_ARROW,   // direction arrow on the grid connector
  OV_PV_ARROW,     // direction arrow on the PV connector
  OV_BAT_ARROW,    // direction arrow on the battery connector
  OV_T_ERZ,        // status table: Erzeugung
  OV_T_VERB,       // status table: Verbrauch
  OV_T_NETZ,       // status table: Netz
  OV_T_BAT,        // status table: Batterie
  OV_LABEL_COUNT,
};

// Info page label indices.
enum InfoLabel {
  INF_HOST = 0,
  INF_PORT,
  INF_LINK,
  INF_LAST,
  INF_UPTIME,
  INF_L1,   // per-phase grid power
  INF_L2,
  INF_L3,
  INF_PV,   // PV total (A + B + S0)
  INF_HOUSE,
  INF_SOC,  // battery SOC
  INF_BAT,  // battery power / current / voltage
  INF_LABEL_COUNT,
};

// Heute (day summary) page label indices. Mirrors the portal "Übersicht"
// boxes (Erzeugt / Eigenverbrauch / Eingespeist) and "Energiestatistiken"
// (Autarkie / Eigenverbrauch). Value and caption share alternating slots.
enum EnLabel {
  EN_TITLE = 0,
  EN_GEN_VAL, EN_GEN_LBL,     // Erzeugt (kWh)
  EN_SELF_VAL, EN_SELF_LBL,   // Eigenverbrauch from PV (kWh)
  EN_FEED_VAL, EN_FEED_LBL,   // Eingespeist (kWh)
  EN_VERB_VAL, EN_VERB_LBL,   // Verbrauch / household (kWh)
  EN_BEZU_VAL, EN_BEZU_LBL,   // Bezug / grid draw (kWh)
  EN_AUT_VAL, EN_AUT_LBL,     // Autarkie (%)
  EN_EVB_VAL, EN_EVB_LBL,     // Eigenverbrauch (%)
  EN_LABEL_COUNT,
};

// 24 h history (graph) page label indices.
enum GhLabel {
  GH_TITLE = 0,
  GH_LABEL_COUNT,
};

// Device info (Gerät) page label indices.
enum DevLabel {
  DEV_NAME = 0, // device name
  DEV_SW,       // control software version
  DEV_CORE,     // core temperature
  DEV_BTEMP,    // battery temperature
  DEV_HTEMP,    // heat sink temperature
  DEV_CALIB,    // next battery calibration
  DEV_CYCLES,   // charge/discharge cycles
  DEV_FREQ,     // grid frequency L1
  DEV_SOH,      // battery state of health
  DEV_ISLAND,   // island (grid-separated) mode
  DEV_LABEL_COUNT,
};

// Service page label indices.
enum SvLabel {
  SV_TITLE = 0,
  SV_BAT_STATUS, // decoded battery status (white)
  SV_BAT_RAW,    // raw bitfield hex (muted)
  SV_FLT_LIST,   // decoded faults, one per line
  SV_SD,         // SD history log status
  SV_LABEL_COUNT,
};

// --- 24 h power history ----------------------------------------------------
// One sample every 5 minutes while the device is running; the ring buffer
// holds 288 samples (= exactly 24 h). Values are W, mirrored as float for the
// Y autoscale and fed to the LVGL chart as int32. S0 is kept as its own series
// (not merged into the PV A+B total), mirroring the portal's separate "+EXT."
// node. Battery/grid may be negative (discharge / feed-in).
static const int HIST_POINTS = 288;              // 288 * 5 min = 24 h
static const int HIST_SERIES = 5;                // grid, house, PV, S0, battery
static const uint32_t HIST_INTERVAL_MS = 300000; // 5 min
static const uint32_t HIST_SEED_WINDOW_MS = 60000; // boot grace without a card
static const uint32_t kHistColor[HIST_SERIES] = {0xCA0C0F, 0xA45EE5, 0x3EC97A,
                                                 0x2E93E5, 0xF0A202};
static const char *const kHistName[HIST_SERIES] = {"Netz", "Haus", "PV", "S0",
                                                   "Bat"};
static lv_obj_t *s_chart = nullptr;
static lv_chart_series_t *s_chartSer[HIST_SERIES] = {nullptr};
static float s_hist[HIST_POINTS * HIST_SERIES] = {0.0f}; // packed [pt][ser]
static int s_histCount = 0; // samples stored so far
static int s_histNext = 0;  // next write slot (ring cursor)
static uint32_t s_lastHistMs = 0; // time of the last stored sample
static bool s_histSeeded = false; // history seeded once from the SD log
static uint32_t s_histSeedStartMs = 0; // boot time used for the no-card grace

// Defined below refreshCb (which calls it): ring + chart share one writer so
// restored rows and live samples land in identical state.
static void histPush(const float v[HIST_SERIES]);

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
static void setText(lv_obj_t *label, const char *fmt, ...) {
  char buf[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
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

static lv_obj_t *makeButton(lv_obj_t *parent, const char *symbol,
                            lv_event_cb_t cb, void *userData) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_style_bg_color(btn, COL_BAR, 0);
  lv_obj_set_style_bg_color(btn, COL_ACCENT, LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, COL_MUTED, 0);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);
  lv_obj_t *l = lv_label_create(btn);
  lv_label_set_text(l, symbol);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_28_uml, 0);
  lv_obj_set_style_text_color(l, COL_TEXT, 0);
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
// Page builders
// ---------------------------------------------------------------------------

// Energiefluss diagram (reference layout: PV left, haus center, batterie
// bottom-centre below haus, netz right).
static void pageBuildOverview(AppPage *p) {
  lv_obj_t *root = p->root;

  // Node circles.
  makeNode(root, 240, 82, 92, LV_SYMBOL_HOME, &lv_font_montserrat_28_uml); // haus
  makeNode(root, 60, 80, 60, kPortalSolarIcon, &lv_font_portal_icons_20); // pv
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
  // centre line, percentage just below it).
  lv_obj_t *batIco =
      makeLabel(bat, LV_SYMBOL_BATTERY_FULL, &lv_font_montserrat_20_uml, FLOW_GRAY);
  lv_obj_align(batIco, LV_ALIGN_CENTER, 0, -7);
  lv_obj_t *batSoc =
      makeLabel(bat, "--", &lv_font_montserrat_14_uml, FLOW_GRAY);
  lv_obj_align(batSoc, LV_ALIGN_CENTER, 0, 9);
  p->labels[OV_BAT_SOC] = batSoc;

  // NETZ node: portal grid icon ("provided", U+E039).
  makeNode(root, 420, 80, 60, kPortalGridIcon, &lv_font_portal_icons_20);

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

  // Values under each node.
  p->labels[OV_GRID_VAL] = makeValueLabel(root, 360, 118);
  p->labels[OV_GRID_DIR] = makeLabel(root, "", &lv_font_montserrat_14_uml, FLOW_RED);
  lv_obj_set_pos(p->labels[OV_GRID_DIR], 360, 144);
  lv_obj_set_style_text_align(p->labels[OV_GRID_DIR], LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(p->labels[OV_GRID_DIR], 120);

  // Haus value sits right of the vertical battery line (x=240) so the line no
  // longer runs through the text.
  p->labels[OV_HOUSE_VAL] = makeValueLabel(root, 250, 135);
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

  // Status table (2x2): Erzeugung / Verbrauch / Netz / Batterie.
  struct {
    int x, y;
    const char *name;
    int labelIdx;
  } cells[4] = {
      {12, 284, "Erzeugung", OV_T_ERZ},
      {248, 284, "Verbrauch", OV_T_VERB},
      {12, 324, "Netz", OV_T_NETZ},
      {248, 324, "Batterie", OV_T_BAT},
  };
  for (int i = 0; i < 4; i++) {
    // Unify the lower part: name + status both white.
    lv_obj_t *nm = makeLabel(root, cells[i].name, &lv_font_montserrat_14_uml, FLOW_WHITE);
    lv_obj_set_pos(nm, cells[i].x, cells[i].y);
    lv_obj_t *st = makeLabel(root, "--", &lv_font_montserrat_14_uml, FLOW_WHITE);
    lv_obj_set_pos(st, cells[i].x + 110, cells[i].y);
    p->labels[cells[i].labelIdx] = st;
  }
  p->labelCount = OV_LABEL_COUNT;
}

// Current-day summary page, mirroring the portal "Übersicht" (Erzeugt /
// Eigenverbrauch / Eingespeist kWh boxes) and "Energiestatistiken" (Autarkie /
// Eigenverbrauch percentage gauges). All energy values are scaled ÷1000 → kWh.
static void pageBuildEnergy(AppPage *p) {
  lv_obj_t *root = p->root;
  p->labels[EN_TITLE] = makeLabel(root, "Heute", &lv_font_montserrat_16_uml, COL_MUTED);
  lv_obj_set_pos(p->labels[EN_TITLE], 20, 10);

  struct {
    int x, y, w, h;
    const char *caption;
    int valIdx, capIdx;
  } cards[] = {
      {16, 36, 144, 96, "Erzeugt", EN_GEN_VAL, EN_GEN_LBL},
      {164, 36, 144, 96, "Eigenverbrauch", EN_SELF_VAL, EN_SELF_LBL},
      {312, 36, 144, 96, "Eingespeist", EN_FEED_VAL, EN_FEED_LBL},
      // Lower rows: two cards with the same 4 px gap as the first row, so
      // 222 px wide starting at 16 and 242.
      {16, 146, 222, 88, "Verbrauch", EN_VERB_VAL, EN_VERB_LBL},
      {242, 146, 222, 88, "Bezug", EN_BEZU_VAL, EN_BEZU_LBL},
      {16, 248, 222, 100, "Autarkie", EN_AUT_VAL, EN_AUT_LBL},
      {242, 248, 222, 100, "Eigenverbrauch", EN_EVB_VAL, EN_EVB_LBL},
  };
  for (unsigned i = 0; i < sizeof(cards) / sizeof(cards[0]); i++) {
    makeStatCard(root, cards[i].x, cards[i].y, cards[i].w, cards[i].h,
                 cards[i].caption, &p->labels[cards[i].valIdx],
                 &p->labels[cards[i].capIdx]);
  }
  p->labelCount = EN_LABEL_COUNT;
}

static void pageBuildInfo(AppPage *p) {
  lv_obj_t *root = p->root;
  const char *rows[INF_LABEL_COUNT] = {
      "RCT host:  --", "RCT port:  --", "Link:      --", "Last data: --",
      "Uptime:    --", "Netz L1:   -- kW", "Netz L2:   -- kW", "Netz L3:   -- kW",
      "PV:        -- kW", "Haus:      -- kW", "Bat SOC:   --",
      "Bat:       --",
  };
  for (int i = 0; i < INF_LABEL_COUNT; i++) {
    p->labels[i] = makeLabel(root, rows[i], &lv_font_montserrat_16_uml, COL_TEXT);
    lv_obj_align(p->labels[i], LV_ALIGN_TOP_LEFT, 24, 24 + i * 28);
  }
  p->labelCount = INF_LABEL_COUNT;
}

// Device info page (portal "Gerätedetails"): name / software version /
// temperatures / next calibration / cycles / grid frequency / SOH / island
// mode. The panel font only covers ASCII, so German umlauts are written as
// "ue/ae/oe" (a custom font with umlauts is a possible future nice-to-have).
static void pageBuildDevice(AppPage *p) {
  lv_obj_t *root = p->root;
  const char *rows[DEV_LABEL_COUNT] = {
      "Name:          --",
      "Software:      --",
      "Kern:          --",
      "Batterie:      --",
      "Kühlkörper:  --",
      "Kalibrierung:  --",
      "Zyklen:        --",
      "Netzfrequenz:  -- Hz",
      "SOH:           --",
      "Inselbetrieb:  --",
  };
  for (int i = 0; i < DEV_LABEL_COUNT; i++) {
    p->labels[i] = makeLabel(root, rows[i], &lv_font_montserrat_16_uml, COL_TEXT);
    lv_obj_align(p->labels[i], LV_ALIGN_TOP_LEFT, 24, 24 + i * 28);
  }
  p->labelCount = DEV_LABEL_COUNT;
}

// ---------------------------------------------------------------------------
// Service page: decoded battery status, a "back to provisioning" button and
// the decoded inverter faults.
// ---------------------------------------------------------------------------

// Fault descriptions from rctclient "Faults": index = bit number in
// fault[0..3].flt (bit n -> "F<n>"). German; kept as a plain table so the
// texts are easy to review / translate later.
static const char *const kFaultDe[128] = {
    "TRAP ausgelöst",                                            //   0
    "RTC nicht konfigurierbar",                                  //   1
    "RTC-1-Hz-Signal-Timeout",                                   //   2
    "Hardware-Stopp durch 3,3-V-Fehler",                         //   3
    "Hardware-Stopp durch PWM-Logik",                            //   4
    "Hardware-Stopp durch Uzk-Überspannung",                     //   5
    "Uzk+ über Grenzwert",                                       //   6
    "Uzk- über Grenzwert",                                       //   7
    "Überstrom Drossel Phase L1",                                //   8
    "Überstrom Drossel Phase L2",                                //   9
    "Überstrom Drossel Phase L3",                                //  10
    "Pufferkondensator-Spannung",                                //  11
    "Quarzfehler",                                               //  12
    "Netzunterspannung Phase 1",                                 //  13
    "Netzunterspannung Phase 2",                                 //  14
    "Netzunterspannung Phase 3",                                 //  15
    "Batterieüberstrom",                                         //  16
    "Relais-Test fehlgeschlagen",                                //  17
    "Platinen-Übertemperatur",                                   //  18
    "Kern-Übertemperatur",                                       //  19
    "Übertemperatur Kühlkörper 1",                               //  20
    "Übertemperatur Kühlkörper 2",                               //  21
    "I2C-Fehler mit Power-Board",                                //  22
    "Power-Board-Fehler",                                        //  23
    "PWM-Ausgänge defekt",                                       //  24
    "Isolation zu gering oder unplausibel",                      //  25
    "I-Gleichanteil max (1 A)",                                  //  26
    "I-Gleichanteil max langsam (47 mA)",                        //  27
    "Möglicher Defekt DSD-Kanal (Offset zu groß)",               //  28
    "RS485-Fehler Relaisbox",                                    //  29
    "Überspannung zwischen Phasen",                              //  30
    "IGBT L1 BH defekt",                                         //  31
    "IGBT L1 BL defekt",                                         //  32
    "IGBT L2 BH defekt",                                         //  33
    "IGBT L2 BL defekt",                                         //  34
    "IGBT L3 BH defekt",                                         //  35
    "IGBT L3 BL defekt",                                         //  36
    "Langzeit-Überspannung Phase 1",                             //  37
    "Langzeit-Überspannung Phase 2",                             //  38
    "Langzeit-Überspannung Phase 3",                             //  39
    "Überspannung Phase 1, Stufe 1",                             //  40
    "Überspannung Phase 1, Stufe 2",                             //  41
    "Überspannung Phase 2, Stufe 1",                             //  42
    "Überspannung Phase 2, Stufe 2",                             //  43
    "Überspannung Phase 3, Stufe 1",                             //  44
    "Überspannung Phase 3, Stufe 2",                             //  45
    "Überfrequenz, Stufe 1",                                     //  46
    "Überfrequenz, Stufe 2",                                     //  47
    "Unterspannung Phase 1, Stufe 1",                            //  48
    "Unterspannung Phase 1, Stufe 2",                            //  49
    "Unterspannung Phase 2, Stufe 1",                            //  50
    "Unterspannung Phase 2, Stufe 2",                            //  51
    "Unterspannung Phase 3, Stufe 1",                            //  52
    "Unterspannung Phase 3, Stufe 2",                            //  53
    "Unterfrequenz, Stufe 1",                                    //  54
    "Unterfrequenz, Stufe 2",                                    //  55
    "CPU-Ausnahme NMI",                                          //  56
    "CPU-Ausnahme HardFault",                                    //  57
    "CPU-Ausnahme MemManage",                                    //  58
    "CPU-Ausnahme BusFault",                                     //  59
    "CPU-Ausnahme UsageFault",                                   //  60
    "RTC Power-on-Reset",                                        //  61
    "RTC-Oszillator gestoppt",                                   //  62
    "RTC-Versorgungsspannung eingebrochen",                      //  63
    "RCD-Sprung DC + AC > 30 mA",                                //  64
    "RCD-Sprung DC > 60 mA",                                     //  65
    "RCD-Sprung AC > 150 mA",                                    //  66
    "RCD-Strom > 300 mA",                                        //  67
    "+5 V fehlerhaft",                                           //  68
    "-9 V fehlerhaft",                                           //  69
    "+9 V fehlerhaft",                                           //  70
    "+3,3 V fehlerhaft",                                         //  71
    "RDC-Kalibrierung fehlgeschlagen",                           //  72
    "I2C-Fehler",                                                //  73
    "AFI-Frequenzgenerator-Fehler",                              //  74
    "Kühlkörpertemperatur zu hoch",                              //  75
    "Uzk über Grenzwert",                                        //  76
    "Usg A über Grenzwert",                                      //  77
    "Usg B über Grenzwert",                                      //  78
    "Einschaltbedingung Umin Phase 1",                           //  79
    "Einschaltbedingung Umax Phase 1",                           //  80
    "Einschaltbedingung Fmin Phase 1",                           //  81
    "Einschaltbedingung Fmax Phase 1",                           //  82
    "Einschaltbedingung Umin Phase 2",                           //  83
    "Einschaltbedingung Umax Phase 2",                           //  84
    "Batteriestromsensor defekt",                                //  85
    "Batterie-Booster defekt",                                   //  86
    "Einschaltbedingung Umin Phase 3",                           //  87
    "Einschaltbedingung Umax Phase 3",                           //  88
    "Spannungssprung/Offset an AC-Klemmen zu groß (Phasenausfall)", // 89
    "Wechselrichter vom Hausnetz getrennt",                      //  90
    "+9-V-Differenz DSP/PIC zu groß",                            //  91
    "1,5-V-Fehler",                                              //  92
    "2,5-V-Fehler",                                              //  93
    "1,5-V-Messdifferenz",                                       //  94
    "2,5-V-Messdifferenz",                                       //  95
    "Batteriespannung außerhalb des erwarteten Bereichs",        //  96
    "PIC-Software nicht startbar",                               //  97
    "PIC-Bootloader unerwartet erkannt",                         //  98
    "Phasenlagefehler (nicht 120°)",                             //  99
    "Batterieüberspannung",                                      // 100
    "Drosselstrom instabil",                                     // 101
    "Netzspannungsdifferenz intern/extern zu groß Phase 1",      // 102
    "Netzspannungsdifferenz intern/extern zu groß Phase 2",      // 103
    "Netzspannungsdifferenz intern/extern zu groß Phase 3",      // 104
    "Externer Not-Aus aktiv",                                    // 105
    "Batterie leer: keine Energie für Standby",                  // 106
    "CAN-Timeout mit Batterie",                                  // 107
    "Timing-Problem",                                            // 108
    "Übertemperatur Kühlkörper Batterie-IGBT",                   // 109
    "Batterie-Kühlkörpertemperatur zu hoch",                     // 110
    "Interner Relaisbox-Fehler",                                 // 111
    "Relaisbox PE-Aus-Fehler",                                   // 112
    "Relaisbox PE-Ein-Fehler",                                   // 113
    "Interner Batteriefehler",                                   // 114
    "Parameter geändert",                                        // 115
    "3 Inselbildungsversuche fehlgeschlagen",                    // 116
    "Unterspannung zwischen Phasen",                             // 117
    "System-Reset erkannt",                                      // 118
    "Update erkannt",                                            // 119
    "FRT-Überspannung",                                          // 120
    "FRT-Unterspannung",                                         // 121
    "IGBT-L1-Freilaufdiode defekt",                              // 122
    "IGBT-L2-Freilaufdiode defekt",                              // 123
    "IGBT-L3-Freilaufdiode defekt",                              // 124
    "Einphasenmodus aktiv, für Geräteklasse nicht erlaubt",      // 125
    "Inselbetrieb erkannt",                                      // 126
    "Neutralleiterfehler",                                       // 127
};

// battery.bat_status decode (rctclient "Bitfields and Enumerations"):
//   * bits 3 + 10 (0x0408 = 1032) both clear  => calibration in progress
//     (the official app appends "(calib.)")
//   * bit 11 (0x0800 = 2048) clear             => balancing in progress
//     (the official app appends "(balance)")
//   * bit 3 set => charging, bit 10 set => discharging, else standby.
// Most other bits are not publicly documented, hence the raw value below.
static void serviceBatteryDecode(uint32_t v, char *out, size_t n) {
  const uint32_t CALIB = (1u << 3) | (1u << 10); // 1032
  const uint32_t BAL = (1u << 11);               // 2048
  const char *state = "--";
  if ((v & CALIB) == 0) {
    state = "Kalibrierung läuft";
  } else if (v & (1u << 3)) {
    state = "Laden";
  } else if (v & (1u << 10)) {
    state = "Entladen";
  } else {
    state = "Standby";
  }
  snprintf(out, n, "%s", state);
  if ((v & BAL) == 0) {
    strncat(out, "  -  Balancing läuft", n - strlen(out) - 1);
  }
}

// Active faults -> "F<n> <description>" lines, capped at kMax so the list
// stays inside the reserved text area; surplus faults are summarized. Returns
// the total number of active faults (0 = none).
static int serviceFaultText(const uint32_t *bits, char *out, size_t n) {
  const int kMax = 10; // reserved rows on the page (14 px font ~18 px/row)
  out[0] = '\0';
  size_t used = 0;
  int count = 0;
  int shown = 0;
  for (int bit = 0; bit < 128; bit++) {
    if (bits[bit / 32] & (1u << (bit % 32))) {
      count++;
      if (shown >= kMax) {
        continue;
      }
      int w = snprintf(out + used, n - used, "F%d %s\n", bit, kFaultDe[bit]);
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
    snprintf(out + used, n - used, " ... und %d weitere", count - shown);
  }
  return count;
}

// "WLAN-Setup starten": re-open the provisioning access point.
static void serviceSetupCb(lv_event_t *e) {
  (void)e;
  restartProvisioning();
}

static void pageBuildService(AppPage *p) {
  lv_obj_t *root = p->root;

  p->labels[SV_TITLE] =
      makeLabel(root, "Service", &lv_font_montserrat_16_uml, COL_MUTED);
  lv_obj_set_pos(p->labels[SV_TITLE], 20, 8);

  auto sectionHead = [&](const char *text, lv_coord_t y) {
    lv_obj_t *h = makeLabel(root, text, &lv_font_montserrat_14_uml, COL_MUTED);
    lv_obj_set_pos(h, 20, y);
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
  lv_label_set_text(bl, LV_SYMBOL_WIFI " Setup starten");
  lv_obj_set_style_text_font(bl, &lv_font_montserrat_16_uml, 0);
  lv_obj_set_style_text_color(bl, FLOW_WHITE, 0);
  lv_obj_center(bl);

  // --- Battery status (decoded from battery.bat_status) ---
  sectionHead("Batterie-Status", 36);
  p->labels[SV_BAT_STATUS] =
      makeLabel(root, "--", &lv_font_montserrat_20_uml, COL_TEXT);
  lv_obj_set_pos(p->labels[SV_BAT_STATUS], 20, 58);
  p->labels[SV_BAT_RAW] =
      makeLabel(root, "", &lv_font_montserrat_14_uml, COL_MUTED);
  lv_obj_set_pos(p->labels[SV_BAT_RAW], 20, 88);

  // --- Faults (decoded, multi-line; several can be active at once) ---
  sectionHead("Störungen", 118);
  p->labels[SV_FLT_LIST] =
      makeLabel(root, "--", &lv_font_montserrat_14_uml, COL_TEXT);
  lv_obj_set_pos(p->labels[SV_FLT_LIST], 20, 142);
  lv_obj_set_width(p->labels[SV_FLT_LIST], 440);
  p->labelCount = SV_LABEL_COUNT;

  // --- SD history log (status only; the writer lives in storage/sdlog.cpp) ---
  (void)sectionHead("SD-Log", 236);
  p->labels[SV_SD] =
      makeLabel(root, sdStatusText(), &lv_font_montserrat_16_uml, COL_TEXT);
  lv_obj_set_pos(p->labels[SV_SD], 20, 260);
  lv_obj_set_width(p->labels[SV_SD], 440);
}

// Recompute the chart Y range from the stored history ring (kW = W / 1000 on
// the axis is implied by the legend; the range itself stays in W).
static void updateChartRange() {
  float loW = 0.0f, hiW = 0.0f;
  bool first = true;
  int start = (s_histNext - s_histCount + HIST_POINTS) % HIST_POINTS;
  for (int p = 0; p < s_histCount; p++) {
    const float *row = &s_hist[((start + p) % HIST_POINTS) * HIST_SERIES];
    for (int i = 0; i < HIST_SERIES; i++) {
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
  loW = floorf(loW / step) * step;
  hiW = ceilf(hiW / step) * step;
  if (hiW - loW < step) hiW = loW + step;

  lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)loW,
                          (int32_t)hiW);
}

// Graph page: all power values of the last 24 hours (legend above, chart
// below). One sample lands every 5 minutes in refreshCb.
static void pageBuildGraph(AppPage *p) {
  lv_obj_t *root = p->root;
  p->labels[GH_TITLE] =
      makeLabel(root, "24 h Verlauf", &lv_font_montserrat_16_uml, COL_MUTED);
  lv_obj_set_pos(p->labels[GH_TITLE], 20, 8);

  // Legend: small color dot + series name.
  for (int i = 0; i < HIST_SERIES; i++) {
    lv_obj_t *dot = lv_obj_create(root);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_pos(dot, 20 + i * 90, 32);
    lv_obj_set_style_bg_color(dot, lv_color_hex(kHistColor[i]), 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_shadow_width(dot, 0, 0);
    lv_obj_t *nm =
        makeLabel(root, kHistName[i], &lv_font_montserrat_14_uml, COL_TEXT);
    lv_obj_set_pos(nm, 34 + i * 90, 29);
  }

  // Chart. Points are seeded with LV_CHART_POINT_NONE so nothing is drawn
  // until real 5-minute samples arrive (no fake zero history after boot).
  s_chart = lv_chart_create(root);
  lv_obj_set_pos(s_chart, 12, 52);
  lv_obj_set_size(s_chart, 456, 298);
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
  for (int i = 0; i < HIST_SERIES; i++) {
    s_chartSer[i] = lv_chart_add_series(s_chart, lv_color_hex(kHistColor[i]),
                                        LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_values(s_chart, s_chartSer[i], LV_CHART_POINT_NONE);
  }
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
  lv_obj_set_style_bg_color(s_apOverlay, COL_BG, 0);
  lv_obj_set_style_radius(s_apOverlay, 0, 0);
  lv_obj_set_style_border_width(s_apOverlay, 0, 0);
  lv_obj_set_style_pad_all(s_apOverlay, 0, 0);
  lv_obj_set_layout(s_apOverlay, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_apOverlay, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_apOverlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_apOverlay, 12, 0);

  lv_obj_t *title = lv_label_create(s_apOverlay);
  lv_label_set_text(title, "Setup: WLAN konfigurieren");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20_uml, 0);
  lv_obj_set_style_text_color(title, COL_TEXT, 0);

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
  lv_label_set_text(hint,
                    "Mit dem Netzwerk verbinden und im Browser\n"
                    "http://192.168.4.1 öffnen");
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
static void refreshCb(lv_timer_t *t) {
  (void)t;
  const RctSnapshot &s = rctState;

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

  // Status badge.
  const char *badge = !s.haveData   ? "no data"
                      : s.connected ? "live"
                                    : "reconnect";
  lv_label_set_text(s_statusLabel, badge);
  lv_obj_set_style_text_color(s_statusLabel, s.haveData ? COL_OK : COL_ERR, 0);

  AppPage &ov = s_pages[PAGE_OVERVIEW];
  if (ov.labels[OV_GRID_VAL]) {
    // Derived quantities (all W; sign conventions per the rctclient docs):
    //   grid  = p_ac_grid_sum_lp        + = Bezug (import from grid)
    //   pv    = p_dc_lp[0]+[1]+S0      >= 0, production
    //   bat   = p_acc_lp               + = charging
    //   house = p_ac_load sum          measured household load
    const float gridActive = 50.0f; // W, below = Standby
    const float pvActive = 20.0f;   // W, below = no visible generation
    const float batActive = 50.0f;  // W, below = Standby
    const char *dash = "--";
    float pTot = s.gridPowerSum;
    float pvTotal = s.pvPower[0] + s.pvPower[1] + s.s0Power;
    float pBat = s.batteryPower;
    float house = s.loadPower[0] + s.loadPower[1] + s.loadPower[2];
    bool has = s.haveData;

    // --- Grid ---
    if (has) {
      float absK = fabsf(pTot) / 1000.0f;
      bool active = fabsf(pTot) >= gridActive;
      const char *dir = pTot > gridActive   ? "Bezug"
                       : pTot < -gridActive ? "Einspeisung"
                                            : "";
      setText(ov.labels[OV_GRID_VAL], "%.2f kW", absK);
      lv_obj_set_style_text_color(ov.labels[OV_GRID_VAL], FLOW_RED, 0);
      setText(ov.labels[OV_GRID_DIR], dir);
      lv_obj_set_style_text_color(ov.labels[OV_GRID_DIR],
                                  active ? FLOW_RED : FLOW_LINE, 0);
      lv_obj_set_style_line_color(s_lineGrid, active ? FLOW_RED : FLOW_LINE, 0);
      lv_obj_set_style_line_width(s_lineGrid, active ? 4 : 3, 0);
      if (active) {
        // import: flow grid -> haus (arrow points left), export: haus -> grid
        lv_label_set_text(ov.labels[OV_GRID_ARROW],
                          pTot > 0 ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT);
        lv_obj_remove_flag(ov.labels[OV_GRID_ARROW], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(ov.labels[OV_GRID_ARROW], LV_OBJ_FLAG_HIDDEN);
      }
    } else {
      lv_label_set_text(ov.labels[OV_GRID_VAL], dash);
      lv_obj_set_style_text_color(ov.labels[OV_GRID_VAL], FLOW_LINE, 0);
      lv_label_set_text(ov.labels[OV_GRID_DIR], "");
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
      lv_obj_set_style_text_color(ov.labels[OV_PV_VAL], FLOW_LINE, 0);
      lv_obj_set_style_line_color(s_linePv, FLOW_LINE, 0);
      lv_obj_set_style_line_width(s_linePv, 2, 0);
      lv_obj_add_flag(ov.labels[OV_PV_ARROW], LV_OBJ_FLAG_HIDDEN);
    }

    // --- Battery (haus <-> batterie) ---
    if (s.haveBattery) {
      setText(ov.labels[OV_BAT_SOC], "%.0f %%", s.batterySoc);
      if (has) {
        float absK = fabsf(pBat) / 1000.0f;
        bool active = fabsf(pBat) >= batActive;
        setText(ov.labels[OV_BAT_VAL], "%.2f kW", absK);
        lv_obj_set_style_text_color(ov.labels[OV_BAT_VAL],
                                    active ? FLOW_RED : FLOW_LINE, 0);
        lv_obj_set_style_line_color(s_lineBat, active ? FLOW_RED : FLOW_LINE, 0);
        lv_obj_set_style_line_width(s_lineBat, active ? 4 : 2, 0);
        if (active) {
          // charging: flow haus -> batterie (down), discharging: up to haus
          lv_label_set_text(ov.labels[OV_BAT_ARROW],
                            pBat > 0 ? LV_SYMBOL_DOWN : LV_SYMBOL_UP);
          lv_obj_remove_flag(ov.labels[OV_BAT_ARROW], LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(ov.labels[OV_BAT_ARROW], LV_OBJ_FLAG_HIDDEN);
        }
      } else {
        lv_label_set_text(ov.labels[OV_BAT_VAL], dash);
        lv_obj_set_style_text_color(ov.labels[OV_BAT_VAL], FLOW_LINE, 0);
        lv_obj_set_style_line_color(s_lineBat, FLOW_LINE, 0);
        lv_obj_set_style_line_width(s_lineBat, 2, 0);
        lv_obj_add_flag(ov.labels[OV_BAT_ARROW], LV_OBJ_FLAG_HIDDEN);
      }
    } else {
      lv_label_set_text(ov.labels[OV_BAT_VAL], dash);
      lv_obj_set_style_text_color(ov.labels[OV_BAT_VAL], FLOW_LINE, 0);
      lv_obj_set_style_line_color(s_lineBat, FLOW_LINE, 0);
      lv_obj_set_style_line_width(s_lineBat, 2, 0);
      lv_obj_add_flag(ov.labels[OV_BAT_ARROW], LV_OBJ_FLAG_HIDDEN);
    }

    // --- Haus (household load measured by the Power Sensor) ---
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
        setText(ov.labels[OV_T_ERZ], "Produzierend");
      } else {
        setText(ov.labels[OV_T_ERZ], "Nicht produzierend");
      }

      if (house >= 50.0f) {
        setText(ov.labels[OV_T_VERB], "Verbrauch");
      } else {
        setText(ov.labels[OV_T_VERB], "Kein Verbrauch");
      }

      if (gridFlowing) {
        setText(ov.labels[OV_T_NETZ], pTot > 0 ? "Bezug" : "Einspeisung");
      } else {
        setText(ov.labels[OV_T_NETZ], "Unabhängig");
      }

      if (s.haveBattery) {
        if (batFlowing) {
          setText(ov.labels[OV_T_BAT], pBat > 0 ? "Laden" : "Entladen");
        } else {
          setText(ov.labels[OV_T_BAT], "Standby");
        }
      } else {
        setText(ov.labels[OV_T_BAT], "keine Batterie");
      }
    } else {
      setText(ov.labels[OV_T_ERZ], dash);
      setText(ov.labels[OV_T_VERB], dash);
      setText(ov.labels[OV_T_NETZ], dash);
      setText(ov.labels[OV_T_BAT], dash);
    }
  }

  AppPage &en = s_pages[PAGE_ENERGY];
  if (en.labels[EN_GEN_VAL]) {
    // Portal "Heute" day counters (all Wh). Eigenverbrauch = PV produced that
    // was not fed into the grid (direct use + battery charge).
    float gen = s.dayPvWh;          // Erzeugt
    float feed = s.dayFeedInWh;     // Eingespeist
    float consumed = s.dayLoadWh;   // Verbrauch (household)
    float gridIn = s.dayGridLoadWh; // Bezug (grid draw)
    float selfUse = gen - feed;
    if (selfUse < 0.0f) selfUse = 0.0f;
    // Autarkie = 1 - Bezug / Verbrauch. No load consumes nothing from the
    // grid, so the day is fully independent.
    float autarkie =
        consumed > 0.0f ? (1.0f - gridIn / consumed) * 100.0f : 100.0f;
    if (autarkie < 0.0f) autarkie = 0.0f;
    float evb = gen > 0.0f ? selfUse / gen * 100.0f : 0.0f; // Eigenverbrauch %

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

  AppPage &inf = s_pages[PAGE_INFO];
  if (inf.labels[INF_HOST]) {
    setText(inf.labels[INF_HOST], "RCT host:  %s", rct_host);
    setText(inf.labels[INF_PORT], "RCT port:  %s", rct_port);
    setText(inf.labels[INF_LINK], "Link:      %s",
            s.connected ? "connected" : "offline");
    setText(inf.labels[INF_LAST], "Last data: %lu s ago",
            s.lastUpdateMs ? (millis() - s.lastUpdateMs) / 1000 : 0UL);
    setText(inf.labels[INF_UPTIME], "Uptime:    %lu s", millis() / 1000);
    setText(inf.labels[INF_L1], "Netz L1:   %6.3f kW", s.gridPower[0] / 1000.0f);
    setText(inf.labels[INF_L2], "Netz L2:   %6.3f kW", s.gridPower[1] / 1000.0f);
    setText(inf.labels[INF_L3], "Netz L3:   %6.3f kW", s.gridPower[2] / 1000.0f);
    float pvTotal = s.pvPower[0] + s.pvPower[1] + s.s0Power;
    float house = s.loadPower[0] + s.loadPower[1] + s.loadPower[2];
    setText(inf.labels[INF_PV], "PV:        %6.3f kW", pvTotal / 1000.0f);
    setText(inf.labels[INF_HOUSE], "Haus:      %6.3f kW", house / 1000.0f);
    setText(inf.labels[INF_SOC], "Bat SOC:   %.0f %%", s.batterySoc);
    setText(inf.labels[INF_BAT], "Bat:       %5.3f kW %+5.1f A %4.1f V",
            s.batteryPower / 1000.0f, s.batteryCurrent, s.batteryVoltage);
  }

  AppPage &dev = s_pages[PAGE_DEVICE];
  if (dev.labels[DEV_NAME]) {
    const char *dash = "--";
    setText(dev.labels[DEV_NAME], "Name:          %s",
            s.deviceName[0] ? s.deviceName : dash);
    setText(dev.labels[DEV_SW], "Software:      %s",
            s.firmwareVersion[0] ? s.firmwareVersion : dash);
    if (s.haveData) {
      setText(dev.labels[DEV_CORE], "Kern:          %.1f °C", s.coreTemp);
      setText(dev.labels[DEV_BTEMP], "Batterie:      %.1f °C", s.batteryTemp);
      setText(dev.labels[DEV_HTEMP], "Kühlkörper:  %.1f °C", s.heatSinkTemp);

      // Next calibration: the inverter reports a Unix timestamp; turn it
      // into a date plus a day countdown once SNTP has a valid wall clock.
      if (s.nextCalibTs) {
        const time_t calib = (time_t)s.nextCalibTs;
        struct tm tmv;
        localtime_r(&calib, &tmv);
        char date[16];
        strftime(date, sizeof(date), "%d.%m.%Y", &tmv);
        const time_t nowT = time(nullptr);
        if (nowT > 1000000000) { // synced (epoch after 2001-09-09)
          const long days = (long)((calib - nowT) / 86400);
          if (days < 0) {
            setText(dev.labels[DEV_CALIB],
                    "Kalibrierung:  %s (überfällig)", date);
          } else if (days == 0) {
            setText(dev.labels[DEV_CALIB], "Kalibrierung:  %s (heute)", date);
          } else {
            setText(dev.labels[DEV_CALIB], "Kalibrierung:  %s (in %ld Tagen)",
                    date, days);
          }
        } else {
          setText(dev.labels[DEV_CALIB], "Kalibrierung:  %s", date);
        }
      } else {
        setText(dev.labels[DEV_CALIB], "Kalibrierung:  --");
      }

      setText(dev.labels[DEV_CYCLES], "Zyklen:        %.0f", s.batteryCycles);
      setText(dev.labels[DEV_FREQ], "Netzfrequenz:  %.2f Hz",
              s.gridFrequency[0]);
      setText(dev.labels[DEV_SOH], "SOH:           %.1f %%", s.batterySoh);
      setText(dev.labels[DEV_ISLAND], "Inselbetrieb:  %s",
              s.islandMode ? "ja" : "nein");
    }
  }

  AppPage &sv = s_pages[PAGE_SERVICE];
  if (sv.labels[SV_BAT_STATUS]) {
    if (!s.haveBattery) {
      lv_label_set_text(sv.labels[SV_BAT_STATUS], "keine Batterie");
      lv_label_set_text(sv.labels[SV_BAT_RAW], "");
    } else if (!s.haveData) {
      lv_label_set_text(sv.labels[SV_BAT_STATUS], "--");
      lv_label_set_text(sv.labels[SV_BAT_RAW], "");
    } else {
      char tmp[48];
      serviceBatteryDecode(s.batteryStatus, tmp, sizeof(tmp));
      lv_label_set_text(sv.labels[SV_BAT_STATUS], tmp);
      setText(sv.labels[SV_BAT_RAW], "Rohwert: 0x%08X", s.batteryStatus);
    }

    if (!s.haveData) {
      lv_label_set_text(sv.labels[SV_FLT_LIST], "--");
    } else {
      char tmp[512];
      int nFlt = serviceFaultText(s.faultBits, tmp, sizeof(tmp));
      lv_label_set_text(sv.labels[SV_FLT_LIST],
                        nFlt > 0 ? tmp : "Keine Störungen");
      lv_obj_set_style_text_color(sv.labels[SV_FLT_LIST],
                                  nFlt > 0 ? COL_TEXT : COL_OK, 0);
    }
  }

  // SD card log status (1 Hz refresh; the writer module keeps it fresh).
  if (sv.labels[SV_SD]) {
    lv_label_set_text(sv.labels[SV_SD], sdStatusText());
  }

  // --- 24 h history: seed once from the SD log, then one sample per 5 min ---
  if (s_chart) {
    if (!s_histSeeded) {
      if (s_histSeedStartMs == 0) {
        s_histSeedStartMs = millis();
      }
      if (sdMounted()) {
        s_histSeeded = true; // (re)mounts later are ignored on purpose
        static SdHistSample seed[HIST_POINTS];
        int n = sdReadHistory(seed, HIST_POINTS);
        if (n > 0) {
          for (int r = 0; r < n; r++) {
            histPush(seed[r].v);
          }
          updateChartRange();
          Serial.printf("hist: %d samples restored from SD log\n", n);
        }
        s_lastHistMs = millis(); // first live sample at the next interval
      } else if (millis() - s_histSeedStartMs >= HIST_SEED_WINDOW_MS) {
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
        v[1] = s.loadPower[0] + s.loadPower[1] + s.loadPower[2]; // Haus
        v[2] = s.pvPower[0] + s.pvPower[1];                      // PV A+B
        v[3] = s.s0Power;                                        // S0
        v[4] = s.batteryPower;                                   // Bat
        histPush(v);
        updateChartRange();
      }
    }
  }
}

// Store one sample in the ring and feed the chart. Ring cursor and the
// chart's per-series cursor advance in lockstep, so replaying restored rows
// through this same call keeps both views identical to a live recording.
static void histPush(const float v[HIST_SERIES]) {
  float *dst = &s_hist[s_histNext * HIST_SERIES];
  for (int i = 0; i < HIST_SERIES; i++) {
    dst[i] = v[i];
    if (s_chart) {
      lv_chart_set_next_value(s_chart, s_chartSer[i], (int32_t)v[i]);
    }
  }
  s_histNext = (s_histNext + 1) % HIST_POINTS;
  if (s_histCount < HIST_POINTS) s_histCount++;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void guiSetup() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, COL_BG, 0);

  // Status bar.
  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_set_size(bar, 480, STATUS_H);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, COL_BAR, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_left(bar, 0, 0);
  lv_obj_set_style_pad_right(bar, 0, 0);
  lv_obj_set_style_pad_top(bar, 0, 0);
  lv_obj_set_style_pad_bottom(bar, 0, 0);
  lv_obj_t *title = lv_label_create(bar);
  lv_label_set_text(title, "RCT Power Panel");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16_uml, 0);
  lv_obj_set_style_text_color(title, COL_TEXT, 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);
  s_statusLabel = lv_label_create(bar);
  lv_label_set_text(s_statusLabel, "boot");
  lv_obj_set_style_text_font(s_statusLabel, &lv_font_montserrat_16_uml, 0);
  lv_obj_align(s_statusLabel, LV_ALIGN_RIGHT_MID, -12, 0);

  // Splash label shown while provisioning happens (blocking).
  s_splashLabel = lv_label_create(scr);
  lv_label_set_text(s_splashLabel, "Starting ...");
  lv_obj_set_style_text_font(s_splashLabel, &lv_font_montserrat_20_uml, 0);
  lv_obj_set_style_text_color(s_splashLabel, COL_TEXT, 0);
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
  lv_obj_set_style_bg_color(content, COL_BG, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_left(content, 0, 0);
  lv_obj_set_style_pad_right(content, 0, 0);
  lv_obj_set_style_pad_top(content, 0, 0);
  lv_obj_set_style_pad_bottom(content, 0, 0);

  // Pages.
  static const char *titles[PAGE_COUNT] = {"Energiefluss", "Heute", "Info",
                                          "Verlauf", "Gerät", "Service"};
  void (*builders[PAGE_COUNT])(AppPage *) = {pageBuildOverview, pageBuildEnergy,
                                             pageBuildInfo, pageBuildGraph,
                                             pageBuildDevice, pageBuildService};
  for (int i = 0; i < PAGE_COUNT; i++) {
    s_pages[i].title = titles[i];
    s_pages[i].labelCount = 0;
    s_pages[i].root = lv_obj_create(content);
    lv_obj_set_size(s_pages[i].root, 480, CONTENT_H);
    lv_obj_set_style_bg_color(s_pages[i].root, COL_BG, 0);
    lv_obj_set_style_border_width(s_pages[i].root, 0, 0);
    lv_obj_set_style_pad_all(s_pages[i].root, 0, 0);
    s_pages[i].build = builders[i];
    for (int k = 0; k < MAX_PAGE_LABELS; k++) {
      s_pages[i].labels[k] = nullptr;
    }
    builders[i](&s_pages[i]);
  }

  // Navigation bar: left | home | right.
  lv_obj_t *nav = lv_obj_create(lv_screen_active());
  lv_obj_set_size(nav, 480, NAV_H);
  lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(nav, COL_BAR, 0);
  lv_obj_set_style_border_width(nav, 0, 0);
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
  // the read callback starts polling; failure only disables touch.
  touchInit();
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchReadCb);

  showPage(PAGE_OVERVIEW);

  // Wi-Fi setup overlay: build hidden; the first refresh tick shows it for
  // the 10 s boot test window (and whenever the provisioning AP is up).
  buildApOverlay();
  s_apTestUntil = millis() + 10000;

  s_refreshTimer = lv_timer_create(refreshCb, 1000, nullptr);
  lv_timer_ready(s_refreshTimer);
}