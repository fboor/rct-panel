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

#endif // GUI_APP_H