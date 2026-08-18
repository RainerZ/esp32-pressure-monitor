// MQTT publisher
//
// The measurement task never performs network operations. It formats a JSON
// snapshot into a one-element mailbox, and a separate low priority task owns
// the broker connection and publishing.

#pragma once

#include <stdbool.h>

#ifndef MQTT_QUEUE_LENGTH
#define MQTT_QUEUE_LENGTH 1
#endif

#ifndef MQTT_PAYLOAD_MAX_LENGTH
#define MQTT_PAYLOAD_MAX_LENGTH 160
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Start the MQTT publisher task. The task owns all network operations.
bool mqttInit(void);

// Format and submit the latest JSON payload without performing network I/O.
// Returns true if the payload was accepted. Requests inside the configured
// publish period or payloads exceeding MQTT_PAYLOAD_MAX_LENGTH are rejected.
bool mqttRequestPublish(const char *format, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif
