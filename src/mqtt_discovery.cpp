#ifdef OPTION_MQTT_DISCOVERY

#ifndef OPTION_MQTT
#error "OPTION_MQTT_DISCOVERY requires OPTION_MQTT"
#endif

#include "mqtt_discovery.h"

#include <Arduino.h>
#include <string.h>

// Home Assistant publishes "online" here whenever its MQTT integration starts
#define MQTT_DISCOVERY_BIRTH_TOPIC MQTT_DISCOVERY_PREFIX "/status"

#ifdef OPTION_MQTT_SUBSCRIBE
static constexpr bool CONFIG_RETAINED = false; // re-announced on every birth message
#else
static constexpr bool CONFIG_RETAINED = true; // the broker keeps it for HA restarts
#endif

// One sensor component of the device config. key must match the field name in
// the measurement payload, see pressure_monitor.cpp. The %s is the node id.
// Abbreviated keys keep the message small: p = platform, uniq_id = unique_id,
// dev_cla = device_class, unit_of_meas = unit_of_measurement,
// stat_cla = state_class, sug_dsp_prc = suggested_display_precision,
// val_tpl = value_template.
#define SENSOR_COMPONENT(key, name, device_class, unit, precision)                                 \
    "\"" key "\":{\"p\":\"sensor\",\"name\":\"" name "\",\"uniq_id\":\"%s_" key "\","              \
    "\"dev_cla\":\"" device_class "\",\"unit_of_meas\":\"" unit "\",\"stat_cla\":\"measurement\"," \
    "\"sug_dsp_prc\":" #precision ",\"val_tpl\":\"{{ value_json." key " }}\"}"

// Device based discovery: one message for the device and all its sensors.
// dev = device, ids = identifiers, mdl = model, o = origin (the sending
// software), cmps = components. stat_t = state_topic and avty_t =
// availability_topic sit at device level and are shared by all components.
// Arguments: node id, device name, state topic, availability topic, node id
// once per component.
static const char CONFIG_FORMAT[] =
    "{\"dev\":{\"ids\":\"%s\",\"name\":\"%s\",\"mdl\":\"ESP32-S3\"},"
    "\"o\":{\"name\":\"esp32-pressure-monitor\"},"
    "\"stat_t\":\"%s\",\"avty_t\":\"%s\","
    "\"cmps\":{" SENSOR_COMPONENT("pressure", "Pressure", "pressure", "bar", 3)
#ifdef OPTION_TEMPERATUR
        "," SENSOR_COMPONENT("temperature", "Temperature", "temperature", "\\u00b0C", 1)
#endif
            "}}";

// Built once at init, static because it is too large for the task stack
static char nodeId[32];
static char configTopic[96];
static char configPayload[MQTT_DISCOVERY_BUFFER_SIZE];

static void announce(PubSubClient &client)
{
    if (client.publish(configTopic, configPayload, CONFIG_RETAINED))
    {
        printf("MQTT discovery published to '%s'\n", configTopic);
    }
    else
    {
        printf("MQTT discovery publish failed, state=%d\n", client.state());
    }
    (void)client.publish(MQTT_AVAILABILITY_TOPIC, MQTT_AVAILABILITY_ONLINE, true);
}

bool mqttDiscoveryInit(PubSubClient &client, const char *stateTopic)
{
    // Same chip id as the MQTT client id, so all names of one board match
    const uint64_t chipId = ESP.getEfuseMac();
    snprintf(nodeId, sizeof(nodeId), "pressure_monitor_%06llX", static_cast<unsigned long long>(chipId & 0xFFFFFFULL));
    snprintf(configTopic, sizeof(configTopic), MQTT_DISCOVERY_PREFIX "/device/%s/config", nodeId);

    const int length = snprintf(configPayload, sizeof(configPayload), CONFIG_FORMAT, nodeId, MQTT_DISCOVERY_DEVICE_NAME, stateTopic, MQTT_AVAILABILITY_TOPIC, nodeId
#ifdef OPTION_TEMPERATUR
                                ,
                                nodeId
#endif
    );
    if (length < 0 || static_cast<size_t>(length) >= sizeof(configPayload))
    {
        printf("MQTT discovery config exceeds its buffer\n");
        return false;
    }

    if (!client.setBufferSize(MQTT_DISCOVERY_BUFFER_SIZE))
    {
        printf("MQTT discovery could not allocate the packet buffer\n");
        return false;
    }
    return true;
}

void mqttDiscoveryConnected(PubSubClient &client)
{
#ifdef OPTION_MQTT_SUBSCRIBE
    // Subscriptions do not survive a reconnect, so subscribe on every connect
    if (!client.subscribe(MQTT_DISCOVERY_BIRTH_TOPIC))
    {
        printf("MQTT subscribe to '%s' failed\n", MQTT_DISCOVERY_BIRTH_TOPIC);
    }
#endif
    announce(client);
}

#ifdef OPTION_MQTT_SUBSCRIBE
void mqttDiscoveryMessage(PubSubClient &client, const char *topic, const uint8_t *payload, unsigned int length)
{
    static const char ONLINE[] = "online";
    if (strcmp(topic, MQTT_DISCOVERY_BIRTH_TOPIC) == 0 && length == sizeof(ONLINE) - 1 && memcmp(payload, ONLINE, length) == 0)
    {
        // Publishing from the message callback is safe with PubSubClient: topic
        // and payload are no longer needed, and the message id for a QoS 1
        // acknowledgement is saved before the callback runs
        announce(client);
    }
}
#endif

#endif // OPTION_MQTT_DISCOVERY
