// The simulator's entry point: the panel's own GUI in an SDL window.
//
// This file contains no layout. It opens a window, hands the values to the same
// GuiApp.cpp the firmware builds, and runs LVGL's timer. Every page, every
// coordinate and every colour comes from the shipped headers - which is the only
// reason a picture from here says anything about the panel.
//
//   run.sh                         German build, 480 x 480
//   run.sh --lang en               English build
//   run.sh --shot out.png          one frame to a file, then exit
//   run.sh --data other.json       values from another capture
//
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <SDL2/SDL.h>
#include <zlib.h>
#include <lvgl.h>

#include "../../src/device/Device.h"
#include "../../src/device/DeviceDriver.h"
#include "../../src/config/Configuration.h"
#include "../../src/device/DeviceConfig.h"
#include "../../src/gui/GuiApp.h"
#include "../../src/web/WebServer.h"
#include "sim_stubs.h"
#include "../../src/ui/UiLayout.h"

// The same accessor the GUI uses, so the tool and the firmware cannot disagree
// about which board is being drawn.
static inline const UiLayout &ui() { return uiLayout(); }
#include "sim_data.h"

namespace {

struct Optionen {
  const char *shotPath = nullptr;
  const char *dataPath = "data/rct_mock.json";
  int breite = 480;
  int hoehe = 480;
  int speed = 1;
  // Which device the simulator talks to. SIM is the default because that is what a
  // build machine has; RCT and OIG reach a real device over the network, through the
  // shipped TcpTransport and the shipped drivers, so "real data" means the code that
  // talks to an inverter and not a replay of one.
  const char *deviceType = "SIM";
  const char *deviceHost = "";
  const char *devicePort = "";
  // How long to let the panel's own clock run before the frame is taken.
  //
  // The GUI holds the Wi-Fi overlay up for a boot test window of 10 s
  // (GuiApp.cpp: s_apTestUntil = millis() + 10000), and a frame taken inside that
  // window shows the QR page instead of the page that was asked for. The first
  // frame that shows a page was measured at 20 s of the panel's own clock - at
  // 16 s the overlay is still there, at 20 s it is gone - so 20 is the default and
  // not 11.
  //
  // The waiting is a wait and not a skip: nothing here touches the GUI's own timer.
  // With --speed 20, twenty seconds are one second of waiting.
  int nachSekunden = 20;
  // Which page to show, 1..7. Reached by clicking the navigation, because that is
  // what a finger does and because the alternative - a function in GuiApp.cpp that
  // only the simulator calls - would put the simulator into the firmware.
  int seite = 0;
};

// LVGL's clock has exactly one source. On the panel that is millis() advanced by
// the main loop; here the SDL loop is that loop, and it calls the same advance.
uint32_t g_lvLastTick = 0;

void lvAdvance(uint32_t now) {
  const uint32_t d = now - g_lvLastTick;
  if (d > 0) {
    lv_tick_inc(d);
    g_lvLastTick = now;
  }
}

// One frame as a PNG.
//
// PNG and not BMP because a PNG needs no second tool: the panel writes BMP onto
// the card, and a BMP needs a converter afterwards - one more thing between the
// picture and the person looking at it. Here zlib is already linked for SDL.
//
// LVGL's snapshot renders the screen into a buffer we own; the window itself is
// not readable in DIRECT render mode, and reading the draw buffer instead would
// tie this to the render mode.
bool schreibeBild(const char *pfad, int w, int h) {
  std::string buf;
  buf.resize((size_t)w * (size_t)h * 2u);

  lv_image_dsc_t dsc = {};
  const lv_result_t r = lv_snapshot_take_to_buf(lv_screen_active(),
                                                LV_COLOR_FORMAT_RGB565, &dsc,
                                                buf.data(),
                                                (uint32_t)buf.size());
  if (r != LV_RESULT_OK) {
    fprintf(stderr, "Snapshot misslungen\n");
    return false;
  }

  // RGB565 -> 8 bit per channel, one PNG filter byte (0 = none) per row. The high
  // bits are the ones that matter, and the panel's own screenshot widens the same
  // way, so the two pictures carry the same colours.
  const uint8_t *rgb565 = reinterpret_cast<const uint8_t *>(dsc.data);
  std::string roh;
  roh.reserve((size_t)h * (1u + (size_t)w * 3u));
  for (int y = 0; y < h; y++) {
    roh.push_back((char)0);
    for (int x = 0; x < w; x++) {
      const size_t si = ((size_t)y * (size_t)w + (size_t)x) * 2u;
      const uint16_t v = (uint16_t)(rgb565[si] | ((uint16_t)rgb565[si + 1] << 8));
      roh.push_back((char)(((v >> 11) & 0x1F) * 255u / 31u));
      roh.push_back((char)(((v >> 5) & 0x3F) * 255u / 63u));
      roh.push_back((char)((v & 0x1F) * 255u / 31u));
    }
  }
  lv_snapshot_free(&dsc);

  uLongf zielGroesse = compressBound((uLong)roh.size());
  std::string z;
  z.resize(zielGroesse);
  if (compress2(reinterpret_cast<Bytef *>(&z[0]), &zielGroesse,
                reinterpret_cast<const Bytef *>(roh.data()), (uLong)roh.size(),
                6) != Z_OK) {
    fprintf(stderr, "PNG-Kompression misslungen\n");
    return false;
  }
  z.resize(zielGroesse);

  FILE *fp = fopen(pfad, "wb");
  if (fp == nullptr) {
    fprintf(stderr, "Bilddatei nicht schreibbar: %s\n", pfad);
    return false;
  }
  // One PNG chunk: length, type, data, CRC - in that order. The CRC covers the
  // type and the data and NOT the length, which is the one detail that makes every
  // chunk wrong at once while the file still looks like a PNG.
  auto put32 = [](uint32_t v, std::string &out) {
    out.push_back((char)(v >> 24));
    out.push_back((char)(v >> 16));
    out.push_back((char)(v >> 8));
    out.push_back((char)v);
  };
  auto chunk = [&](const char *typ, const void *daten, size_t n) {
    std::string koerper(typ);
    koerper.append(static_cast<const char *>(daten), n);
    std::string c;
    put32((uint32_t)n, c);
    c += koerper;
    put32((uint32_t)(::crc32(0, (const Bytef *)koerper.c_str(),
                             (uInt)koerper.size()) &
                     0xFFFFFFFFu),
          c);
    fwrite(c.data(), 1, c.size(), fp);
  };

  static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  fwrite(sig, 1, sizeof(sig), fp);

  // IHDR: width, height, 8 bit per channel, truecolour, no interlacing.
  {
    std::string ihdr;
    put32((uint32_t)w, ihdr);
    put32((uint32_t)h, ihdr);
    const unsigned char rest[5] = {8, 2, 0, 0, 0};
    ihdr.append(reinterpret_cast<const char *>(rest), sizeof(rest));
    chunk("IHDR", ihdr.data(), ihdr.size());
  }
  chunk("IDAT", z.data(), z.size());
  chunk("IEND", "", 0);
  fclose(fp);
  return true;
}

}  // namespace

