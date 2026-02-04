# ESP32 Firmware Quick Reference

## 🛠️ Build Commands

| Command | Description |
|---------|-------------|
| `idf.py build` | Build firmware |
| `idf.py flash monitor` | Flash & monitor |
| `idf.py monitor` | Serial monitor only |
| `idf.py fullclean` | Clean all build files |
| `idf.py menuconfig` | Configure project |
| `idf.py size-components` | Show memory usage |
| `idf.py erase-flash` | Erase entire flash |

## 📝 Logging

```cpp
static const char *TAG = "Module";

ESP_LOGI(TAG, "Info: %s", str);       // Info
ESP_LOGW(TAG, "Warning: %d", code);   // Warning  
ESP_LOGE(TAG, "Error: %s", err);      // Error
ESP_LOGD(TAG, "Debug: %p", ptr);      // Debug
```

## 💾 Memory

```cpp
// PSRAM allocation (for large buffers)
void* buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
heap_caps_free(buf);

// Check memory
esp_get_free_heap_size();               // Free SRAM
heap_caps_get_free_size(MALLOC_CAP_SPIRAM);  // Free PSRAM
esp_get_minimum_free_heap_size();       // Lowest free ever
```

## 🔒 Thread Safety

```cpp
// Mutex
std::lock_guard<std::mutex> lock(mutex_);

// Schedule to main thread
Schedule([this]() {
    display_->SetChatMessage("user", msg);
});
```

## ❌ Error Handling

```cpp
esp_err_t err = function();
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
    return err;
}
```

## 🎯 State Machine

```cpp
enum DeviceState {
    kDeviceStateIdle,        // Waiting
    kDeviceStateConnecting,  // Opening channel
    kDeviceStateListening,   // Recording
    kDeviceStateSpeaking     // TTS playback
};

SetDeviceState(kDeviceStateIdle);
```

## 📺 Display (LVGL)

```cpp
auto display = Board::GetInstance().GetDisplay();

display->SetStatus("Status");
display->SetChatMessage("user", "message");
display->SetChatMessage("assistant", "reply");
display->SetEmotion("happy");
display->ShowNotification("Title");
```

## 📡 MQTT

```cpp
// Subscribe
esp_mqtt_client_subscribe(client, "topic", 1);

// Publish
esp_mqtt_client_publish(client, "topic", data, len, 1, 0);
```

## 📋 JSON (cJSON)

```cpp
// Parse
cJSON* root = cJSON_ParseWithLength(data, len);
cJSON* item = cJSON_GetObjectItem(root, "key");

if (cJSON_IsString(item)) {
    const char* value = item->valuestring;
}
if (cJSON_IsBool(item)) {
    bool value = cJSON_IsTrue(item);
}

cJSON_Delete(root);

// Create
cJSON* obj = cJSON_CreateObject();
cJSON_AddStringToObject(obj, "type", "hello");
char* json = cJSON_PrintUnformatted(obj);
// use json...
cJSON_free(json);
cJSON_Delete(obj);
```

## ⏰ FreeRTOS

```cpp
// Delay
vTaskDelay(pdMS_TO_TICKS(100));  // 100ms

// Create task
xTaskCreate(func, "name", 4096, NULL, 5, &handle);

// Event groups
xEventGroupSetBits(group, BIT0);
xEventGroupWaitBits(group, BIT0, pdTRUE, pdTRUE, portMAX_DELAY);
```

## 🔊 Audio

```cpp
audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
audio_service_.EnableVoiceProcessing(true);
audio_service_.EnableWakeWordDetection(true);
```

## 📁 Key Files

| File | Purpose |
|------|---------|
| `main/application.cc` | Core state machine |
| `main/protocols/mqtt_protocol.cc` | MQTT/WebSocket |
| `main/mqtt_notification.cc` | Push notifications |
| `main/display/lcd_display.cc` | LVGL rendering |
| `main/audio/audio_service.cc` | Audio pipeline |
