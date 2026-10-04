// LVGL panel UI: data pages + left/home/right navigation bar.
#ifndef GUI_APP_H
#define GUI_APP_H

#include <lvgl.h>

// Create the base screen (splash). Call after displayInit().
void guiSetup();

// Update the splash text (e.g. WiFi provisioning status).
void guiSetSplashText(const char *text);

// Build the real UI (content pages + navigation bar) and start the
// 1 Hz data refresh timer.
void guiStartApp();

// The switched output changed state - refresh what the Service page shows about
// it right away instead of on the next 1 Hz tick. Called from relayUpdate() in
// loop(), the same task the refresh timer runs in, so no locking is needed.
void guiRelayStateChanged();

// Start a screenshot right now, without the panel's 5 s countdown. The web
// interface triggers it from /bilder, where the page to be photographed is
// already the one on screen - the countdown on the panel exists to leave time
// for navigating there first. Returns false when a capture is already running or
// the card is missing, so the caller can say so instead of starting a second one.
//
// Safe to call from the web handler: that runs in the same task as the GUI
// (the server is pumped from loop()), it only has to be an LVGL-touching
// function, not a different task.
bool guiRequestShot();

// True while a capture is running, or while its image buffer has not been handed
// back yet. The web interface uses this to reload the picture list until the new
// file is in it - the write takes about 1.5 s, and the directory listing is cached
// for up to 5 s, so the file appears a second or two after the button.
bool guiShotRunning();

// The five energy figures of one period, in Wh, plus the two percentages:
//
//   wh[0] pv generation      wh[3] grid draw
//   wh[1] own consumption    wh[4] consumption (incl. external generator)
//   wh[2] grid feed-in
//   *autarky     own consumption as a share of the consumption
//   *ownShare    own consumption as a share of the generation
//
// Same computation the Energie page draws (energyPeriodValues), so the page and
// the web interface cannot report different numbers for the same period.
// period is 0 day, 1 month, 2 year, 3 total.
void guiEnergyPeriod(int period, float wh[5], float *autarky, float *ownShare);

// Set the background theme from outside: hell = true for the light page.
//
// Both halves happen here - the flag and the walk over what already exists - and
// the choice is stored, so it survives the restart that follows a settings save.
// The web interface's settings page uses this, which is why the button on the
// Service page and the form in the browser cannot disagree: they are the same
// two calls, in the same order.
void guiSetTheme(bool hell);

// The background theme currently shown.
bool guiThemeHell();

// The 24 h ring, oldest point first, for the web interface's chart.
// guiHistoryPoints() is how many points the ring holds at the moment (288 once
// it has been full for a day, fewer right after a restart). False from
// guiHistoryPoint() means the point has no sample - the ring starts empty and
// the gaps after a pause stay empty, so the chart can break its line instead of
// inventing a value.
//
// The web handler reads point by point rather than copying the ring into a
// buffer: 288 * 6 floats plus timestamps would be 8 kB of RAM for the sake of
// one request.
int guiHistoryPoints();
bool guiHistoryPoint(int idx, uint32_t *ts, float *w);

#endif // GUI_APP_H