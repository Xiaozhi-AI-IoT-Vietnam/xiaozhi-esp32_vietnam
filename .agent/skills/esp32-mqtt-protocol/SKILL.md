---
name: ESP32 MQTT Protocol
description: MQTT protocol implementation for ESP32 IoT devices. Covers MQTT client, subscriptions, publish, QoS, and push notifications. Use when working with MQTT communication, server connections, or push notifications.
triggers:
  - mqtt
  - protocol
  - subscribe
  - publish
  - notification
  - push
  - mqtt_protocol
  - topic
---

# ESP32 MQTT Protocol Skill

## Overview
MQTT (Message Queuing Telemetry Transport) implementation for ESP32 using esp-mqtt component.

## ESP-MQTT Client

### Configuration
```cpp
#include "mqtt_client.h"

esp_mqtt_client_config_t config = {
    .broker = {
        .address = {
            .uri = "mqtt://server.com:1883",
        },
    },
    .credentials = {
        .username = "user",
        .authentication = {
            .password = "pass",
        },
    },
    .session = {
        .keepalive = 60,
    },
    .buffer = {
        .size = 4096,
        .out_size = 512,
    },
};

esp_mqtt_client_handle_t client = esp_mqtt_client_init(&config);
```

### Event Handler
```cpp
static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Connected");
        esp_mqtt_client_subscribe(client, "device/+/commands", 1);
        break;
        
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT Disconnected");
        break;
        
    case MQTT_EVENT_DATA:
        // Handle incoming message
        std::string topic(event->topic, event->topic_len);
        std::string data(event->data, event->data_len);
        HandleMessage(topic, data);
        break;
        
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT Error");
        break;
    }
}

// Register handler
esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
esp_mqtt_client_start(client);
```

### Publish
```cpp
// QoS 0 - Fire and forget
esp_mqtt_client_publish(client, "device/status", "online", 0, 0, 0);

// QoS 1 - At least once
esp_mqtt_client_publish(client, "device/data", json_str, 0, 1, 0);

// Retained message
esp_mqtt_client_publish(client, "device/status", "online", 0, 1, 1);
```

### Subscribe
```cpp
// Single topic
esp_mqtt_client_subscribe(client, "device/commands", 1);

// Wildcard subscription
esp_mqtt_client_subscribe(client, "device/+/status", 1);  // Single level
esp_mqtt_client_subscribe(client, "device/#", 1);          // Multi level
```

## Xiaozhi MQTT Protocol Pattern

### Topic Structure
```
device/{mac_address}/           # Device base topic
device/{mac_address}/server     # Server to device messages
device/{mac_address}/client     # Device to server messages
```

### Message Types
```cpp
// Notification message
{
    "type": "notification",    // or "reminder", "info", "warning", "alert"
    "title": "Title",
    "content": "Message content",
    "useTTS": true            // Enable TTS playback
}

// TTS control
{
    "type": "tts",
    "state": "start"          // or "stop", "sentence_start", "sentence_end"
}

// Goodbye (close session)
{
    "type": "goodbye"
}
```

### MqttNotification Class
```cpp
class MqttNotification {
public:
    void Start(const std::string& broker_url, 
               const std::string& device_mac,
               const std::string& username = "",
               const std::string& password = "");
    void Stop();
    void SetOnNotificationCallback(OnNotificationCallback callback);
    
private:
    void HandleMessage(const char* topic, const char* data, int len);
    void ParseNotification(const cJSON* root, MqttNotificationData& notification);
};
```

### Notification Data Structure
```cpp
struct MqttNotificationData {
    std::string type;      // notification, reminder, info, warning, alert
    std::string title;
    std::string content;
    bool useLLM;           // Enable TTS
    std::string extra;     // Additional JSON data
};
```

## UDP Audio Protocol

### Bind Packet (Register UDP address)
```cpp
// After UDP connect, send bind packet
std::string bind_packet;
bind_packet.push_back(0x00);  // Packet type: bind
bind_packet.append(session_id);
udp_->Send(bind_packet);
```

### Audio Packet Format
```
[1 byte: type] [N bytes: payload]

Types:
0x00 - Bind (session_id string)
0x01 - Audio data (Opus frames)
```

## Best Practices

### 1. Connection Management
```cpp
// Auto-reconnect is built-in
// But handle disconnect event for cleanup
case MQTT_EVENT_DISCONNECTED:
    connected_ = false;
    // Clear any pending state
    break;
```

### 2. Thread Safety
```cpp
// Use mutex for shared state
std::lock_guard<std::mutex> lock(mutex_);
callback = on_notification_;
```

### 3. Large Messages
```cpp
// Increase buffer size for large messages
config.buffer.size = 8192;
```

### 4. QoS Selection
- **QoS 0**: Status updates, frequent data
- **QoS 1**: Commands, important notifications
- **QoS 2**: Critical operations (rarely needed)

## References
- ESP-MQTT: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html
- MQTT Spec: https://mqtt.org/mqtt-specification/
