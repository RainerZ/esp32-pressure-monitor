// WiFi station mode connection

#pragma once

#include <stdbool.h>

// Scan for the configured SSID, connect to the strongest matching AP and wait
// for an IP address. Logs RSSI, channel, encryption and disconnect reasons.
bool wifiConnect(void);