// Where the three navigation buttons are, DERIVED from the board rather than
// written down: three equal cells across the bar, so their centres are at the sixths
// of the width, and vertically in the middle of the bar.
//
// They were 80, 240 and 390 at y = 442 - measured on the frame, not read out of the
// source - and those numbers only held at 480 px. At 800 px the click at 390 landed
// in the middle button, so --page 3 showed the overview and --page 5 the same page
// again: the screenshot tool was walking to a place that had moved.
namespace {
int navButtonX(int k) { return ui().screenW * (1 + 2 * k) / 6; }
int navButtonY() { return ui().screenH - ui().navH / 2; }
bool g_quit = false;

int SDLCALL quitWatch(void *, SDL_Event *ev) {
  if (ev->type == SDL_QUIT) {
    g_quit = true;
  }
  return 1;
}

// One press and release at a point, pushed onto SDL's queue. LVGL's SDL driver
// drains that queue from its own timer and hands the events to its mouse, so the
// panel's own touch handling runs - the whole point of pushing real events rather
// than calling a page-switch function.
void bewege(int x, int y) {
  SDL_Event m;
  SDL_memset(&m, 0, sizeof(m));
  m.type = SDL_MOUSEMOTION;
  m.motion.x = x;
  m.motion.y = y;
  m.motion.xrel = 0;
  m.motion.yrel = 0;
  SDL_PushEvent(&m);
}

void klicke(int x, int y) {
  SDL_Event m;
  SDL_memset(&m, 0, sizeof(m));
  m.type = SDL_MOUSEMOTION;
  m.motion.x = x;
  m.motion.y = y;
  m.motion.xrel = 0;
  m.motion.yrel = 0;
  SDL_PushEvent(&m);

  SDL_Event d;
  SDL_memset(&d, 0, sizeof(d));
  d.type = SDL_MOUSEBUTTONDOWN;
  d.button.button = SDL_BUTTON_LEFT;
  d.button.x = x;
  d.button.y = y;
  d.button.clicks = 1;
  SDL_PushEvent(&d);

  SDL_Event u;
  SDL_memset(&u, 0, sizeof(u));
  u.type = SDL_MOUSEBUTTONUP;
  u.button.button = SDL_BUTTON_LEFT;
  u.button.x = x;
  u.button.y = y;
  u.button.clicks = 1;
  SDL_PushEvent(&u);
}
}  // namespace

