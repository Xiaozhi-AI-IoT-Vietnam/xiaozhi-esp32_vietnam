#include "mqtt_notification.h"
#include <cJSON.h>
#include <cstring>
#include <esp_log.h>

#define TAG "MQTT_NOTIFY"

MqttNotification &MqttNotification::GetInstance() {
  static MqttNotification instance;
  return instance;
}

MqttNotification::~MqttNotification() { Stop(); }

void MqttNotification::Start(const std::string &broker_url,
                             const std::string &device_mac,
                             const std::string &username,
                             const std::string &password) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (started_) {
    ESP_LOGW(TAG, "MQTT already started, stopping first");
    // Don't call Stop() here as it will deadlock (already holding mutex)
    if (client_) {
      esp_mqtt_client_stop(client_);
      esp_mqtt_client_destroy(client_);
      client_ = nullptr;
    }
    connected_ = false;
    started_ = false;
  }

  if (broker_url.empty()) {
    ESP_LOGW(TAG, "Empty broker URL, MQTT not started");
    return;
  }

  device_mac_ = device_mac;

  // Build subscribe topic: device/{mac}/#
  // The # wildcard allows receiving all sub-topics
  topic_ = "device/" + device_mac + "/#";

  // Ensure broker URL has mqtt:// prefix
  std::string mqtt_uri = broker_url;
  if (mqtt_uri.find("://") == std::string::npos) {
    mqtt_uri = "mqtt://" + mqtt_uri;
    ESP_LOGI(TAG, "Added mqtt:// prefix, URI: %s", mqtt_uri.c_str());
  }

  // Configure MQTT client
  esp_mqtt_client_config_t config = {};
  config.broker.address.uri = mqtt_uri.c_str();

#ifdef CONFIG_MQTT_KEEPALIVE_SECONDS
  config.session.keepalive = CONFIG_MQTT_KEEPALIVE_SECONDS;
#else
  config.session.keepalive = 60;
#endif

#ifdef CONFIG_MQTT_RECONNECT_TIMEOUT_MS
  config.network.reconnect_timeout_ms = CONFIG_MQTT_RECONNECT_TIMEOUT_MS;
#else
  config.network.reconnect_timeout_ms = 5000;
#endif

#ifdef CONFIG_MQTT_BUFFER_SIZE
  config.buffer.size = CONFIG_MQTT_BUFFER_SIZE;
#else
  config.buffer.size = 1024;
#endif
  config.buffer.out_size = 512; // Outgoing buffer (smaller, we mainly receive)

  // Set credentials if provided
  if (!username.empty()) {
    config.credentials.username = username.c_str();
  }
  if (!password.empty()) {
    config.credentials.authentication.password = password.c_str();
  }

  // Initialize client
  client_ = esp_mqtt_client_init(&config);
  if (!client_) {
    ESP_LOGE(TAG, "Failed to initialize MQTT client");
    return;
  }

  // Register event handler
  esp_mqtt_client_register_event(
      client_, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
      mqtt_event_handler, this);

  // Start client
  esp_err_t err = esp_mqtt_client_start(client_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
    esp_mqtt_client_destroy(client_);
    client_ = nullptr;
    return;
  }

  started_ = true;
  ESP_LOGI(TAG, "MQTT client started, connecting to: %s", broker_url.c_str());
  ESP_LOGI(TAG, "Will subscribe to: %s", topic_.c_str());
}

void MqttNotification::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (client_) {
    esp_mqtt_client_stop(client_);
    esp_mqtt_client_destroy(client_);
    client_ = nullptr;
  }

  connected_ = false;
  started_ = false;
  ESP_LOGI(TAG, "MQTT client stopped");
}

bool MqttNotification::IsConnected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return connected_;
}

std::string MqttNotification::GetStatusString() const {
  if (!started_)
    return "Not started";
  if (connected_)
    return "Connected";
  return "Connecting...";
}

void MqttNotification::SetOnNotification(OnNotificationCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_notification_ = std::move(callback);
}

void MqttNotification::SetOnAssetsUpdate(OnAssetsUpdateCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_assets_update_ = std::move(callback);
}

