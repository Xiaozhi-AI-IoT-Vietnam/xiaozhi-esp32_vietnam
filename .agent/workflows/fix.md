---
description: Fix bugs với systematic debugging cho ESP32 firmware
---

# /fix - ESP32 Firmware Bug Fixing

## Khi nào sử dụng
- Device crash/reboot
- Audio không hoạt động
- Display không update
- WiFi/MQTT issues
- Unexpected behavior

## Phase 1: Understand the Bug

1. **Reproduce the issue**
   - Xác định steps to reproduce
   - Xác định expected vs actual behavior

2. **Check logs**
   ```bash
   idf.py monitor
   ```
   
   Tìm kiếm:
   - `E (` - Errors
   - `W (` - Warnings
   - `Guru Meditation` - Crash
   - `assert failed` - Assertion failures
   - `Stack` - Stack traces

## Phase 2: Diagnose

### Memory Issues
```bash
# Check heap stats in logs
I (xxx) SystemInfo: free sram: XXXX minimal sram: XXXX
```

Nếu `minimal sram` giảm liên tục → Memory leak

### Stack Overflow
```
Guru Meditation Error: Core X panic'ed (Stack canary watchpoint triggered)
```
→ Tăng task stack size

### Null Pointer
```
Guru Meditation Error: Core X panic'ed (LoadProhibited/StoreProhibited)
```
→ Check null pointers before use

### Watchdog Timeout
```
Task watchdog got triggered
```
→ Task bị block quá lâu, cần yield hoặc tối ưu

## Phase 3: Fix

3. **Locate the bug** in code:
   ```bash
   # Search for related code
   grep -rn "keyword" main/
   ```

4. **Apply fix** following ESP32 patterns:
   - Add null checks
   - Add error handling
   - Fix memory management
   - Fix thread safety

5. **Build and test**:
   ```bash
   idf.py build
   idf.py -p /dev/cu.usbmodem* flash monitor
   ```

## Phase 4: Verify

6. **Verify fix**:
   - Bug không còn reproduce
   - Không có regressions
   - Logs clean

7. **Add logging** để detect issues sớm hơn trong tương lai

## Common Fixes

### Audio Not Playing
```cpp
// Check audio codec enabled
audio_service_.EnableAudioOutput(true);

// Check audio channel open
if (!protocol_->IsAudioChannelOpened()) {
    protocol_->OpenAudioChannel();
}
```

### Display Not Updating
```cpp
// Must update from main thread
Schedule([this]() {
    display_->SetChatMessage("role", message.c_str());
});
```

### MQTT Not Receiving
```cpp
// Check subscription
esp_mqtt_client_subscribe(client, topic, 1);

// Check topic format
"device/{mac_address}/server"
```

### State Stuck
```cpp
// Force reset to idle
SetDeviceState(kDeviceStateIdle);

// Close any open channels
if (protocol_->IsAudioChannelOpened()) {
    protocol_->CloseAudioChannel();
}
```

## Debug Tools

// turbo
- **Monitor**: `idf.py monitor`
- **Core dump**: `idf.py coredump-info`
- **Heap trace**: Enable in menuconfig
