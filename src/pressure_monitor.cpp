// Pressure monitor application
//
// Two FreeRTOS tasks:
//   fastTask - high priority, default 1 ms, free running counter, shows
//              scheduling jitter and XCP instrumentation cost
//   slowTask - lower priority, default 2 ms, reads the pressure sensor, applies
//              the two-point calibration, feeds the display and MQTT
//
// Both tasks trigger XCP DAQ events, so CANape or xcpclient can observe the
// pressure at task rate while MQTT only carries a snapshot once per second.

#include <math.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "pressure_monitor.h"
#include "xcp.h" // NOP when !defined(OPTION_XCP)

#ifdef OPTION_DISPLAY
#include "display.h"
#endif

#ifdef OPTION_IO
#include "board_io.h"
#endif

#ifdef OPTION_ANALOG
#include "analog.h"
#endif

#ifdef OPTION_MQTT
#include "mqtt.h"
#endif

//----------------------------------------------------------------------------------------------------
// Configuration

// #define TASK_CORE 1 // If defined, pin both tasks to this core

#define FASTTASK_PRIORITY (configMAX_PRIORITIES - 1)
#define FASTTASK_STACKSIZE 4096

#define SLOWTASK_PRIORITY 3
#define SLOWTASK_STACKSIZE 4096

// Power-on default for the MQTT publish period and averaging window.
// This is the value of the calibration default/reference page, so a build flag
// sets what the node starts up with; XCP can still change it at runtime. A
// runtime change is lost on reset, since this build has no persistence.
#ifndef MQTT_PUBLISH_PERIOD_MS
#define MQTT_PUBLISH_PERIOD_MS 1000
#endif

//----------------------------------------------------------------------------------------------------
// Measurement variables

uint16_t global_counter = 0;
XCP_COMMENT(global_counter, "Global measurement variable, incremented in fastTask");

float pressure = 0.0f;
XCP_COMMENT(pressure, "Calibrated pressure, or generated sine wave when no analog converter is present, updated in slowTask");
XCP_UNIT(pressure, "bar");

#ifdef OPTION_ANALOG
float pressure_sensor_voltage = NAN;
XCP_COMMENT(pressure_sensor_voltage, "Raw pressure sensor voltage measured on analog channel 0");
XCP_UNIT(pressure_sensor_voltage, "V");
#endif

// Extremes recorded at task rate, not at display or MQTT rate, so a short
// pressure spike between two publishes is still captured.
float pressure_min = NAN;
XCP_COMMENT(pressure_min, "Lowest pressure recorded since the last reset");
XCP_UNIT(pressure_min, "bar");

float pressure_max = NAN;
XCP_COMMENT(pressure_max, "Highest pressure recorded since the last reset");
XCP_UNIT(pressure_max, "bar");

static uint32_t fastTaskOverruns = 0;
static uint32_t slowTaskOverruns = 0;

#ifdef OPTION_MQTT
// Mean pressure over the last publish interval; this is what MQTT sends.
// Deliberately not declared in pressure_monitor.h: a variable that is both
// declared in a header and defined here registers twice during A2L generation.
float pressure_filtered = NAN;
XCP_COMMENT(pressure_filtered, "Mean pressure over the last MQTT publish interval, as published to MQTT");
XCP_UNIT(pressure_filtered, "bar");

// Number of samples averaged into the last published value
uint32_t pressure_filtered_samples = 0;
XCP_COMMENT(pressure_filtered_samples, "Number of slowTask samples averaged into pressure_filtered");
#endif

//----------------------------------------------------------------------------------------------------
// Calibration parameters

struct parameters {
    uint32_t fast_task_period_ms;   // Period of fastTask in milliseconds
    uint32_t slow_task_period_ms;   // Period of slowTask in milliseconds
    uint32_t mqtt_publish_period_ms; // Averaging window and MQTT publish period in milliseconds
    uint32_t min_max_reset;          // Write any different value to clear pressure_min and pressure_max
    uint16_t counter_max;         // Wrap-around value for global_counter
    float sensor_voltage_point1;  // Sensor voltage at calibration point 1
    float pressure_point1;        // Pressure at calibration point 1
    float sensor_voltage_point2;  // Sensor voltage at calibration point 2
    float pressure_point2;        // Pressure at calibration point 2
};

