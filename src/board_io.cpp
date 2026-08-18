// Scope trigger pins
//
// Both demo tasks are pinned high while they run, so their interaction is
// visible on a two channel oscilloscope. Connect both probe grounds to GND.

#include "board_io.h"

#ifdef OPTION_IO

#include <Arduino.h>

#define PIN1 2 // fastTask
#define PIN2 3 // slowTask

void setPin1(void) { digitalWrite(PIN1, HIGH); }

void rstPin1(void) { digitalWrite(PIN1, LOW); }

void setPin2(void) { digitalWrite(PIN2, HIGH); }

void rstPin2(void) { digitalWrite(PIN2, LOW); }

void ioInit(void) {
    pinMode(PIN1, OUTPUT);
    rstPin1();
    pinMode(PIN2, OUTPUT);
    rstPin2();
}

#endif
