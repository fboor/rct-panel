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

#endif // GUI_APP_H