// WiFi station mode connection
//
// The ESP32 only connects to 2.4 GHz networks. The configured SSID is scanned
// first so the strongest matching BSSID can be selected explicitly, which makes
// diagnosing a weak or wrong-band AP much easier.

#include "wifi_sta.h"

#include <Arduino.h>
#include <WiFi.h>

// Credentials come from platformio.ini build flags, or from src/wlan.h
#if !defined(WIFI_SSID) || !defined(WIFI_PASSWORD)
#include "wlan.h"
#endif

#define WIFI_CONNECT_TIMEOUT_MS 30000

struct WiFiTarget {
    bool found;
    int32_t rssi;
    int32_t channel;
    uint8_t bssid[6];
};

static const char *wifiStatusName(wl_status_t status) {
    switch (status) {
    case WL_IDLE_STATUS:
        return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:
        return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:
        return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:
        return "WL_CONNECTED";
    case WL_CONNECT_FAILED:
        return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:
        return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:
        return "WL_DISCONNECTED";
    default:
        return "UNKNOWN";
    }
}

static const char *wifiAuthModeName(wifi_auth_mode_t authMode) {
    switch (authMode) {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2_PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA_WPA2_PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "WPA2_ENTERPRISE";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3_PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2_WPA3_PSK";
    default:
        return "UNKNOWN";
    }
}

static void printBssid(const uint8_t *bssid) { Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]); }

static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        const uint8_t reason = info.wifi_sta_disconnected.reason;
        Serial.printf("WiFi disconnected, reason=%u (%s)\n", reason, WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason)));
    }
}

static WiFiTarget scanForConfiguredSsid(void) {
    WiFiTarget target = {};
    target.rssi = -1000;
    target.channel = 0;

    Serial.printf("Scanning WLANs for '%s'...\n", WIFI_SSID);
    const int networkCount = WiFi.scanNetworks();
    if (networkCount < 0) {
        Serial.printf("WiFi scan failed: %d\n", networkCount);
        return target;
    }

    for (int i = 0; i < networkCount; i++) {
        if (WiFi.SSID(i) == WIFI_SSID) {
            const int32_t rssi = WiFi.RSSI(i);
            const wifi_auth_mode_t authMode = static_cast<wifi_auth_mode_t>(WiFi.encryptionType(i));
            Serial.printf("Found '%s': RSSI=%d dBm, channel=%d, encryption=%d (%s), BSSID=", WiFi.SSID(i).c_str(), rssi, WiFi.channel(i), authMode, wifiAuthModeName(authMode));
            printBssid(WiFi.BSSID(i));
            Serial.println();

            if (!target.found || rssi > target.rssi) {
                target.found = true;
                target.rssi = rssi;
                target.channel = WiFi.channel(i);
                memcpy(target.bssid, WiFi.BSSID(i), sizeof(target.bssid));
            }
        }
    }

    if (!target.found) {
        Serial.printf("SSID '%s' was not found. Check that it is a 2.4 GHz WLAN and in range.\n", WIFI_SSID);
    } else {
        Serial.printf("Using strongest AP: RSSI=%d dBm, channel=%d, BSSID=", target.rssi, target.channel);
        printBssid(target.bssid);
        Serial.println();
    }
    return target;
}

bool wifiConnect(void) {
    WiFi.onEvent(onWiFiEvent);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(true, true);
    delay(250);

    const WiFiTarget target = scanForConfiguredSsid();

    if (target.found) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD, target.channel, target.bssid);
    } else {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }

    Serial.printf("Connecting to WLAN '%s'", WIFI_SSID);
    const uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < WIFI_CONNECT_TIMEOUT_MS) {
        Serial.print(".");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        const wl_status_t status = WiFi.status();
        Serial.printf("WiFi connection failed, status=%d (%s)\n", status, wifiStatusName(status));
        return false;
    }

    Serial.printf("WiFi connected, IP address %s\n", WiFi.localIP().toString().c_str());
    return true;
}