XCP_UNIT(parameters__fast_task_period_ms, "ms");
XCP_UNIT(parameters__slow_task_period_ms, "ms");
XCP_COMMENT(parameters__mqtt_publish_period_ms, "MQTT publish period, and the averaging window for pressure_filtered");
XCP_UNIT(parameters__mqtt_publish_period_ms, "ms");
XCP_COMMENT(parameters__min_max_reset, "Write any value different from the current one to restart the pressure min/max recording");
XCP_COMMENT(parameters__sensor_voltage_point1, "Pressure sensor voltage at two-point calibration point 1");
XCP_UNIT(parameters__sensor_voltage_point1, "V");
XCP_COMMENT(parameters__pressure_point1, "Pressure at two-point calibration point 1");
XCP_UNIT(parameters__pressure_point1, "bar");
XCP_COMMENT(parameters__sensor_voltage_point2, "Pressure sensor voltage at two-point calibration point 2");
XCP_UNIT(parameters__sensor_voltage_point2, "V");
XCP_COMMENT(parameters__pressure_point2, "Pressure at two-point calibration point 2");
XCP_UNIT(parameters__pressure_point2, "bar");

// Default calibration parameters (default/reference page)
// &parameters is the A2L file address of the calibration parameter segment 'parameters'
// Typename and variable name must be identical for the offline A2L generator
const struct parameters parameters = {
    .fast_task_period_ms = 1, // 1 ms = 1000 Hz
    .slow_task_period_ms = 2, // 2 ms = 500 Hz
    .mqtt_publish_period_ms = MQTT_PUBLISH_PERIOD_MS,
    .min_max_reset = 0,
    .counter_max = 1000,
    .sensor_voltage_point1 = 0.58775f,
    .pressure_point1 = 0.0f,
    .sensor_voltage_point2 = 0.020 * 150 + 0.58775f,
    .pressure_point2 = 10.0f,
};

// Bounds for the calibratable parameters
#define FASTTASK_PERIOD_MIN_MS 1
#define FASTTASK_PERIOD_MAX_MS 100
#define SLOWTASK_PERIOD_MIN_MS 1
#define SLOWTASK_PERIOD_MAX_MS 1000
#define MQTT_PERIOD_MIN_MS 100
#define MQTT_PERIOD_MAX_MS 3600000

// Clamp a calibration parameter to a given value range
#define clamp_parameter(x, y, min, max)                                                                                                                                            \
    do {                                                                                                                                                                           \
        if ((y) < (min))                                                                                                                                                           \
            (x) = (min);                                                                                                                                                           \
        else if ((y) > (max))                                                                                                                                                      \
            (x) = (max);                                                                                                                                                           \
        else                                                                                                                                                                       \
            (x) = (y);                                                                                                                                                             \
    } while (0)



#ifdef OPTION_XCP
// Declare a calibration segment that wraps 'parameters' for thread-safe and consistent access.
// This creates:
//  - a linker-section 'xcp_cals' descriptor used by XcpInit() for registration
//  - an internal calibration segment index initialized by XcpInit()
//  - the typed C++ handle 'parameters_calseg' used by the tasks below
CalSegDeclRef(parameters, parameters_calseg);
#endif


//----------------------------------------------------------------------------------------------------
// Tasks

#ifdef OPTION_MQTT
// Render a float as a JSON number, or as null when it is not available.
// A bare nan is not valid JSON and would be rejected by strict parsers.
static void formatJsonNumber(char *out, size_t len, float value) {
    if (isnan(value)) {
        snprintf(out, len, "null");
    } else {
        snprintf(out, len, "%.6f", (double)value);
    }
}
#endif

static TaskHandle_t fastTaskHandle;
static TaskHandle_t slowTaskHandle;

static bool createTask(TaskFunction_t taskCode, const char *name, const uint32_t stackDepth, UBaseType_t priority, TaskHandle_t *taskHandle) {
#ifdef TASK_CORE
    return (pdPASS == xTaskCreatePinnedToCore(taskCode, name, stackDepth, NULL, priority, taskHandle, TASK_CORE));
#else
    return (pdPASS == xTaskCreate(taskCode, name, stackDepth, NULL, priority, taskHandle));
#endif
}

// High priority fast task
static void fastTask(void *parameter) {
    (void)parameter;

    // Volatile keeps this local measurement variable visible in optimized builds.
    // The offline A2L generator can discover it in the ELF file and associate it to the task's DAQ event.
    volatile uint16_t counter = 0;
    static volatile uint16_t static_counter = 0;

    printf("fastTask started\n");

    // Create a DAQ event named 'fastTask'
#ifdef OPTION_XCP
    DaqCreateEvent(fastTask);
#endif

    TickType_t lastWakeTime = xTaskGetTickCount();
    for (;;) {

        uint32_t period_ms;

#ifdef OPTION_IO
        setPin1();
#endif

        // Lock the calibration segment 'parameters' for thread-safe and consistent access.
        // There is no blocking mutex held during the lock, only atomics are used.
        {
            #ifdef OPTION_XCP
            auto params = parameters_calseg.lock();
            #else
            auto params = &parameters;
            #endif

            // Save the task period parameter, don't delay during the lock to give XCP a chance to modify the parameters
            clamp_parameter(period_ms, params->fast_task_period_ms, FASTTASK_PERIOD_MIN_MS, FASTTASK_PERIOD_MAX_MS);

            // Explicit read-modify-write: ++ on a volatile is deprecated in C++20.
            // The volatile itself is deliberate, it keeps these locals in memory
            // so the offline A2L generator can discover them in the DWARF data.
            counter = counter + 1;
            static_counter = static_counter + 1;
            if (counter > params->counter_max) {
                counter = 0;
                static_counter = 0;
            }
            global_counter++;
            if (global_counter > params->counter_max) {
                global_counter = 0;
            }
        }

        // Trigger the DAQ event 'fastTask'
#ifdef OPTION_XCP
        DaqTriggerEvent(fastTask);
#endif

#ifdef OPTION_IO
        rstPin1();
#endif

        // Sleep until next wakeup time, check for overruns
        const BaseType_t delayed = xTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(period_ms));
        if (delayed == pdFALSE) {
            fastTaskOverruns++;
        }
    }
}

