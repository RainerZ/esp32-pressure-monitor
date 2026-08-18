// ADS1115 analog input
//
// External 4 channel I2C converter at its default address 0x48. The pressure
// sensor is connected to AIN0.

#include "analog.h"

#ifdef OPTION_ANALOG

#include <Adafruit_ADS1X15.h>
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

static constexpr uint8_t ADS1115_I2C_ADDRESS = 0x48;

static Adafruit_ADS1115 ads1115;
static bool ads1115Present = false;

void analogInit(void) {
    Wire.begin(SDA, SCL);

    ads1115Present = ads1115.begin(ADS1115_I2C_ADDRESS, &Wire);
    if (!ads1115Present) {
        Serial.printf("ADS1115 not found at I2C address 0x%02X; using sine signal\n", ADS1115_I2C_ADDRESS);
        return;
    }

    // With a 3.3 V supply, gain 1 covers every valid single-ended input
    ads1115.setGain(GAIN_ONE);
    // slowTask defaults to 500 Hz, so use the ADS1115's fastest rate
    ads1115.setDataRate(RATE_ADS1115_860SPS);
    Serial.printf("ADS1115 found at I2C address 0x%02X (SDA=%u, SCL=%u)\n", ADS1115_I2C_ADDRESS, SDA, SCL);
}

bool analogIsPresent(void) { return ads1115Present; }

float analogReadChannel(uint8_t channel) {
    if (!ads1115Present || channel > 3) {
        return NAN;
    }

    return ads1115.computeVolts(ads1115.readADC_SingleEnded(channel));
}

#endif
