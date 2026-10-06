// The simulator's fake hardware.
//
// Everything here is something the panel has and this program does not. The list
// is short because the GUI is almost pure: across GuiApp.cpp's 3356 lines it
// reaches outside LVGL for twelve functions, the clock, and Serial. Adding a
// fifteenth fake means something new reached out, and that is worth knowing.
//
// SPDX-License-Identifier: MIT
#include "sim_stubs.h"
#include "sim_data.h"

#include <lvgl.h>

#include <chrono>
#include <cstring>
#include <thread>

#include "../../src/Diag.h"
#include "../../src/config/Configuration.h"
#include "../../src/i18n/Lang.h"
#include "../../src/output/Relay.h"
#include "../../src/web/WebServer.h"

#include "stubs/WiFi.h"
#include "../../src/device/Device.h"
#include "../../src/device/DeviceState.h"
#include "../../src/storage/sdlog.h"

namespace {

using Clock = std::chrono::steady_clock;
Clock::time_point g_start = Clock::now();
Clock::time_point g_letzteFortschreibung = Clock::now();
uint32_t g_virtualMs = 0;   // what millis() reports
int g_speed = 1;
uint32_t g_pollAtMs = 0;
uint32_t g_breite = 480, g_hoehe = 480;
DeviceSemantics g_sem;
void (*g_yieldHook)() = nullptr;
char g_code[8] = {0};

EspClass esp;
WiFiClass WiFi;

}  // namespace

// --- the clock ---------------------------------------------------------------

