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

#ifdef OPTION_XCP
#include "xcp.h"
#endif

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

void setup() {

    Serial.begin(115200);
    delay(500);

#ifdef OPTION_DISPLAY
    displayInit();
#endif

#ifdef OPTION_IO
    ioInit();
#endif

#ifdef OPTION_ANALOG
    analogInit();
#endif

    // XCP and MQTT both need the network
    if (!wifiConnect()) {
        Serial.println("XCP server not started because WiFi is not connected");
#ifdef OPTION_DISPLAY
        displayError("WiFi failed");
#endif
        return;
    }

#ifdef OPTION_MQTT
    if (!mqttInit()) {
        Serial.println("MQTT init failed");
    }
#endif

#ifdef OPTION_XCP
    if (!xcpInit()) {
        printf("XCP server startup failed\n");
#ifdef OPTION_DISPLAY
        displayError("XCP init failed");
#endif
    }
#endif

    if (!pressureMonitorInit()) {
        Serial.println("Pressure monitor init failed");
#ifdef OPTION_DISPLAY
        displayError("Init failed");
#endif
    }
}

// Everything runs in the XCP server and measurement tasks
void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
