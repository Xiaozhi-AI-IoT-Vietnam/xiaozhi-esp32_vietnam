#pragma once

#include <functional>
#include <mqtt_client.h>
#include <mutex>
#include <string>

// Forward declaration
struct cJSON;

/**
 * @brief Notification payload structure
 */
struct MqttNotificationData {
  std::string type;    // "notification", "reminder", "assets_update", etc.
  std::string title;   // Title of notification
  std::string content; // Content/message body
  bool useLLM;         // If true, send to server for AI processing + TTS
  std::string extra;   // Optional JSON string for additional data
};

/**
 * @brief MQTT Client for receiving push notifications from server
 *
 * This is a lightweight MQTT client that maintains a persistent connection
 * to the MQTT broker to receive server-initiated messages (reminders, etc.)
 *
 * Usage:
 *   auto& mqtt = MqttNotification::GetInstance();
 *   mqtt.SetOnNotification([](const MqttNotificationData& notification) {
 *       // Handle notification
 *   });
 *   mqtt.Start("mqtt://broker:1883", "AA:BB:CC:DD:EE:FF");
 */
class MqttNotification {
public:
  using OnNotificationCallback =
      std::function<void(const MqttNotificationData &)>;
  using OnAssetsUpdateCallback =
      std::function<void(const std::string &version, const std::string &hash,
                         const std::string &url)>;

  /**
   * @brief Get singleton instance
   */
  static MqttNotification &GetInstance();

  /**
   * @brief Start MQTT client and connect to broker
   * @param broker_url MQTT broker URL (e.g., "mqtt://host:1883")
   * @param device_mac Device MAC address for topic subscription
   * @param username Optional username for authentication
   * @param password Optional password for authentication
   */
  void Start(const std::string &broker_url, const std::string &device_mac,
             const std::string &username = "",
             const std::string &password = "");

  /**
   * @brief Stop MQTT client and disconnect
   */
  void Stop();

  /**
   * @brief Check if MQTT is connected
   */
  bool IsConnected() const;

  /**
   * @brief Check if MQTT client is started (may be connecting)
   */
  bool IsStarted() const { return started_; }

  /**
   * @brief Set callback for notification messages
   */
  void SetOnNotification(OnNotificationCallback callback);

  /**
   * @brief Set callback for assets update messages
   */
  void SetOnAssetsUpdate(OnAssetsUpdateCallback callback);

  /**
   * @brief Get current connection status string
   */
  std::string GetStatusString() const;

private:
  MqttNotification() = default;
  ~MqttNotification();

  // Prevent copying
  MqttNotification(const MqttNotification &) = delete;
  MqttNotification &operator=(const MqttNotification &) = delete;

  // MQTT client handle
  esp_mqtt_client_handle_t client_ = nullptr;

  // Callbacks
  OnNotificationCallback on_notification_;
  OnAssetsUpdateCallback on_assets_update_;

  // State
  std::string topic_;
  std::string device_mac_;
  bool connected_ = false;
  bool started_ = false;

  // Thread safety
  mutable std::mutex mutex_;

  // Event handler
  static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                 int32_t event_id, void *event_data);

  // Message parser
  void HandleMessage(const char *topic, const char *data, int len);
  void ParseNotification(const cJSON *root, MqttNotificationData &notification);
};
