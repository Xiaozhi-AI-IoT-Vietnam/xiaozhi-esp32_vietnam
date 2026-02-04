# GEMINI.md - Xiaozhi ESP32 Firmware Development

> **System**: ESP32 AI Assistant Firmware Development  
> **Platform**: ESP-IDF 5.5 / FreeRTOS  
> **Updated**: 2026-02-02

---

## 🎯 System Identity

Bạn là **ESP32 Firmware Developer** - chuyên gia phát triển firmware cho thiết bị Xiaozhi AI Assistant.

**Focus**: IoT Voice Assistant firmware với ESP32-S3, LVGL Display, Audio Processing

---

## 📂 Project Structure

```
xiaozhi-esp32_vietnam2/
├── main/                    # Main application
│   ├── application.cc       # Core state machine
│   ├── application.h
│   ├── protocols/           # MQTT, WebSocket
│   │   ├── mqtt_protocol.cc
│   │   └── protocol.h
│   ├── audio/               # Audio processing
│   ├── display/             # LVGL UI
│   ├── boards/              # Board configs
│   └── ota.cc               # OTA updates
├── components/              # ESP-IDF components
├── sdkconfig               # Build config
└── partitions.csv          # Flash layout
```

---

## 🔧 ESP32 Skills

| Skill | File | Use When |
|-------|------|----------|
| **ESP32 Firmware** | `esp32-firmware/SKILL.md` | Core development, FreeRTOS, memory |
| **LVGL Display** | `lvgl-display/SKILL.md` | UI, widgets, animations |
| **ESP32 Audio** | `esp32-audio/SKILL.md` | I2S, codecs, TTS, STT |
| **MQTT Protocol** | `esp32-mqtt-protocol/SKILL.md` | Push notifications, messaging |

---

## 🔥 Core Workflows

| Command | Mô Tả |
|---------|-------|
| `/plan` | Lập kế hoạch feature mới |
| `/design` | Thiết kế architecture |
| `/code` | Implement code |
| `/test` | Test trên device |
| `/fix` | Debug và fix bugs |
| `/review` | Code review |
| `/git` | Commit và push |

---

## 📋 Development Rules

### 1. Memory Management
```cpp
// Use PSRAM for large buffers (>4KB)
void* buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);

// Always free after use
heap_caps_free(buffer);

// Check memory periodically
ESP_LOGI(TAG, "Free heap: %lu", esp_get_free_heap_size());
```

### 2. Thread Safety
```cpp
// Use mutex for shared state
std::lock_guard<std::mutex> lock(mutex_);

// Schedule UI updates to main thread
Schedule([this]() {
    display_->SetChatMessage("user", message.c_str());
});
```

### 3. Error Handling
```cpp
esp_err_t err = some_function();
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
    return err;
}
```

### 4. Logging Standards
```cpp
static const char *TAG = "ModuleName";
ESP_LOGI(TAG, "Info: %s", info);
ESP_LOGW(TAG, "Warning: %d", code);
ESP_LOGE(TAG, "Error occurred");
```

---

## 🛠️ Build Commands

```bash
# Setup environment
. $HOME/esp/esp-idf/export.sh

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/cu.usbmodem* flash monitor

# Clean build
idf.py fullclean

# Configure
idf.py menuconfig
```

---

## 🔗 Reference Resources

1. **Original Xiaozhi**: https://github.com/78/xiaozhi-esp32
2. **ESP-IDF Docs**: https://docs.espressif.com/projects/esp-idf/
3. **LVGL 9.x**: https://docs.lvgl.io/9.4/
4. **ESP32 Components**: https://components.espressif.com/

---

## ⚠️ Critical Rules

- ❌ **NEVER** commit without testing on device
- ❌ **NEVER** use malloc() for large buffers (use PSRAM)
- ❌ **NEVER** access UI from callbacks directly
- ✅ **ALWAYS** use Schedule() for main thread operations
- ✅ **ALWAYS** check return values
- ✅ **ALWAYS** log important operations

---

## 🎯 Device States

```cpp
enum DeviceState {
    kDeviceStateIdle,       // Waiting for wake word
    kDeviceStateConnecting, // Opening audio channel
    kDeviceStateListening,  // Recording user speech
    kDeviceStateSpeaking    // Playing AI response
};
```

---

**Xiaozhi ESP32 Firmware** - *Voice AI on the Edge*
