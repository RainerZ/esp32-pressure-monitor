// DS18B20 1-Wire temperature sensor

#pragma once

#include <stdbool.h>

#ifdef OPTION_TEMPERATUR

// Start the low priority task that probes the 1-Wire bus and runs the
// conversions. Safe to call when no sensor is connected; the task keeps looking
// for one, so a sensor plugged in later is picked up.
bool temperatureInit(void);

// Latest temperature in degrees Celsius. Never blocks: the 750 ms conversion
// and the bus traffic happen in the temperature task. Returns NAN until the
// first valid conversion and while no sensor answers.
float temperatureGet(void);

#endif
