// Pressure monitor application: XCP server, measurement tasks, calibration
//
// Build options are set in platformio.ini build_flags:
//   OPTION_DISPLAY  Status output on the LilyGo T-Display-S3 LCD
//   OPTION_IO       Scope trigger pins for both tasks
//   OPTION_ANALOG   ADS1115 pressure sensor input (sine generator when absent)
//   OPTION_MQTT     Periodic publishing of the measurement to an MQTT broker

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Start the XCP server and the measurement tasks. Call once, after WiFi is up.
bool pressureMonitorInit(void);

//----------------------------------------------------------------------------------------------------
// Measurement variables, observable over XCP

// Calibrated pressure in bar, updated in slowTask. NAN when the two-point
// calibration is degenerate.
extern float pressure;

#ifdef OPTION_ANALOG
// Raw pressure sensor voltage in V, updated in slowTask. NAN when no analog
// converter is available.
extern float pressure_sensor_voltage;
#endif

// Note: xcpclient logs "Duplicate instance named ..." for each variable that is
// both declared here and defined in pressure_monitor.cpp, because the DWARF
// data of that translation unit then holds a declaration and a definition. The
// first registration wins and carries the address, so the generated A2L is
// correct. Only declare variables here that other modules really need.