int main(int argc, char **argv) {
  Optionen opt;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if ((a == "--shot") && i + 1 < argc) {
      opt.shotPath = argv[++i];
    } else if ((a == "--data") && i + 1 < argc) {
      opt.dataPath = argv[++i];
    } else if (a == "--lang") {
      // Accepted and ignored: the language is chosen when the program is compiled,
      // not when it is started. run.sh reads the same flag and adds -DRCT_LANG_EN.
      if (i + 1 < argc) i++;   // the language name, whichever it is
    } else if ((a == "--device") && i + 1 < argc) {
      opt.deviceType = argv[++i];
    } else if ((a == "--host") && i + 1 < argc) {
      opt.deviceHost = argv[++i];
    } else if ((a == "--port") && i + 1 < argc) {
      opt.devicePort = argv[++i];
    } else if (a == "--page") {
      opt.seite = (i + 1 < argc) ? atoi(argv[++i]) : 1;
    } else if (a == "--speed") {
      opt.speed = (i + 1 < argc) ? atoi(argv[++i]) : 20;
    } else if (a == "--after") {
      opt.nachSekunden = (i + 1 < argc) ? atoi(argv[++i]) : 11;
    } else if ((a == "--size") && i + 1 < argc) {
      if (sscanf(argv[++i], "%dx%d", &opt.breite, &opt.hoehe) != 2) {
        fprintf(stderr, "--size erwartet 800x480\n");
        return 2;
      }
    } else if (a == "--help") {
      printf("panel_sim [--shot out.png] [--data file.json] [--size WxH] "
             "[--page 1..7] [--speed N] [--after s]\n");
      return 0;
    } else {
      fprintf(stderr, "unbekanntes Argument: %s\n", a.c_str());
      return 2;
    }
  }

  // The window's size is the board's size. The simulator asks the tree which board
  // it wants for these dimensions and says so plainly if there is none - drawing at
  // 480 x 480 inside an 800 x 480 window would look like a working second
  // resolution and be nothing of the kind.
  const UiLayout *board = uiLayoutForSize(opt.breite, opt.hoehe);
  if (board == nullptr) {
    fprintf(stderr, "Kein Boardprofil fuer %d x %d in diesem Baum.\n", opt.breite,
            opt.hoehe);
    return 2;
  }
  simSetBoard(*board);

  // The settings, built the way main.cpp builds them from the stored configuration.
  // Without this the device layer never starts and every page says "keine Daten" -
  // which is what happened the first time this file was called at all.
  DeviceConfig cfg;
  snprintf(cfg.type, sizeof(cfg.type), "%s", opt.deviceType);
  snprintf(cfg.host, sizeof(cfg.host), "%s", opt.deviceHost);
  snprintf(cfg.port, sizeof(cfg.port), "%s", opt.devicePort);
  if (cfg.port[0] == '\0') {
    snprintf(cfg.port, sizeof(cfg.port), "%s", deviceDefaultPort(cfg.type));
  }
  printf("Geraet: Typ %s, Adresse %s, Port %s\n", cfg.type,
         cfg.host[0] ? cfg.host : "-", cfg.port);

  simDataLoad(opt.dataPath);
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }

  // lv_init before the display: that is the order LVGL's own desktop example
  // uses, and the display takes its allocator from an initialised LVGL.
  lv_init();
  lv_display_t *disp = lv_sdl_window_create(opt.breite, opt.hoehe);
  if (disp == nullptr) {
    fprintf(stderr, "lv_sdl_window_create: kein Fenster\n");
    return 1;
  }
  lv_sdl_window_set_title(disp, "rct-panel (simulation)");
  lv_sdl_mouse_create();

  // A watch, and not SDL_PollEvent in the loop: LVGL's SDL driver drains the
  // queue itself from an LVGL timer, and a second reader would take the mouse
  // events away from it - the window would then stop reacting and the reason
  // would not be near this line.
  SDL_AddEventWatch(quitWatch, nullptr);

  simBegin(opt.breite, opt.hoehe, opt.speed);

  // The yield hook the panel installs before the first poll, so that a driver which
  // blocks keeps the display alive. The simulator's is the LVGL clock and the
  // handler, which is the same thing the panel's is.
  deviceSetYieldHook([]() {
    lvAdvance(millis());
    lv_timer_handler();
  });
  // Through the same door the settings page uses, so that a switch from the browser
  // and a switch at startup are the same code - the first version called deviceBegin
  // here and webDeviceSwitch from the browser, which were two ways of doing one thing.
  snprintf(device_type, sizeof(device_type), "%s", cfg.type);
  snprintf(device_host, sizeof(device_host), "%s", cfg.host);
  snprintf(device_port, sizeof(device_port), "%s", cfg.port);
  webDeviceSwitch();

  // The panel's own web interface, on the loopback interface and on 8081 rather than
  // 80 (src/web/WebServer.cpp decides which under PANEL_SIM). Started before the
  // GUI so that the first browser request finds it.
  webStart();

  guiSetup();
  guiStartApp();

  // The clicks, spread out in the panel's own clock so that each one has been
  // processed before the next arrives, and the picture waits 1.5 s of panel time after the
