---
name: ESP32 Firmware Development
description: Core skill for ESP32/ESP-IDF firmware development. Covers ESP-IDF framework, FreeRTOS, memory management, peripherals, WiFi/BLE, OTA updates, and embedded C++ patterns. Use when writing, debugging, or reviewing ESP32 firmware code.
triggers:
  - esp32
  - esp-idf
  - freertos
  - embedded
  - firmware
  - idf.py
  - main/
  - components/
  - .cc files
  - .h files
---

# ESP32 Firmware Development Skill

## Overview
This skill provides expertise for ESP32 firmware development using ESP-IDF framework and FreeRTOS.

## Project Structure Reference
```
xiaozhi-esp32_vietnam2/
├── main/                    # Main application code
│   ├── application.cc       # Core application logic
│   ├── application.h
│   ├── protocols/           # Communication protocols (MQTT, WebSocket)
│   ├── audio/               # Audio processing, codec
│   ├── display/             # LCD, LVGL UI
│   └── boards/              # Board-specific configurations
├── components/              # ESP-IDF components
├── sdkconfig                # Project configuration
├── CMakeLists.txt
└── partitions.csv           # Flash partition table
```

## ESP-IDF Core Concepts

### 1. Build System
```bash
# Configure project
idf.py menuconfig

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/cu.usbmodemXXXX flash monitor

# Clean build
idf.py fullclean
```

### 2. Logging
```cpp
#include "esp_log.h"

static const char *TAG = "MyComponent";

ESP_LOGI(TAG, "Info message");
ESP_LOGW(TAG, "Warning: %s", error_msg);
ESP_LOGE(TAG, "Error code: %d", err);
ESP_LOGD(TAG, "Debug details");
```

### 3. Memory Management
```cpp
// Heap allocation
void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
heap_caps_free(ptr);

// Check memory
ESP_LOGI(TAG, "Free heap: %lu", esp_get_free_heap_size());
ESP_LOGI(TAG, "Free PSRAM: %lu", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

### 4. FreeRTOS Tasks
```cpp
// Create task
xTaskCreate(task_function, "TaskName", 4096, NULL, 5, &task_handle);

// Task with specific core
xTaskCreatePinnedToCore(task_function, "TaskName", 4096, NULL, 5, &task_handle, 0);

// Delay
vTaskDelay(pdMS_TO_TICKS(100));

// Event groups
EventGroupHandle_t event_group = xEventGroupCreate();
xEventGroupSetBits(event_group, BIT0);
xEventGroupWaitBits(event_group, BIT0, pdTRUE, pdTRUE, portMAX_DELAY);
```

### 5. WiFi Connection
```cpp
#include "esp_wifi.h"
#include "esp_event.h"

// Initialize WiFi
esp_netif_init();
esp_event_loop_create_default();
esp_netif_create_default_wifi_sta();

wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
esp_wifi_init(&cfg);
esp_wifi_set_mode(WIFI_MODE_STA);
esp_wifi_start();
```

### 6. NVS (Non-Volatile Storage)
```cpp
#include "nvs_flash.h"
#include "nvs.h"

// Initialize
nvs_flash_init();

// Read/Write
nvs_handle_t handle;
nvs_open("storage", NVS_READWRITE, &handle);
nvs_set_str(handle, "key", "value");
nvs_commit(handle);
nvs_close(handle);
```

## Xiaozhi ESP32 Specific Patterns

### Application State Machine
```cpp
enum DeviceState {
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking
};

void Application::SetDeviceState(DeviceState state) {
    device_state_ = state;
    // Update display, audio, etc.
}
```

### Protocol Pattern
```cpp
class Protocol {
public:
    virtual bool Start() = 0;
    virtual void SendText(const std::string& text) = 0;
    virtual bool IsAudioChannelOpened() = 0;
    virtual void OpenAudioChannel() = 0;
    virtual void CloseAudioChannel() = 0;
};
```

### Scheduled Tasks Pattern
```cpp
void Application::Schedule(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    main_tasks_.push_back(std::move(callback));
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}
```

## Common Issues & Solutions

### 1. Stack Overflow
```cpp
// Increase task stack size
xTaskCreate(task, "name", 8192, NULL, 5, NULL); // 8KB stack
```

### 2. Memory Fragmentation
```cpp
// Use PSRAM for large allocations
void* buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
```

### 3. WiFi Reconnection
```cpp
esp_wifi_connect(); // In disconnect event handler
```

### 4. Watchdog Timeout
```cpp
// Feed watchdog in long loops
esp_task_wdt_reset();
```

## Build Configuration (sdkconfig)
```
CONFIG_ESP32S3_DEFAULT_CPU_FREQ_240=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_USE_CAPS_ALLOC=y
CONFIG_FREERTOS_HZ=1000
```

## References
- Original Xiaozhi: https://github.com/78/xiaozhi-esp32
- ESP-IDF: https://docs.espressif.com/projects/esp-idf/
- Arduino-ESP32: https://github.com/espressif/arduino-esp32
