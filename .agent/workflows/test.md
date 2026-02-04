---
description: Chạy tests và tạo QA report
---

# /test - ESP32 Firmware Testing

## Khi nào sử dụng
- Sau khi implement feature mới
- Trước khi merge/push
- Kiểm tra regression

## Phase 1: Build Verification

// turbo
1. **Clean build**:
   ```bash
   cd /Users/digits/Github/xiaozhi-esp32_vietnam2
   . $HOME/esp/esp-idf/export.sh
   idf.py fullclean
   idf.py build
   ```

2. **Check build output**:
   - No errors
   - No critical warnings
   - Binary size within limits

## Phase 2: Flash & Monitor

// turbo
3. **Flash firmware**:
   ```bash
   idf.py -p /dev/cu.usbmodem* flash monitor
   ```

4. **Check boot sequence**:
   ```
   ✓ Board initialization
   ✓ Display on
   ✓ Audio codec initialized
   ✓ WiFi connected
   ✓ MQTT connected
   ✓ STATE: idle
   ```

## Phase 3: Functional Testing

### Core Functionality
```
□ Wake word detection works
□ Voice input recognized
□ AI response received
□ TTS playback works
□ Display shows conversation
```

### State Machine
```
□ Idle → Connecting works
□ Connecting → Listening works
□ Listening → Speaking works
□ Speaking → Listening works (multi-turn)
□ Any state → Idle (timeout/cancel)
```

### Notifications (MQTT)
```
□ Notification received when idle
□ Notification received when busy
□ TTS plays for notification (if useTTS=true)
□ Display updates with notification
```

### Audio
```
□ Microphone input clear
□ Speaker output clear
□ No audio glitches
□ Volume control works
```

### Display
```
□ Status updates correctly
□ Chat messages display
□ Emotions change
□ Text clears on idle
```

### Edge Cases
```
□ WiFi disconnect/reconnect
□ MQTT disconnect/reconnect
□ Multiple rapid requests
□ Long conversations
□ Large text responses
```

## Phase 4: Memory Check

5. **Monitor memory** during testing:
   ```
   Watch logs for:
   I (xxx) SystemInfo: free sram: XXXX minimal sram: XXXX
   ```

   - `free sram` should be stable
   - `minimal sram` should not decrease continuously

## Phase 5: Report

6. **Create QA Report** if issues found:

```markdown
# QA Report - {Date}

## Test Environment
- Firmware version: X.X.X
- Board: xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8
- WiFi: Connected

## Test Results

### Passed ✓
- Wake word detection
- Voice conversation
- Notifications

### Failed ✗
- {Issue description}
- Steps to reproduce
- Expected vs Actual

### Warnings ⚠️
- {Observation}

## Memory Stats
- Boot: 113KB free
- After test: 90KB free
- Minimal: 74KB

## Recommendations
- {Fix suggestions}
```

## Quick Test Commands

// turbo-all
```bash
# Build only
idf.py build

# Flash and monitor
idf.py -p /dev/cu.usbmodem* flash monitor

# Monitor only
idf.py -p /dev/cu.usbmodem* monitor

# Check partition table
idf.py partition-table
```
