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
// Returns true if the payload was accepted; payloads exceeding
// MQTT_PAYLOAD_MAX_LENGTH are rejected.
//
// This does not rate limit. The caller decides when to publish, because the
// interval is an XCP calibration parameter (parameters.mqtt_publish_period_ms)
// and doubles as the averaging window. Calling faster simply overwrites the
// mailbox, so the publisher task always sends the newest value.
bool mqttRequestPublish(const char *format, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif
