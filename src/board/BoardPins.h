// Which board this firmware is for.
//
// One include, one decision, at compile time - the same shape the language already
// uses (platformio.ini sets one flag per env). Before this file the numbers lived in
// three places that had to agree with each other and none of them knew what board
// it was describing: DisplayPins.h, four constexpr in sdlog.cpp, and RelayPins.h.
//
// WHY A HEADER AND NOT A STRUCT
//
// The call sites say PIN_LCD_HSYNC and RELAY_PIN. A struct would mean rewriting all
// of them to ui().board.lcdHsync, and every one of those edits is a chance to break
// a page that works. The macros keep the names, so the numbers move and the code
// does not. What is bought is the part that mattered: the numbers are now in one
// place per board, and a second board is a second file rather than a second copy of
// this project with digits edited in.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_BOARD_BOARDPINS_H
#define RCT_BOARD_BOARDPINS_H

#if defined(RCT_BOARD_GUITION_4848S040) && defined(RCT_BOARD_WAVESHARE_LCD7)
#error "Two boards selected. One firmware is one board."
#endif

#if defined(RCT_BOARD_WAVESHARE_LCD7)

#include "board/waveshare_lcd7/Pins.h"

#elif defined(RCT_BOARD_GUITION_4848S040)

#include "board/guition4848s040/Pins.h"

#else
// The default keeps every existing build working and keeps the mistake visible:
// without a flag the build stops here rather than shipping the 480 board's pin map
// to a panel it does not belong to.
#error "No board selected. Set -DRCT_BOARD_GUITION_4848S040 or -DRCT_BOARD_WAVESHARE_LCD7."
#endif

#endif // RCT_BOARD_BOARDPINS_H