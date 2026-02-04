# Xiaozhi ESP32 Firmware - Architecture

> Voice AI Assistant on ESP32-S3

## 📋 Overview

| Component | Technology |
|-----------|------------|
| **MCU** | ESP32-S3 (Dual-core 240MHz) |
| **Framework** | ESP-IDF 5.5 |
| **RTOS** | FreeRTOS |
| **Display** | LVGL 9.x |
| **Audio** | Opus, ES8311 codec |
| **Network** | WiFi, MQTT, WebSocket |

---

## 🏗️ Project Structure

```
xiaozhi-esp32_vietnam2/
├── main/
│   ├── application.cc       # 🎯 Core state machine
│   ├── protocols/           # 📡 MQTT, WebSocket
│   ├── audio/               # 🔊 Audio pipeline
│   ├── display/             # 📺 LVGL UI
│   ├── boards/              # 🔧 Hardware configs
│   ├── mqtt_notification.cc # 📬 Push notifications
│   └── ota.cc               # 🔄 OTA updates
├── components/              # ESP-IDF libraries
├── sdkconfig               # Build config
└── GEMINI.md               # AI config
```

---

## 🔧 Skills (5)

| Skill | Purpose |
|-------|---------|
| `esp32-firmware` | Core ESP-IDF, FreeRTOS, memory |
| `esp32-audio` | I2S, Opus, codecs, wake word |
| `lvgl-display` | Graphics, widgets, styles |
| `esp32-mqtt-protocol` | MQTT client, notifications |
| `xiaozhi-patterns` | Application state, callbacks |

---

## 🔄 Workflows (7)

| Command | Action |
|---------|--------|
| `/plan` | Design feature |
| `/design` | Architecture |
| `/code` | Implement |
| `/test` | Build & flash |
| `/review` | Code review |
| `/fix` | Debug issues |
| `/git` | Commit & push |

---

## 🎯 State Machine

```
    ┌──────────┐
    │   IDLE   │◄─────────────┐
    └────┬─────┘              │
         │ wake word          │ done
         ▼                    │
    ┌──────────┐              │
    │CONNECTING│──────────────┤
    └────┬─────┘              │
         │ connected          │
         ▼                    │
    ┌──────────┐   speaking   │
    │LISTENING │◄────────┐    │
    └────┬─────┘         │    │
         │ user done     │    │
         ▼               │    │
    ┌──────────┐  continue│   │
    │SPEAKING  │─────────┘    │
    └────┬─────┘              │
         │ done               │
         └────────────────────┘
```

---

## 📝 Key Patterns

### Thread Safety - Schedule
```cpp
// From callback → main thread
Schedule([this]() {
    display_->SetChatMessage("user", msg);
});
```

### Memory - PSRAM
```cpp
void* buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
```

### Logging
```cpp
static const char *TAG = "Module";
ESP_LOGI(TAG, "Info");
ESP_LOGE(TAG, "Error: %s", err);
```

---

## 🛠️ Commands

```bash
# Build
. $HOME/esp/esp-idf/export.sh && idf.py build

# Flash + Monitor
idf.py -p /dev/cu.usbmodem* flash monitor
```

---

## 🔗 References

- [Original Xiaozhi](https://github.com/78/xiaozhi-esp32)
- [ESP-IDF Docs](https://docs.espressif.com/projects/esp-idf/)
- [LVGL 9.x](https://docs.lvgl.io/9.4/)