// last one: a page switch redraws, and half a second is not always enough frames.
// Page 1 is the overview, which is where the
  // panel comes up.
  const uint32_t klickAbMs = 21000;
  std::vector<std::pair<int, int>> klicks;
  if (opt.seite > 0) {
    const int ziel = opt.seite;
    if (ziel == 1) {
      klicke(navButtonX(1), navButtonY());
    } else if (ziel > 1) {
      for (int i = 0; i < ziel - 1; i++) {
        klicks.push_back({navButtonX(2), navButtonY()});
      }
    }
  }
  size_t naechsterKlick = 0;
  uint32_t klickUmMs = klickAbMs;

  uint32_t bilder = 0;
  bool aus = false;
  while (!aus) {
    lvAdvance(millis());

    // The poll the firmware makes every 10 s, out of the same file.
    simPoll();

    // The web interface runs on its own tick, exactly as the panel runs it: a
    // request is served inside one LVGL pass, so a slow download cannot stop the
    // display and the display cannot stop a download.
    webUpdate();

    uint32_t warteMs = lv_timer_handler();
    if (warteMs > 20) {
      warteMs = 20;
    }
    SDL_Delay(warteMs);

    const uint32_t jetzt2 = millis();
    if (naechsterKlick < klicks.size() && jetzt2 >= klickUmMs) {
      klicke(klicks[naechsterKlick].first, klicks[naechsterKlick].second);
      naechsterKlick++;
      klickUmMs += 300;
      if (opt.shotPath != nullptr) {
        opt.nachSekunden = (int)((klickUmMs + 1500) / 1000);
      }
    }

    // The picture is taken once the panel's own clock has run out the overlay
    // window and every click has been processed - which is the panel's decision,
    // not this program's, so the waiting is a wait and not a skip.
    const bool klicksFertig = naechsterKlick >= klicks.size();
    const bool warteNoch =
        opt.shotPath != nullptr &&
        (!klicksFertig || (int)(jetzt2 / 1000) < opt.nachSekunden);
    if (!warteNoch) {
      if (opt.shotPath != nullptr) {
        // The clock, pinned to the moment the picture was asked for - see
        // simSetClockMs(). Two runs of the same build then produce the same file,
        // which is what makes a before-and-after comparison of a refactor mean
        // anything.
        // Settle deterministically: the pointer has to leave the navigation bar,
        // and the buttons have to finish coming back from being pressed.
        //
        // Both are done on the panel's own clock in fixed steps rather than on the
        // wall clock, and that is the whole point. Driven by SDL_Delay the settle
        // took a different number of steps on every run - the navigation bar came
        // out in 9000 slightly different pixels each time, which is not a layout
        // difference and would have been read as one. Twenty steps of 50 ms is
        // enough for both and is the same sixty steps every time. Twenty was not:
        // pages that carry a lot of labels - the energy page and the device page -
        // still came out in 9000 differing pixels, and the whole width of the
        // navigation bar was among them, which is what an unfinished layout pass
        // looks like rather than a button that has not finished coming back.
        const uint32_t zielMs = (uint32_t)opt.nachSekunden * 1000u;
        for (int i = 0; i < 60; i++) {   // 3000 ms of panel time
          simSetClockMs(zielMs + (uint32_t)(i + 1) * 50u);
          bewege(10, 300);
          lv_tick_inc(50);
          lv_timer_handler();
          SDL_Delay(4);
        }
        const bool ok = schreibeBild(opt.shotPath, opt.breite, opt.hoehe);
        if (!ok) {
          SDL_Quit();
          return 1;
        }
        printf("Bild: %s (%d x %d, Seite %d)\n", opt.shotPath, opt.breite,
               opt.hoehe, opt.seite > 0 ? opt.seite : 1);
        aus = true;
        continue;
      }
    }

    if (g_quit) {
      aus = true;
    }
  }

  webStop();
  SDL_Quit();
  return 0;
}