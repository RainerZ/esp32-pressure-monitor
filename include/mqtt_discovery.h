// Home Assistant MQTT discovery
//
// Announces this node as one device with a pressure sensor, plus temperature
// with OPTION_TEMPERATUR, so Home Assistant creates them without any
// configuration on its side. Also
// maintains an availability topic: "online" (retained) after every connect,
// and "offline" as the last will the broker publishes when the connection is
// lost.
//
// Plain MQTT is unaffected: the measurement topic and payload stay the same.
// Discovery only adds messages on separate topics, which other subscribers
// ignore.
//
// Without OPTION_MQTT_SUBSCRIBE the node only publishes, and the config message
// is retained, so the broker hands it to Home Assistant after every HA restart.
// With OPTION_MQTT_SUBSCRIBE the config is not retained; the node re-announces
// whenever Home Assistant publishes its birth message instead, which leaves no
// stale device behind on the broker when a node is retired.
//
// All functions run in the MQTT publisher task, which owns the client (mqtt.cpp).

#pragma once

#include <PubSubClient.h>

#ifndef MQTT_DISCOVERY_PREFIX
#define MQTT_DISCOVERY_PREFIX "homeassistant"
#endif

#ifndef MQTT_AVAILABILITY_TOPIC
#define MQTT_AVAILABILITY_TOPIC "pressure_monitor/status"
#endif

#ifndef MQTT_DISCOVERY_DEVICE_NAME
#define MQTT_DISCOVERY_DEVICE_NAME "Pressure monitor"
#endif

#define MQTT_AVAILABILITY_ONLINE "online"
#define MQTT_AVAILABILITY_OFFLINE "offline"

// PubSubClient's default 256 byte packet buffer is too small for the config
// message (about 450 bytes, 700 with OPTION_TEMPERATUR). The buffer holds one
// whole packet: header, topic and payload.
#define MQTT_DISCOVERY_BUFFER_SIZE 1024

// Before the first connect: build the config message for this chip and enlarge
// the client's packet buffer. stateTopic is the measurement topic and must stay
// valid.
bool mqttDiscoveryInit(PubSubClient &client, const char *stateTopic);

// After every successful connect: subscribe to the birth message if enabled,
// publish the config and "online".
void mqttDiscoveryConnected(PubSubClient &client);

#ifdef OPTION_MQTT_SUBSCRIBE
// For every received message: re-announces on Home Assistant's birth message
void mqttDiscoveryMessage(PubSubClient &client, const char *topic, const uint8_t *payload, unsigned int length);
#endif