// Low priority slow task
static void slowTask(void *parameter) {
    (void)parameter;

    volatile uint16_t counter = 0;
    float phase = 0.0f;
    uint32_t slow_task_period_ms;
    uint32_t fast_task_period_ms;
    (void)fast_task_period_ms;

    // The calibration segment gives read-only access to the working page, so a
    // self-clearing flag is not possible. Instead any change of min_max_reset
    // restarts the recording, which CANape can trigger by writing a new value.
    uint32_t min_max_reset = 0;
    uint32_t last_min_max_reset = 0;

#ifdef OPTION_MQTT
    // Averaging accumulator for the MQTT publish interval. The sum is a double:
    // a long calibrated interval can accumulate tens of thousands of samples,
    // and on this Xtensa core one software double add per 2 ms task cycle is
    // far below the noise floor of the task timing.
    uint32_t mqtt_publish_period_ms = 0;
    double pressure_sum = 0.0;
    uint32_t pressure_samples = 0;
    TickType_t lastPublishTime = xTaskGetTickCount();
#endif

    printf("slowTask started\n");

    TickType_t lastWakeTime = xTaskGetTickCount();
    for (;;) {

#ifdef OPTION_IO
        setPin2();
#endif

        {
            #ifdef OPTION_XCP
            auto params = parameters_calseg.lock();
            #else
            auto params = &parameters;
            #endif

            clamp_parameter(slow_task_period_ms, params->slow_task_period_ms, SLOWTASK_PERIOD_MIN_MS, SLOWTASK_PERIOD_MAX_MS);
            fast_task_period_ms = params->fast_task_period_ms;
            min_max_reset = params->min_max_reset;
#ifdef OPTION_MQTT
            clamp_parameter(mqtt_publish_period_ms, params->mqtt_publish_period_ms, MQTT_PERIOD_MIN_MS, MQTT_PERIOD_MAX_MS);
#endif

            counter = counter + 1; // ++ on a volatile is deprecated in C++20
            if (counter > params->counter_max) {
                counter = 0;
            }

#ifdef OPTION_ANALOG
            const float voltage = analogReadChannel(0);
            pressure_sensor_voltage = voltage;
            if (!isnan(voltage)) {
                // Two-point calibration, values outside the points are extrapolated
                const float voltageSpan = params->sensor_voltage_point2 - params->sensor_voltage_point1;
                if (voltageSpan != 0.0f) {
                    pressure = params->pressure_point1 + (voltage - params->sensor_voltage_point1) * (params->pressure_point2 - params->pressure_point1) / voltageSpan;
                } else {
                    pressure = NAN; // Degenerate calibration
                }
            } 
#endif
        } // unlock

        if (min_max_reset != last_min_max_reset) {
            last_min_max_reset = min_max_reset;
            pressure_min = NAN;
            pressure_max = NAN;
        }

        // Record the extremes at task rate, so a spike between two publishes is
        // not missed. isnan() comparisons are false, so the first valid sample
        // seeds both bounds.
        if (!isnan(pressure)) {
            if (isnan(pressure_min) || pressure < pressure_min) {
                pressure_min = pressure;
            }
            if (isnan(pressure_max) || pressure > pressure_max) {
                pressure_max = pressure;
            }
        }

        // Create and trigger the DAQ event 'slowTask'
#ifdef OPTION_XCP
        DaqCreateAndTriggerEvent(slowTask);
#endif

#ifdef OPTION_MQTT
        // XCP observes the raw signal at task rate; MQTT carries the mean over
        // the calibrated interval instead, so a slow subscriber sees a filtered
        // value rather than an arbitrary instantaneous sample.
        if (!isnan(pressure)) {
            pressure_sum += (double)pressure;
            pressure_samples++;
        }

        // TickType_t is unsigned, so this comparison survives tick wraparound
        const TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - lastPublishTime) >= pdMS_TO_TICKS(mqtt_publish_period_ms)) {
            lastPublishTime = now;
            if (pressure_samples > 0) {
                pressure_filtered = (float)(pressure_sum / (double)pressure_samples);
                pressure_filtered_samples = pressure_samples;

                // min and max are the extremes recorded since the last reset,
                // the same values the display shows, not the extremes of this
                // interval alone.
                char meanText[16];
                char minText[16];
                char maxText[16];
                formatJsonNumber(meanText, sizeof(meanText), pressure_filtered);
                formatJsonNumber(minText, sizeof(minText), pressure_min);
                formatJsonNumber(maxText, sizeof(maxText), pressure_max);

                // Formats and queues only; the publisher task owns all network I/O
                (void)mqttRequestPublish("{\"pressure\":%s,\"min\":%s,\"max\":%s}", meanText, minText, maxText);
            }
            pressure_sum = 0.0;
            pressure_samples = 0;
        }
#endif

#ifdef OPTION_DISPLAY
        // Called every cycle; the display module rate limits its own rendering
        displayUpdate(pressure, pressure_min, pressure_max);
#endif

#ifdef OPTION_IO
        rstPin2();
#endif

        const BaseType_t delayed = xTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(slow_task_period_ms));
        if (delayed == pdFALSE) {
            slowTaskOverruns++;
        }
    }
}

//----------------------------------------------------------------------------------------------------
// Init

bool pressureMonitorInit(void) {

    printf("Pressure monitor\n");

    if (!createTask(fastTask, "fastTask", FASTTASK_STACKSIZE, FASTTASK_PRIORITY, &fastTaskHandle)) {
        printf("Failed to create fastTask\n");
        return false;
    }

    if (!createTask(slowTask, "slowTask", SLOWTASK_STACKSIZE, SLOWTASK_PRIORITY, &slowTaskHandle)) {
        printf("Failed to create slowTask\n");
        return false;
    }

    return true;
}
