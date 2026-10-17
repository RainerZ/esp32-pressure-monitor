// Pressure display on the LilyGo T-Display-S3
//
// Shows the current pressure in large digits, and a bar placing the current
// value on a fixed scale from DISPLAY_BAR_MIN to DISPLAY_BAR_MAX. With
// OPTION_TEMPERATUR the temperature is shown in a small line at the bottom.
// XCP diagnostics deliberately do not appear here; use CANape or xcpclient for
// those.

#pragma once

#include <stdint.h>

#ifdef OPTION_DISPLAY

// Refresh period. The display is redrawn at most this often, regardless of how
// fast the caller invokes displayUpdate(), which keeps the digits readable
// instead of flickering at task rate.
#ifndef DISPLAY_PERIOD_MS
#define DISPLAY_PERIOD_MS 1000
#endif

// Fixed bar graph scale in bar
#ifndef DISPLAY_BAR_MIN
#define DISPLAY_BAR_MIN 0.0f
#endif
#ifndef DISPLAY_BAR_MAX
#define DISPLAY_BAR_MAX 6.0f
#endif

void displayInit(void);

// Called at task rate from slowTask; renders at most every DISPLAY_PERIOD_MS.
// Pass NAN for a value that is not available. temperature is ignored without
// OPTION_TEMPERATUR.
void displayUpdate(float pressure, float temperature);

// Show a startup error, so callers do not need to know the display colors.
void displayError(const char *text);

#endif
