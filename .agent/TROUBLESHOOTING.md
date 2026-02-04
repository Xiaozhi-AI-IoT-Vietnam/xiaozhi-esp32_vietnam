# ESP32 Troubleshooting Guide

## 🔥 Common Crashes

### Guru Meditation Error: LoadProhibited / StoreProhibited
**Cause**: Null pointer dereference
```cpp
// BAD
display_->SetStatus("test"); // display_ is null!

// GOOD
if (display_ != nullptr) {
    display_->SetStatus("test");
}
```

### Guru Meditation Error: Stack canary watchpoint triggered
**Cause**: Stack overflow
```cpp
// BAD - Task stack too small
xTaskCreate(task, "name", 2048, NULL, 5, NULL);

// GOOD - Increase stack size
xTaskCreate(task, "name", 8192, NULL, 5, NULL);
```

### Task watchdog got triggered
**Cause**: Task blocked too long without yielding
```cpp
// BAD - Infinite loop without yield
while (true) {
    // busy work
}

// GOOD - Add delay to yield
while (true) {
    // work
    vTaskDelay(pdMS_TO_TICKS(10));
}
```

---

## 💾 Memory Issues

### Free heap keeps decreasing
**Cause**: Memory leak
```cpp
// BAD - Memory not freed
char* str = (char*)malloc(100);
return; // leak!

// GOOD - Always free
char* str = (char*)malloc(100);
// use str...
free(str);
```

### malloc returns NULL
**Cause**: Out of memory or fragmentation
```cpp
// Use PSRAM for large buffers
void* buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
if (buf == nullptr) {
    ESP_LOGE(TAG, "Out of memory! Free: %lu", 
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
```

### How to check memory
```cpp
ESP_LOGI(TAG, "Free SRAM: %lu", esp_get_free_heap_size());
ESP_LOGI(TAG, "Free PSRAM: %lu", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
ESP_LOGI(TAG, "Min free: %lu", esp_get_minimum_free_heap_size());
```

---

## 🔊 Audio Issues

### No sound output
1. Check codec initialization:
   ```
   Look for: I (xxx) ES8311: Work in Slave mode
   ```
2. Check volume: `audio_codec->SetVolume(80);`
3. Check I2S config in board file

### Audio glitches/crackling
**Cause**: Buffer underrun
- Increase buffer size
- Check CPU usage
- Lower sample rate if needed

### Wake word not detecting
1. Check model loaded:
   ```
   Look for: I (xxx) AFE: AFE Pipeline: ... |WakeNet(...)|
   ```
2. Check microphone working
3. Reduce background noise

---

## 📡 WiFi/Network Issues

### WiFi won't connect
```cpp
// Check these logs:
// I (xxx) wifi:state: auth -> assoc
// I (xxx) wifi:connected with SSID

// If stuck at auth:
// - Wrong password
// - Wrong auth mode
```

### MQTT disconnect
```cpp
// Handle reconnection
case MQTT_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "MQTT disconnected, will auto-reconnect");
    break;
```

### UDP packets not received
**Cause**: NAT/firewall or bind not sent
```cpp
// After UDP connect, send bind packet
std::string bind_packet;
bind_packet.push_back(0x00);
bind_packet.append(session_id);
udp_->Send(bind_packet);
```

---

## 📺 Display Issues

### Display not updating
**Cause**: Updated from wrong thread
```cpp
// BAD - Direct update from callback
callback = [this]() {
    display_->SetStatus("test"); // Wrong thread!
};

// GOOD - Schedule to main thread
callback = [this]() {
    Schedule([this]() {
        display_->SetStatus("test");
    });
};
```

### LVGL crash
**Cause**: Concurrent access
- Use `lv_lock()` / `lv_unlock()` if accessing from multiple threads
- Always update from LVGL task

---

## 🔧 Build Issues

### Component not found
```bash
# Clean and rebuild
idf.py fullclean
idf.py build
```

### Linker error: undefined reference
- Check CMakeLists.txt includes all source files
- Check REQUIRES in component

### Flash fails
```bash
# Try lower baud rate
idf.py -p /dev/cu.usbmodem* -b 115200 flash

# Or erase first
idf.py -p /dev/cu.usbmodem* erase-flash
idf.py -p /dev/cu.usbmodem* flash
```

---

## 🔍 Debug Commands

```bash
# Check memory components
idf.py size-components

# Get core dump
idf.py coredump-info

# Monitor with timestamp
idf.py monitor --timestamps

# Find port
ls /dev/cu.usb*
```
