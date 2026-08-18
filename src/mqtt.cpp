#include "mqtt.h"

#ifdef OPTION_MQTT

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdarg.h>

#ifndef MQTT_BROKER_HOST
#define MQTT_BROKER_HOST "mqtt.local"
#endif

#ifndef MQTT_BROKER_PORT
#define MQTT_BROKER_PORT 1883
#endif

#ifndef MQTT_TOPIC
#define MQTT_TOPIC "pressure_monitor/measurement"
#endif

static constexpr uint32_t MQTT_RECONNECT_PERIOD_MS = 5000;
static constexpr uint32_t MQTT_TASK_POLL_PERIOD_MS = 100;
static constexpr uint32_t MQTT_TASK_STACK_SIZE = 4096;
static constexpr UBaseType_t MQTT_TASK_PRIORITY = 1;

static_assert(MQTT_QUEUE_LENGTH == 1, "The MQTT queue is a latest-value mailbox and must have length 1");

struct MqttPublishRequest {
    char payload[MQTT_PAYLOAD_MAX_LENGTH + 1];
};

static WiFiClient mqttNetworkClient;
static PubSubClient mqttClient(mqttNetworkClient);
static QueueHandle_t publishQueue = nullptr;

static bool connectMqtt() {
    char clientId[40];
    const uint64_t chipId = ESP.getEfuseMac();
    snprintf(clientId, sizeof(clientId), "pressure-monitor-%06llX", static_cast<unsigned long long>(chipId & 0xFFFFFFULL));

#if defined(MQTT_USERNAME) && defined(MQTT_PASSWORD)
    const bool connected = mqttClient.connect(clientId, MQTT_USERNAME, MQTT_PASSWORD);
#else
    const bool connected = mqttClient.connect(clientId);
#endif

    if (connected) {
        Serial.printf("MQTT connected to %s:%u, topic '%s'\n", MQTT_BROKER_HOST, MQTT_BROKER_PORT, MQTT_TOPIC);
    } else {
        Serial.printf("MQTT connection to %s:%u failed, state=%d\n", MQTT_BROKER_HOST, MQTT_BROKER_PORT, mqttClient.state());
    }
    return connected;
}

static void mqttPublishTask(void *parameter) {
    (void)parameter;

    MqttPublishRequest pendingRequest = {};
    bool publishPending = false;
    uint32_t lastReconnectAttemptMs = millis() - MQTT_RECONNECT_PERIOD_MS;

    for (;;) {
        MqttPublishRequest requestedPublish;
        if (xQueueReceive(publishQueue, &requestedPublish, 0) == pdTRUE) {
            pendingRequest = requestedPublish;
            publishPending = true;
        }

        const uint32_t now = millis();
        if (WiFi.status() == WL_CONNECTED) {
            if (!mqttClient.connected()) {
                if (now - lastReconnectAttemptMs >= MQTT_RECONNECT_PERIOD_MS) {
                    lastReconnectAttemptMs = now;
                    connectMqtt();
                }
            } else {
                mqttClient.loop();

                if (publishPending) {
                    if (mqttClient.publish(MQTT_TOPIC, pendingRequest.payload)) {
                        publishPending = false;
                    } else {
                        Serial.printf("MQTT publish failed, state=%d\n", mqttClient.state());
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MQTT_TASK_POLL_PERIOD_MS));
    }
}

bool mqttInit(void) {
    publishQueue = xQueueCreate(MQTT_QUEUE_LENGTH, sizeof(MqttPublishRequest));
    if (publishQueue == nullptr) {
        Serial.println("Failed to create MQTT publish queue");
        return false;
    }

    mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    mqttClient.setSocketTimeout(2);

    TaskHandle_t taskHandle = nullptr;
    if (xTaskCreate(mqttPublishTask, "mqttPublish", MQTT_TASK_STACK_SIZE, nullptr, MQTT_TASK_PRIORITY, &taskHandle) != pdPASS) {
        vQueueDelete(publishQueue);
        publishQueue = nullptr;
        Serial.println("Failed to create MQTT publish task");
        return false;
    }

    return true;
}

bool mqttRequestPublish(const char *format, ...) {
    if (publishQueue == nullptr || format == nullptr) {
        return false;
    }

    MqttPublishRequest request = {};
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(request.payload, sizeof(request.payload), format, args);
    va_end(args);

    if (length < 0 || static_cast<size_t>(length) >= sizeof(request.payload)) {
        return false;
    }

    return xQueueOverwrite(publishQueue, &request) == pdPASS;
}

#else

bool mqttInit(void) { return false; }

bool mqttRequestPublish(const char *format, ...) {
    (void)format;
    return false;
}

#endif
