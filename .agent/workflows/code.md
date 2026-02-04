---
description: Triển khai code ESP32 firmware theo design document
---

# /code - ESP32 Firmware Code Workflow

## Khi nào sử dụng
- Implement tính năng mới cho ESP32
- Viết code theo design document
- Refactor code firmware

## Pre-requisites
// turbo
1. Đọc các skill ESP32:
   - `.agent/skills/esp32-firmware/SKILL.md`
   - `.agent/skills/esp32-audio/SKILL.md`
   - `.agent/skills/lvgl-display/SKILL.md`
   - `.agent/skills/esp32-mqtt-protocol/SKILL.md`

## Phase 1: Understand Context

// turbo
2. Kiểm tra project structure:
   ```bash
   ls -la main/
   ```

// turbo
3. Xem reference code (nếu cần):
   - `main/application.cc` - Core application logic
   - `main/protocols/mqtt_protocol.cc` - MQTT implementation
   - `main/audio/` - Audio processing

## Phase 2: Implementation

4. **Viết code** theo ESP32 patterns:

### Code Standards cho ESP32
```cpp
// 1. Always use logging
static const char *TAG = "ModuleName";
ESP_LOGI(TAG, "Action description");

// 2. Check errors
esp_err_t err = some_function();
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
    return err;
}

// 3. Use PSRAM for large buffers
void* buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);

// 4. Thread safety
std::lock_guard<std::mutex> lock(mutex_);

// 5. Memory cleanup
if (ptr) {
    heap_caps_free(ptr);
    ptr = nullptr;
}
```

### Common Patterns
```cpp
// Schedule task on main thread
Schedule([this]() {
    // Safe to access UI, state
});

// Event groups for synchronization
xEventGroupSetBits(event_group_, EVENT_BIT);

// State machine
void SetDeviceState(DeviceState state) {
    device_state_ = state;
    // Update dependent components
}
```

## Phase 3: Build & Test

// turbo
5. Build firmware:
   ```bash
   . $HOME/esp/esp-idf/export.sh && idf.py build
   ```

6. Fix any build errors

// turbo-all
7. Flash và test:
   ```bash
   idf.py -p /dev/cu.usbmodem* flash monitor
   ```

## Phase 4: Verify

8. Kiểm tra trong logs:
   - Không có errors/warnings mới
   - Functionality hoạt động đúng
   - Memory usage ổn định

9. Test edge cases:
   - Device khi idle
   - Device khi busy (listening/speaking)
   - WiFi disconnect/reconnect

## Output
- Code mới/sửa đổi
- Build thành công
- Test trên device thực
