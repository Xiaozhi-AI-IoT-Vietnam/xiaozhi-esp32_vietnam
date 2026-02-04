---
description: Review ESP32 firmware code và đảm bảo chất lượng
---

# /review - ESP32 Firmware Code Review

## Khi nào sử dụng
- Trước khi commit/push code
- Sau khi implement tính năng mới
- Khi có PR cần review

## ESP32 Firmware Review Checklist

### 1. Memory Safety ⚠️ CRITICAL
```
□ Không có memory leaks (free all malloc)
□ Sử dụng PSRAM cho buffers lớn (>4KB)
□ Check heap sau operations quan trọng
□ Avoid stack overflow (task stack >= 4096)
□ No dangling pointers
```

### 2. Thread Safety ⚠️ CRITICAL
```
□ Shared state có mutex protection
□ Schedule() cho UI/state updates từ callbacks
□ Event groups cho synchronization
□ No race conditions trong callbacks
```

### 3. Error Handling
```
□ Check esp_err_t return values
□ ESP_LOGE cho errors
□ Graceful degradation khi error
□ No silent failures
```

### 4. Logging Standards
```
□ Có TAG cho mỗi module
□ ESP_LOGI cho info
□ ESP_LOGW cho warnings
□ ESP_LOGE cho errors
□ ESP_LOGD cho debug (production-safe)
```

### 5. WiFi/Network Resilience
```
□ Handle disconnect events
□ Auto-reconnect logic
□ Timeout cho network operations
□ Buffer management cho streaming
```

### 6. State Machine Consistency
```
□ DeviceState transitions hợp lệ
□ Display updated khi state change
□ Audio service updated
□ No stuck states
```

### 7. Code Style
```
□ Consistent formatting
□ Meaningful variable names
□ Comments cho complex logic
□ No magic numbers
```

## Review Process

// turbo
1. **Read skill files** để hiểu context:
   ```
   .agent/skills/esp32-firmware/SKILL.md
   ```

2. **Identify changed files**:
   ```bash
   git diff --name-only HEAD~1
   ```

3. **Review each file** với checklist trên

4. **Check build**:
   ```bash
   idf.py build
   ```

5. **Run on device** nếu có changes quan trọng

## Common Issues to Catch

### Memory Issues
```cpp
// BAD: Memory leak
char* str = (char*)malloc(100);
return; // leak!

// GOOD: Always free
char* str = (char*)malloc(100);
// use str...
free(str);
```

### Thread Safety Issues
```cpp
// BAD: Direct UI access from callback
protocol_->SetOnMessageCallback([this](const std::string& msg) {
    display_->SetChatMessage("user", msg.c_str()); // UNSAFE!
});

// GOOD: Schedule to main thread
protocol_->SetOnMessageCallback([this](const std::string& msg) {
    Schedule([this, msg]() {
        display_->SetChatMessage("user", msg.c_str());
    });
});
```

### State Machine Issues
```cpp
// BAD: State not cleaned up
if (device_state_ != kDeviceStateIdle) {
    return; // Skip without cleanup
}

// GOOD: Proper cleanup
if (device_state_ != kDeviceStateIdle) {
    ESP_LOGW(TAG, "Device busy, ignoring request");
    audio_service_.PlaySound(Lang::Sounds::OGG_ERROR);
    return;
}
```

## Output
- Review report với findings
- Suggested fixes
- Approval/Request changes
