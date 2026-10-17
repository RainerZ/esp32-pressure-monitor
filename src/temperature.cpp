// DS18B20 1-Wire temperature sensor
//
// A single sensor on its own 1-Wire bus, powered from 3.3 V (not parasite
// power), with a 4.7 kOhm pull-up on the data line. See README.md for wiring.
//
// A 12 bit conversion takes 750 ms, far longer than any measurement task
// period, so a separate low priority task owns the bus: it starts a
// conversion, sleeps until it is done, reads the result and stores it for
// temperatureGet(). The measurement tasks never touch the bus.
//
// The OneWire library disables interrupts for each bit slot (about 70 us).
// That is visible as a small jitter of fastTask and slowTask while a
// conversion result is read, once per period.

#include "temperature.h"

#ifdef OPTION_TEMPERATUR

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#ifndef TEMPERATURE_PIN
#define TEMPERATURE_PIN 21
#endif

// Conversion period. A 12 bit conversion takes 750 ms, so this cannot go lower.
#ifndef TEMPERATURE_PERIOD_MS
#define TEMPERATURE_PERIOD_MS 1000
#endif

static constexpr uint8_t TEMPERATURE_RESOLUTION_BITS = 12;     // 0.0625 degC steps
static constexpr uint32_t TEMPERATURE_RESCAN_PERIOD_MS = 5000; // Retry period while no sensor answers
static constexpr uint32_t TEMPERATURE_TASK_STACK_SIZE = 3072;
static constexpr UBaseType_t TEMPERATURE_TASK_PRIORITY = 1;

static_assert(TEMPERATURE_PERIOD_MS >= 750, "TEMPERATURE_PERIOD_MS must cover the 750 ms conversion time");

static OneWire oneWire(TEMPERATURE_PIN);
static DallasTemperature sensors(&oneWire);
static DeviceAddress sensorAddress;

// Written by the temperature task only. An aligned 32 bit store is atomic on
// the ESP32, so readers in other tasks never see a torn value.
static volatile float latestTemperature = NAN;

// Search the bus for the first DS18B20 and configure it
static bool findSensor(void)
{
    sensors.begin();
    if (!sensors.getAddress(sensorAddress, 0))
    {
        return false;
    }
    sensors.setResolution(sensorAddress, TEMPERATURE_RESOLUTION_BITS);
    // requestTemperatures() returns at once, the task sleeps during the conversion
    sensors.setWaitForConversion(false);
    printf("DS18B20 found on GPIO%u, address %02X%02X%02X%02X%02X%02X%02X%02X\n", TEMPERATURE_PIN, sensorAddress[0], sensorAddress[1], sensorAddress[2],
           sensorAddress[3], sensorAddress[4], sensorAddress[5], sensorAddress[6], sensorAddress[7]);
    return true;
}

static void temperatureTask(void *parameter)
{
    (void)parameter;

    printf("temperatureTask started\n");

    bool present = findSensor();
    if (!present)
    {
        printf("No DS18B20 found on GPIO%u, temperature is null until one answers\n", TEMPERATURE_PIN);
    }

    TickType_t lastWakeTime = xTaskGetTickCount();
    for (;;)
    {
        if (!present)
        {
            latestTemperature = NAN;
            vTaskDelay(pdMS_TO_TICKS(TEMPERATURE_RESCAN_PERIOD_MS));
            present = findSensor();
            lastWakeTime = xTaskGetTickCount();
            continue;
        }

        sensors.requestTemperaturesByAddress(sensorAddress);
        vTaskDelay(pdMS_TO_TICKS(sensors.millisToWaitForConversion(TEMPERATURE_RESOLUTION_BITS)));

        // getTempC() checks the scratchpad CRC and returns DEVICE_DISCONNECTED_C
        // when the sensor does not answer or the data is corrupt
        const float celsius = sensors.getTempC(sensorAddress);
        if (celsius == DEVICE_DISCONNECTED_C)
        {
            printf("DS18B20 not responding\n");
            present = false;
            continue;
        }
        latestTemperature = celsius;

        xTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TEMPERATURE_PERIOD_MS));
    }
}

bool temperatureInit(void)
{
    if (xTaskCreate(temperatureTask, "temperature", TEMPERATURE_TASK_STACK_SIZE, nullptr, TEMPERATURE_TASK_PRIORITY, nullptr) != pdPASS)
    {
        printf("Failed to create temperature task\n");
        return false;
    }
    return true;
}

float temperatureGet(void) { return latestTemperature; }

#endif
