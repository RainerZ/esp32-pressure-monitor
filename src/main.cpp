// ESP32 Pressure Monitor
//
// Reads a pressure sensor through an ADS1115, publishes the calibrated value to
// MQTT once per second, and serves the same signals over XCP on Ethernet for
// real time observation at task rate.
//
// See README.md for wiring, configuration and A2L generation.

#include <Arduino.h>

#include "pressure_monitor.h"
#include "wifi_sta.h"

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

#ifdef OPTION_TEMPERATUR
#include "temperature.h"
#endif

#ifdef OPTION_MQTT
#include "mqtt.h"
#endif

void setup()
{

    Serial.begin(115200);
    delay(2000); // Delay to make sure the serial monitor catches all messages

#ifdef OPTION_DISPLAY
    displayInit();
#endif

#ifdef OPTION_IO
    ioInit();
#endif

#ifdef OPTION_ANALOG
    if (!analogInit())
    {
        printf("Analog init failed\n");
    }
#endif

#ifdef OPTION_TEMPERATUR
    if (!temperatureInit())
    {
        printf("Temperature init failed\n");
    }
#endif

    // XCP and MQTT both need the network
    if (!wifiConnect())
    {
        printf("XCP server not started because WiFi is not connected\n");
#ifdef OPTION_DISPLAY
        displayError("WiFi failed");
#endif
        return;
    }

#ifdef OPTION_MQTT
    if (!mqttInit())
    {
        printf("MQTT init failed\n");
    }
#endif

#ifdef OPTION_XCP
    if (!xcpInit())
    {
        printf("XCP server startup failed\n");
#ifdef OPTION_DISPLAY
        displayError("XCP init failed");
#endif
    }
#endif

    if (!pressureMonitorInit())
    {
        printf("Pressure monitor init failed\n");
#ifdef OPTION_DISPLAY
        displayError("Init failed");
#endif
    }
}

// Everything runs in the XCP server and measurement tasks
void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