void MqttNotification::mqtt_event_handler(void *handler_args,
                                          esp_event_base_t base,
                                          int32_t event_id, void *event_data) {
  auto *self = static_cast<MqttNotification *>(handler_args);
  auto *event = static_cast<esp_mqtt_event_handle_t>(event_data);

  switch (event_id) {
  case MQTT_EVENT_CONNECTED: {
    ESP_LOGI(TAG, "MQTT connected to broker");

    {
      std::lock_guard<std::mutex> lock(self->mutex_);
      self->connected_ = true;
    }

    // Subscribe to device topic
    int msg_id = esp_mqtt_client_subscribe(event->client, self->topic_.c_str(),
                                           1); // QoS 1
    ESP_LOGI(TAG, "Subscribed to: %s (msg_id=%d)", self->topic_.c_str(),
             msg_id);
    break;
  }

  case MQTT_EVENT_DISCONNECTED: {
    ESP_LOGW(TAG, "MQTT disconnected, will auto-reconnect");

    {
      std::lock_guard<std::mutex> lock(self->mutex_);
      self->connected_ = false;
    }
    break;
  }

  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGI(TAG, "MQTT subscription confirmed (msg_id=%d)", event->msg_id);
    break;

  case MQTT_EVENT_DATA:
    // Received a message
    if (event->topic && event->data) {
      // Create null-terminated strings
      std::string topic(event->topic, event->topic_len);
      std::string data(event->data, event->data_len);

      ESP_LOGI(TAG, "Received message on topic: %s", topic.c_str());
      ESP_LOGD(TAG, "Message data: %s", data.c_str());

      self->HandleMessage(topic.c_str(), data.c_str(), data.length());
    }
    break;

  case MQTT_EVENT_ERROR:
    ESP_LOGE(TAG, "MQTT error occurred");
    if (event->error_handle) {
      if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        ESP_LOGE(TAG, "Transport error: %s",
                 strerror(event->error_handle->esp_transport_sock_errno));
      }
    }
    break;

  default:
    ESP_LOGD(TAG, "MQTT event: %ld", event_id);
    break;
  }
}

void MqttNotification::HandleMessage(const char *topic, const char *data,
                                     int len) {
  // Parse JSON message
  cJSON *root = cJSON_ParseWithLength(data, len);
  if (!root) {
    ESP_LOGW(TAG, "Failed to parse MQTT message as JSON");
    return;
  }

  // Get message type
  cJSON *type = cJSON_GetObjectItem(root, "type");
  if (!cJSON_IsString(type)) {
    ESP_LOGW(TAG, "MQTT message missing 'type' field");
    cJSON_Delete(root);
    return;
  }

  const char *type_str = type->valuestring;

  // Handle different message types
  // Types: notification, reminder, info, warning, alert
  if (strcmp(type_str, "notification") == 0 ||
      strcmp(type_str, "reminder") == 0 || strcmp(type_str, "info") == 0 ||
      strcmp(type_str, "warning") == 0 || strcmp(type_str, "alert") == 0) {
    // Parse notification
    MqttNotificationData notification;
    ParseNotification(root, notification);

    // Call callback on main thread
    OnNotificationCallback callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback = on_notification_;
    }

    if (callback) {
      ESP_LOGI(TAG, "Dispatching notification: %s - %s",
               notification.title.c_str(), notification.content.c_str());
      callback(notification);
    } else {
      ESP_LOGW(TAG, "No notification callback registered");
    }

  } else if (strcmp(type_str, "assets_update") == 0) {
    // Parse assets update
    cJSON *version = cJSON_GetObjectItem(root, "version");
    cJSON *hash = cJSON_GetObjectItem(root, "hash");
    cJSON *url = cJSON_GetObjectItem(root, "download_url");

    OnAssetsUpdateCallback callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback = on_assets_update_;
    }

    if (callback) {
      callback(cJSON_IsString(version) ? version->valuestring : "",
               cJSON_IsString(hash) ? hash->valuestring : "",
               cJSON_IsString(url) ? url->valuestring : "");
    }

  } else if (strcmp(type_str, "intercom") == 0 ||
             strcmp(type_str, "intercom_reply") == 0) {
    // Intercom (Walkie-Talkie) message from another device
    IntercomData intercom;
    ParseIntercom(root, intercom);

    OnIntercomCallback callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback = on_intercom_;
    }

    if (callback) {
      ESP_LOGI(TAG, "Dispatching intercom from %s: %s",
               intercom.from_device_name.c_str(), intercom.message.c_str());
      callback(intercom);
    } else {
      ESP_LOGW(TAG, "No intercom callback registered");
    }

  } else if (strcmp(type_str, "tts") == 0 || strcmp(type_str, "stt") == 0 ||
             strcmp(type_str, "llm") == 0 || strcmp(type_str, "audio") == 0 ||
             strcmp(type_str, "goodbye") == 0 || strcmp(type_str, "mcp") == 0 ||
             strcmp(type_str, "system") == 0 ||
             strcmp(type_str, "hello") == 0) {
    // These message types are handled by MqttProtocol, ignore silently
    ESP_LOGD(TAG, "Ignoring protocol message type: %s", type_str);
  } else {
    ESP_LOGW(TAG, "Unknown MQTT message type: %s", type_str);
  }

  cJSON_Delete(root);
}

