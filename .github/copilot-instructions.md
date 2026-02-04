# ESP32 Firmware Development Instructions

You are an expert ESP32 firmware developer working on Xiaozhi AI Assistant.

## Project Context
- **Platform**: ESP32-S3
- **Framework**: ESP-IDF 5.5
- **RTOS**: FreeRTOS
- **Display**: LVGL 9.x
- **Audio**: Opus codec, ES8311

## Code Structure
```
main/
├── application.cc       # Core state machine
├── protocols/           # MQTT, WebSocket
├── audio/               # Audio processing
├── display/             # LVGL UI
└── boards/              # Hardware configs
```

## Device States
```cpp
enum DeviceState {
    kDeviceStateIdle,       // Waiting for wake word
    kDeviceStateConnecting, // Opening audio channel
    kDeviceStateListening,  // Recording speech
    kDeviceStateSpeaking    // Playing TTS
};
```

## Coding Rules

### 1. Memory Management
```cpp
// ✅ Use PSRAM for large buffers
void* buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);

// ✅ Always free
heap_caps_free(buf);
```

### 2. Thread Safety
```cpp
// ✅ Schedule UI updates to main thread
Schedule([this]() {
    display_->SetChatMessage("user", msg);
});
```

### 3. Error Handling
```cpp
// ✅ Always check errors
esp_err_t err = function();
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
    return err;
}
```

### 4. Logging
```cpp
static const char *TAG = "ModuleName";
ESP_LOGI(TAG, "Info");
ESP_LOGW(TAG, "Warning");
ESP_LOGE(TAG, "Error");
```

## Build Commands
```bash
# Setup
. $HOME/esp/esp-idf/export.sh

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/cu.usbmodem* flash monitor
```

## References
- https://github.com/78/xiaozhi-esp32
- https://docs.espressif.com/projects/esp-idf/
- https://docs.lvgl.io/9.4/
