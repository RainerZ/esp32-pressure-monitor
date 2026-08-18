// ADS1115 analog input

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef OPTION_ANALOG

// Probe the ADS1115 on I2C. Safe to call when no converter is connected.
void analogInit(void);

// True when the converter answered during analogInit().
bool analogIsPresent(void);

// Read a single-ended channel in volts. Returns NAN when the converter is
// unavailable or the channel is invalid.
float analogReadChannel(uint8_t channel);

#endif
