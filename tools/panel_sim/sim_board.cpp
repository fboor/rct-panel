// The simulator's own uiLayout().
//
// The firmware gets one board, chosen at compile time: UiLayout.cpp defines
// uiLayout() and the linker keeps it. A build machine that wants a different board
// must not edit that file - it would then be changing the firmware's answer in order
// to look at a second one - so this file provides the same symbol for the
// simulator and picks the board from the size that was asked for.
//
// Which is also why the two profiles live in UiLayout.cpp and not here: both are
// boards, and a board belongs with the boards.
//
// SPDX-License-Identifier: MIT
#include "../../src/ui/UiLayout.h"

#include "sim_stubs.h"

namespace {

// Set from the simulator before the first object is created. Null means "the tree's
// own board", which is the right answer for a firmware build and a wrong one for a
// simulator that was asked for something else - so it is checked there, not here.
const UiLayout *g_board = nullptr;

}  // namespace

const UiLayout &uiLayout() {
  if (g_board != nullptr) {
    return *g_board;
  }
  const UiLayout *eigen = uiLayoutForSize(480, 480);
  return (eigen != nullptr) ? *eigen : *uiLayoutForSize(800, 480);
}

// The transfer between the two: the simulator hands its board in before it starts
// drawing, and this file hands that one back to everything that asks.
void simSetBoard(const UiLayout &board) { g_board = &board; }