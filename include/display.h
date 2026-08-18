// LilyGo T-Display-S3 status display

#pragma once

#include <stdint.h>

#ifdef OPTION_DISPLAY

void displayInit(void);

// Write one text line. Line 0 is at the top, displayLineCount()-1 at the bottom.
void displayLine(int32_t line, const char *text, uint16_t color);

int32_t displayLineCount(void);

// Refresh the status page. Called from slowTask.
void displayUpdate(uint32_t slowTaskPeriodMs, uint16_t slowCounter, uint32_t fastTaskPeriodMs, uint16_t fastCounter);

// Show a startup error, so callers do not need to know the display colors.
void displayError(const char *text);

#endif
