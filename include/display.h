// Pressure display on the LilyGo T-Display-S3
//
// Shows the current pressure in large digits, plus the minimum and maximum
// recorded so far and a bar placing the current value inside that range.
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

void displayInit(void);

// Called at task rate from slowTask; renders at most every DISPLAY_PERIOD_MS.
// Pass NAN for any value that is not available.
void displayUpdate(float pressure, float pressureMin, float pressureMax);

// Show a startup error, so callers do not need to know the display colors.
void displayError(const char *text);

#endif
