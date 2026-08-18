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

#include "xcplib.hpp" // libxcplite C++ application programming interface

#include "clock64.h"
#include "pressure_monitor.h"

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

// XCPlite parameters
#define XCP_PROJECT_NAME "pressure_monitor"
#define XCP_PROJECT_VERSION "V100"
#define XCP_USE_TCP false
#define XCP_SERVER_PORT 5555
#define XCP_QUEUE_SIZE 0 // Fixed by OPTION_QUEUE_32_SIZE in xcplib_rtos_cfg.h for the 32 bit FreeRTOS build, this parameter is ignored
#define XCP_LOG_LEVEL 4  // 3 - Info, 4 - Print XCP commands, 5 - Debug

// #define TASK_CORE 1 // If defined, pin both tasks to this core

#define FASTTASK_PRIORITY (configMAX_PRIORITIES - 1)
#define FASTTASK_STACKSIZE 4096
#define FASTTASK_PERIOD_MIN_MS 1
#define FASTTASK_PERIOD_MAX_MS 100

#define SLOWTASK_PRIORITY 3
#define SLOWTASK_STACKSIZE 4096
#define SLOWTASK_PERIOD_MIN_MS 1
#define SLOWTASK_PERIOD_MAX_MS 1000

// Fallback signal generator, used when no analog converter is available
#define SINE_PHASE_STEP_RAD 0.001f
#define SINE_PERIOD_RAD 6.28318530717958647692f

//----------------------------------------------------------------------------------------------------
// XCP server

static bool startXcpServer(void) {

    XcpSetLogLevel(XCP_LOG_LEVEL);
    XcpCreateEpk(XCP_PROJECT_VERSION);

    // Initialize XCP protocol layer
    const uint8_t bindAny[4] = {0, 0, 0, 0};
    if (!XcpInit(XCP_PROJECT_NAME, XCP_PROJECT_VERSION, XCP_MODE_LOCAL)) {
        printf("XcpInit failed\n");
        return false;
    }

    // Register the high resolution 64-bit clock implemented in clock64.c as XCP DAQ clock
    Clock64_Init();
    ApplXcpRegisterGetClockCallback(Clock64_Get);
    ApplXcpRegisterIdleCallback(Clock64_Update);

    // Initialize XCP Ethernet server
    if (!XcpEthServerInit(bindAny, XCP_SERVER_PORT, XCP_USE_TCP, XCP_QUEUE_SIZE)) {
        printf("XcpEthServerInit failed\n");
        return false;
    }

    return true;
}

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

static uint32_t fastTaskOverruns = 0;
static uint32_t slowTaskOverruns = 0;

//----------------------------------------------------------------------------------------------------
// Calibration parameters

struct parameters {
    uint32_t fast_task_period_ms; // Period of fastTask in milliseconds
    uint32_t slow_task_period_ms; // Period of slowTask in milliseconds
    uint16_t counter_max;         // Wrap-around value for global_counter
    float amplitude;              // Amplitude of the fallback sine generator
    float sensor_voltage_point1;  // Sensor voltage at calibration point 1
    float pressure_point1;        // Pressure at calibration point 1
    float sensor_voltage_point2;  // Sensor voltage at calibration point 2
    float pressure_point2;        // Pressure at calibration point 2
};

XCP_UNIT(parameters__fast_task_period_ms, "ms");
XCP_UNIT(parameters__slow_task_period_ms, "ms");
XCP_UNIT(parameters__amplitude, "bar");
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
    .counter_max = 1000,
    .amplitude = 1.0f,
    .sensor_voltage_point1 = 0.0f,
    .pressure_point1 = 0.0f,
    .sensor_voltage_point2 = 1.0f,
    .pressure_point2 = 1.0f,
};

// Declare a calibration segment that wraps 'parameters' for thread-safe and consistent access.
// This creates:
//  - a linker-section 'xcp_cals' descriptor used by XcpInit() for registration
//  - an internal calibration segment index initialized by XcpInit()
//  - the typed C++ handle 'parameters_calseg' used by the tasks below
CalSegDeclRef(parameters, parameters_calseg);

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

//----------------------------------------------------------------------------------------------------
// Tasks

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
    printf("  frameaddr = %p\n", xcp_get_frame_addr());
    printf("  &counter = %p\n", &counter);
    printf("  &static_counter = %p\n", &static_counter);

    // Create a DAQ event named 'fastTask'
    DaqCreateEvent(fastTask);

    TickType_t lastWakeTime = xTaskGetTickCount();
    for (;;) {

        uint32_t period_ms;

#ifdef OPTION_IO
        setPin1();
#endif

        // Lock the calibration segment 'parameters' for thread-safe and consistent access.
        // There is no blocking mutex held during the lock, only atomics are used.
        {
            auto params = parameters_calseg.lock();

            // Save the task period parameter, don't delay during the lock to give XCP a chance to modify the parameters
            clamp_parameter(period_ms, params->fast_task_period_ms, FASTTASK_PERIOD_MIN_MS, FASTTASK_PERIOD_MAX_MS);

            counter++;
            static_counter++;
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
        DaqTriggerEvent(fastTask);

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

    printf("slowTask started\n");
    printf("  frameaddr = %p\n", xcp_get_frame_addr());
    printf("  &counter = %p\n", &counter);

    TickType_t lastWakeTime = xTaskGetTickCount();
    for (;;) {

#ifdef OPTION_IO
        setPin2();
#endif

        {
            auto params = parameters_calseg.lock();

            clamp_parameter(slow_task_period_ms, params->slow_task_period_ms, SLOWTASK_PERIOD_MIN_MS, SLOWTASK_PERIOD_MAX_MS);
            fast_task_period_ms = params->fast_task_period_ms;

            counter++;
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
            } else
#endif
            {
                pressure = params->amplitude * sinf(phase);
                phase += SINE_PHASE_STEP_RAD;
                if (phase >= SINE_PERIOD_RAD) {
                    phase -= SINE_PERIOD_RAD;
                }
            }
        }

        // Create and trigger the DAQ event 'slowTask'
        DaqCreateAndTriggerEvent(slowTask);

#if defined(OPTION_MQTT) && defined(OPTION_ANALOG)
        // Hand the latest snapshot to the MQTT publisher task. This only formats
        // and queues, the publisher task owns all network operations and the
        // request is dropped when the publish period has not elapsed yet.
        if (!isnan(pressure) && !isnan(pressure_sensor_voltage)) {
            (void)mqttRequestPublish("{\"pressure\":%.6f,\"voltage\":%.6f}", (double)pressure, (double)pressure_sensor_voltage);
        }
#endif

#ifdef OPTION_DISPLAY
        displayUpdate(slow_task_period_ms, counter, fast_task_period_ms, global_counter);
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

    printf("Pressure monitor, XCP on Ethernet\n");
    printf("  Scheduler running = %u\n", xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
    printf("  Timebase 1 ms = %u ticks\n", (unsigned int)pdMS_TO_TICKS(1));
    printf("  &global_counter = %p\n", &global_counter);
    printf("  &parameters = %p\n", &parameters);

    if (!startXcpServer()) {
        printf("XCP server startup failed\n");
        return false;
    }

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
