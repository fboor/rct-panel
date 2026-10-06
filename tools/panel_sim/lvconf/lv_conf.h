// LVGL configuration for the simulator: the panel's own file, with the display
// driver pointed at SDL.
//
// The panel's lv_conf.h is included verbatim, so everything that decides how a
// page looks is the same on both sides: 16-bit colour, 64 kB of LVGL heap, the
// same fonts, the same built-in string functions. Only the window driver differs.
// A simulator with its own colour depth or its own heap would be a second answer
// to questions the panel has already answered.
//
// The three overrides come AFTER the include, not before. The panel's file has an
// unconditional `#define LV_USE_SDL 0`, so a define ahead of it only produces a
// redefinition warning and loses.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_PANEL_SIM_LV_CONF_H
#define RCT_PANEL_SIM_LV_CONF_H

#include "../../../include/lv_conf.h"

// Window, mouse and - for the simulator only - the snapshot API that turns a page
// into a file. The panel needs none of the three and pays for none of them: the
// switch is here, in the simulator's own configuration.
#undef LV_USE_SDL
#define LV_USE_SDL 1
// One buffer, which is what the panel's own driver uses, and DIRECT render mode,
// which writes straight into the window.
#define LV_SDL_BUF_COUNT 1
#define LV_SDL_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT
#undef LV_USE_SNAPSHOT
#define LV_USE_SNAPSHOT 1

#endif // RCT_PANEL_SIM_LV_CONF_H