// The one question the theme walk asks about an object.
//
// Header-only and free of LVGL, like DataStatus.h: the decision is a handful of
// comparisons over colours, and it is the decision that has to be right - the
// walk itself is LVGL calls that only have to be written once. Colours are
// plain 24-bit RGB here instead of lv_color_t, so this can be checked on the
// host (tools/theme_test).
//
// The question, in the order it is asked:
//
//   Given an object's background (and whether it has one), the background of
//   the theme being left and the one being entered, its text colour and the
//   text colour being left, what colour does its background and its text get?
//
// Why it is a question and not two if-statements in the walk: the answer
// depends on a comparison, and one comparison in this project was a bug for a
// long time. The node fill was pure white, which is also the light theme's page
// background, so a white node in the light theme compared equal to the page and
// was taken for something sitting on it - and switching to dark repainted it.
// Only the theme button runs the walk, so no screenshot caught it; the panel was
// right after every boot and wrong after every switch.
//
// The rule the fix follows: an object's own surface is identified by NOT being
// the page colour of the theme it was written for. A colour that equals a page
// background cannot serve as a panel, because then it is indistinguishable from
// the page.
//
// SPDX-License-Identifier: MIT
#ifndef RCT_GUI_THEME_H
#define RCT_GUI_THEME_H

#include <stdint.h>

// What the walk decided for one object.
struct ThemeDecision {
  int32_t bg;    // new background, or -1 to leave it alone
  int32_t text;  // new text colour, or -1 to leave it alone
  // False once the object turned out to have a background of its own: nothing
  // below it is on the page any more, so its children are not on the page
  // either. The walk passes this down as `aufBg`.
  bool childAufBg;
};

// The decision. `bgOwn` is false when the object has no background of its own
// (transparent), `bg` its colour otherwise.
inline ThemeDecision themeDecision(bool bgOwn, int32_t bg, int32_t altBg,
                                   int32_t newBg, int32_t text, int32_t altText,
                                   int32_t newText, int32_t altOk,
                                   int32_t newOk, bool aufBg) {
  ThemeDecision d = {-1, -1, aufBg};

  // "A panel of its own": it has a background, and that background is not the
  // page. Everything else follows from this one comparison.
  const bool eigeneFlaeche = bgOwn && bg != altBg;

  if (aufBg && !eigeneFlaeche && bgOwn) {
    d.bg = newBg;
  } else if (eigeneFlaeche) {
    d.childAufBg = false;
  }

  // The text follows by colour, not by place: white on the page is the dark
  // theme's text and becomes the light theme's, and the green follows the theme
  // wherever it stands - on the page and on the cards alike.
  if (aufBg && text == altText) {
    d.text = newText;
  } else if (text == altOk) {
    d.text = newOk;
  }

  return d;
}

#endif // RCT_GUI_THEME_H