void MqttNotification::ParseNotification(const cJSON *root,
                                         MqttNotificationData &notification) {
  cJSON *type = cJSON_GetObjectItem(root, "type");
  cJSON *title = cJSON_GetObjectItem(root, "title");
  cJSON *content = cJSON_GetObjectItem(root, "content");
  cJSON *mess =
      cJSON_GetObjectItem(root, "mess"); // Backend uses 'mess' for TTS content

  notification.type = cJSON_IsString(type) ? type->valuestring : "notification";
  notification.title = cJSON_IsString(title) ? title->valuestring : "";

  // Try 'content' first, then fallback to 'mess'
  if (cJSON_IsString(content) && strlen(content->valuestring) > 0) {
    notification.content = content->valuestring;
  } else if (cJSON_IsString(mess) && strlen(mess->valuestring) > 0) {
    notification.content = mess->valuestring;
  } else {
    notification.content = "";
  }

  // Check notification_type field - if "tts", force enable TTS
  cJSON *notification_type = cJSON_GetObjectItem(root, "notification_type");
  bool force_tts = false;
  if (cJSON_IsString(notification_type)) {
    const char *ntype = notification_type->valuestring;
    if (strcmp(ntype, "tts") == 0 || strcmp(ntype, "voice") == 0 ||
        strcmp(ntype, "speak") == 0) {
      force_tts = true;
      ESP_LOGI(TAG, "notification_type=%s, forcing TTS enabled", ntype);
    }
  }

  // useLLM or useTTS flag for TTS playback
  // Server controls this via "Phát âm thanh (TTS)" toggle
  cJSON *useLLM = cJSON_GetObjectItem(root, "useLLM");
  cJSON *useTTS = cJSON_GetObjectItem(root, "useTTS");

  if (force_tts) {
    // Force TTS if notification_type indicates voice output
    notification.useLLM = true;
  } else if (cJSON_IsBool(useTTS)) {
    notification.useLLM = cJSON_IsTrue(useTTS);
  } else if (cJSON_IsBool(useLLM)) {
    notification.useLLM = cJSON_IsTrue(useLLM);
  } else {
    // Default: enable TTS for notifications/reminders with content
    // This ensures voice playback when user expects it
    notification.useLLM = !notification.content.empty();
  }

  ESP_LOGI(TAG, "ParseNotification: type=%s, force_tts=%d, useLLM=%s",
           notification.type.c_str(), force_tts,
           notification.useLLM ? "true" : "false");

  // Parse extra data if present
  cJSON *extra = cJSON_GetObjectItem(root, "extra");
  if (cJSON_IsObject(extra)) {
    char *extra_str = cJSON_PrintUnformatted(extra);
    if (extra_str) {
      notification.extra = extra_str;
      cJSON_free(extra_str);
    }
  }
}

void MqttNotification::SetOnIntercom(OnIntercomCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_intercom_ = callback;
  ESP_LOGI(TAG, "Intercom callback registered");
}

void MqttNotification::ParseIntercom(const cJSON *root,
                                     IntercomData &intercom) {
  cJSON *type = cJSON_GetObjectItem(root, "type");
  cJSON *from_device_name = cJSON_GetObjectItem(root, "from_device_name");
  cJSON *from_device_id = cJSON_GetObjectItem(root, "from_device_id");
  cJSON *message = cJSON_GetObjectItem(root, "message");
  cJSON *conversation_id = cJSON_GetObjectItem(root, "conversation_id");
  cJSON *reply_to_mac = cJSON_GetObjectItem(root, "reply_to_mac");

  intercom.type = cJSON_IsString(type) ? type->valuestring : "intercom";
  intercom.from_device_name = cJSON_IsString(from_device_name)
                                  ? from_device_name->valuestring
                                  : "thiết bị khác";
  intercom.from_device_id =
      cJSON_IsString(from_device_id) ? from_device_id->valuestring : "";
  intercom.message = cJSON_IsString(message) ? message->valuestring : "";
  intercom.conversation_id =
      cJSON_IsString(conversation_id) ? conversation_id->valuestring : "";
  intercom.reply_to_mac =
      cJSON_IsString(reply_to_mac) ? reply_to_mac->valuestring : "";
  intercom.is_reply = (intercom.type == "intercom_reply");

  ESP_LOGI(TAG, "ParseIntercom: type=%s, from=%s, conversation_id=%s",
           intercom.type.c_str(), intercom.from_device_name.c_str(),
           intercom.conversation_id.c_str());
}
