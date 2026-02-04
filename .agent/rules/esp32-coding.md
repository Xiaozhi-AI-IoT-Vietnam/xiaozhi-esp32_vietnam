---
name: ESP32 Coding Standards
description: Coding standards for ESP32 firmware development
---

# ESP32 Firmware Coding Standards

## Always Follow These Rules

### 1. Memory Management
```cpp
// ✅ Use PSRAM for large buffers (>4KB)
void* buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);

// ✅ Always free after use
heap_caps_free(buffer);

// ❌ Never use regular malloc for large allocations
void* bad = malloc(large_size); // WRONG!
```

### 2. Thread Safety
```cpp
// ✅ Use mutex for shared state
std::lock_guard<std::mutex> lock(mutex_);

// ✅ Schedule UI updates to main thread
Schedule([this]() {
    display_->SetChatMessage("user", message.c_str());
});

// ❌ Never access UI from callbacks directly
callback([this]() {
    display_->SetStatus("bad"); // WRONG! Not thread safe
});
```

### 3. Error Handling
```cpp
// ✅ Always check return values
esp_err_t err = some_function();
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
    return err;
}

// ❌ Never ignore errors
some_function(); // WRONG! Error ignored
```

### 4. Logging
```cpp
// ✅ Use proper logging with TAG
static const char *TAG = "ModuleName";
ESP_LOGI(TAG, "Info message: %s", data);
ESP_LOGW(TAG, "Warning: %d", code);
ESP_LOGE(TAG, "Error occurred");

// ❌ Don't use printf for logging
printf("message\n"); // WRONG!
```

### 5. Task Management
```cpp
// ✅ Sufficient stack size
xTaskCreate(task, "name", 8192, NULL, 5, NULL); // 8KB

// ✅ Use appropriate priority
// 1-5: Normal tasks
// 6-10: Time-critical tasks

// ❌ Never block main task indefinitely
while(1) { /* blocking */ } // WRONG!
```

### 6. Null Checks
```cpp
// ✅ Always check pointers before use
if (ptr != nullptr) {
    ptr->doSomething();
}

// ❌ Never dereference without check
ptr->doSomething(); // WRONG if ptr could be null
```

### 7. String Handling
```cpp
// ✅ Use std::string for safety
std::string text = "content";

// ✅ Check string length before use
if (!text.empty()) {
    process(text.c_str());
}
```

## Build Requirements
- No errors
- No critical warnings
- Test on real device before commit