// One clock, read the same way from everywhere - the panel's rule is that LVGL's
// time and the code's time are the same number, and a second, independent counter
// is how timers come to run fast or stall depending on which was called last.
uint32_t millis() {
  const auto jetzt = Clock::now();
  const auto d = jetzt - g_letzteFortschreibung;
  g_letzteFortschreibung = jetzt;
  g_virtualMs += (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(d).count() *
                 (uint32_t)g_speed;
  return g_virtualMs;
}

uint32_t micros() { return millis() * 1000u; }

void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void delayMicroseconds(uint32_t us) {
  std::this_thread::sleep_for(std::chrono::microseconds(us));
}

void yield() {}

// --- Serial ------------------------------------------------------------------

SimSerial Serial;

void SimSerial::begin(unsigned long) {}
int SimSerial::available() { return 0; }
int SimSerial::read() { return -1; }
size_t SimSerial::write(const uint8_t *buf, size_t n) {
  fwrite(buf, 1, n, stdout);
  return n;
}
size_t SimSerial::print(const char *s) {
  return write(reinterpret_cast<const uint8_t *>(s), strlen(s));
}
size_t SimSerial::print(char c) { return write(reinterpret_cast<const uint8_t *>(&c), 1); }
size_t SimSerial::println(const char *s) { return print(s) + print('\n'); }
size_t SimSerial::println() { return print('\n'); }
size_t SimSerial::printf(const char *fmt, ...) {
  char puffer[512];
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(puffer, sizeof(puffer), fmt, ap);
  va_end(ap);
  if (n > 0) {
    write(reinterpret_cast<const uint8_t *>(puffer), (size_t)n);
  }
  return (size_t)(n > 0 ? n : 0);
}
void SimSerial::flush() { fflush(stdout); }

// --- the device --------------------------------------------------------------
//
// The seven functions Device.h declares, and no more. The state comes from the
// JSON file; the semantics are the RCT's, because that is the plant the panel
// talks to and Rules.h has one code path for it.

const DeviceState &deviceState() { return simDataState(); }

DeviceState &deviceStateMutable() { return simDataState(); }

const DeviceSemantics &deviceSemantics() { return g_sem; }

void deviceSetSemantics(const DeviceSemantics &sem) { g_sem = sem; }

void deviceBegin(const DeviceConfig &) {}

void devicePoll() { simDataPoll(); }

const char *deviceTypeName() { return "RCT"; }

void deviceSetYieldHook(void (*fn)()) { g_yieldHook = fn; }

void deviceYieldHook() {
  if (g_yieldHook != nullptr) {
    g_yieldHook();
  }
}

// --- the display and the touch -----------------------------------------------
//
// The window IS the display: LVGL's SDL driver drew it, so there is nothing left
// to flush and no colour correction to apply. Both answers are said out loud in the
// comment rather than left as an empty function, because a simulator that quietly
// skipped the colour correction would show colours the panel does not - and the
// whole point is that a picture here says something about there.

bool displayInit() { return true; }

void displayLooper() {}   // the SDL window presents itself

lv_display_t *dispGetHandle() { return lv_display_get_default(); }

uint16_t dispCorrectPixel(uint16_t v) { return v; }

bool touchInit() { return true; }

// The panel reads the controller into an LVGL input device. Here the mouse is an
// LVGL input device already (lv_sdl_mouse_create), so there is no controller to
// poll and this callback belongs to the panel's driver alone.
void touchReadCb(lv_indev_t *, lv_indev_data_t *) {}

// --- the backlight -----------------------------------------------------------
//
// At full brightness and never asleep. A simulator that dimmed would make every
// screenshot darker than the panel's, and the colour of a picture is one of the
// things a picture is for.

void backlightInit() {}
void backlightActivity() {}
void backlightSetWakeable(bool) {}
void backlightUpdate() {}
uint8_t backlightPercent() { return 100; }

// --- the SD card -------------------------------------------------------------
//
// No card, and the panel is told so. A stub that claimed a card would put the
// history button on a page where pressing it does nothing.

bool sdMounted() { return false; }

const char *sdStatusText() { return "keine Karte (Simulation)"; }

void sdScreenshot(const uint16_t *, int, int) {}

bool sdTakeShotDone() { return true; }

bool sdShotOk() { return false; }

// No card means no history, and "no history yet" is what a panel with an empty
// card also answers - so the chart page takes the same branch it takes there after
// the first minute rather than a branch of its own.
void sdRequestHistory(int, bool) {}

int sdTakeHistory(SdHistSample *out, int) {
  (void)out;
  return -1;
}

// --- the network -------------------------------------------------------------
//
// Up, and not provisioning. A page that never leaves "connecting" shows the badge
// rather than the layout, and the badge is not what this program is for.

bool networkConnecting() { return false; }

bool provisioningApActive() { return false; }
const char *provisioningApSsid() { return "RCT-Panel"; }
bool provisioningApOpen() { return true; }
void restartProvisioning() {}

// --- the settings ------------------------------------------------------------

char device_type[12] = "RCT";
char device_host[41] = "192.168.1.83";
char device_port[6] = "8899";

// --- the switching output ----------------------------------------------------
//
// Off, and never switching. The output page then shows its idle state, which is
// the state that has to be looked at anyway; a simulator that toggled a relay would
// be pretending to touch a building's wiring.

void relayInit() {}
void relayLoadConfig() {}
void relaySaveConfig() {}
bool relayUpdate() { return false; }
RelayMode relayMode() { return RelayMode::Off; }
void relaySetMode(RelayMode) {}
const char *relayModeName(RelayMode m) {
  switch (m) {
    case RelayMode::Off: return tr(T_RELAY_SHORT_OFF);
    case RelayMode::GridDraw: return tr(T_RELAY_SHORT_GRID);
    case RelayMode::PvSurplus: return tr(T_RELAY_SHORT_SURPLUS);
    case RelayMode::Fault: return tr(T_RELAY_SHORT_FAULT);
    case RelayMode::Island: return tr(T_RELAY_SHORT_ISLAND);
  }
  return "";
}
void relayCycleMode() {}
int relayThreshold() { return 0; }
void relaySetThreshold(int) {}
bool relayIsOn() { return false; }
float relayTriggerValue() { return 0.0f; }
bool relayStartTest() { return false; }
bool relayTestRunning() { return false; }

// --- the web interface -------------------------------------------------------
//
// Not running, because there is no socket here. The maintenance code is still
// there and still printed, so the settings pages can be looked at: on a panel that
// code comes from the boot log, and here it comes from the same place, the terminal
// the program was started in.

bool webRunning() { return false; }

const char *webCode(const char *newCode) {
  if (newCode != nullptr && newCode[0] != '\0') {
    snprintf(g_code, sizeof(g_code), "%s", newCode);
  } else if (g_code[0] == '\0') {
    snprintf(g_code, sizeof(g_code), "%04u", (unsigned)(1000 + (millis() % 9000)));
  }
  printf("Wartungscode: %s\n", g_code);
  return g_code;
}

const char *webNewCode() { return g_code; }

// --- the diagnostics ---------------------------------------------------------

void diagPhase(const char *) {}
void diagStart() {}
void diagMem(const char *) {}

// --- the simulator's own lifecycle -------------------------------------------

void simBegin(int breite, int hoehe, int speed) {
  g_breite = (uint32_t)breite;
  g_hoehe = (uint32_t)hoehe;
  g_speed = (speed > 0) ? speed : 1;
  g_start = Clock::now();
  g_letzteFortschreibung = g_start;
  g_virtualMs = 0;
  printf("Simulation: %u x %u, Daten aus %s, Uhr %dx\n", g_breite, g_hoehe,
         simDatenQuelle(), g_speed);
}

void simSetClockMs(uint32_t ms) {
  g_virtualMs = ms;
  g_letzteFortschreibung = Clock::now();
}

void simPoll() {
  const uint32_t jetzt = millis();
  if (jetzt - g_pollAtMs >= 10000u) {
    g_pollAtMs = jetzt;
    simDataPoll();
  }
}