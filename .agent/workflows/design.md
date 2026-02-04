---
description: Thiết kế hệ thống từ PRD
---

# /design - ESP32 Firmware System Design

## Khi nào sử dụng
- Thiết kế architecture cho feature mới
- Major refactoring
- Thêm component mới

## Phase 1: Context Analysis

// turbo
1. **Read ESP32 skills**:
   - `.agent/skills/esp32-firmware/SKILL.md`
   - `.agent/skills/esp32-audio/SKILL.md`
   - `.agent/skills/lvgl-display/SKILL.md`
   - `.agent/skills/esp32-mqtt-protocol/SKILL.md`

2. **Analyze existing architecture**:
   ```
   main/
   ├── application.cc       # Core state machine
   ├── protocols/           # Communication
   │   ├── mqtt_protocol.cc
   │   └── websocket_protocol.cc
   ├── audio/               # Audio subsystem
   ├── display/             # UI/Display
   └── boards/              # Board configs
   ```

## Phase 2: Design

3. **Component Design**:

### For new component:
```cpp
// component.h
class NewComponent {
public:
    void Initialize();
    void Start();
    void Stop();
    
    // Callbacks
    void SetOnEventCallback(std::function<void()> callback);

private:
    std::mutex mutex_;
    // State
};
```

### For modifying existing:
- Identify touch points
- Plan minimal changes
- Maintain backward compatibility

## Phase 3: State Machine Design

4. **Update state diagram**:
```
┌─────────┐
│  Idle   │◄────────────────┐
└────┬────┘                 │
     │ wake/button          │ timeout/cancel
     ▼                      │
┌──────────┐                │
│Connecting│────────────────┤
└────┬─────┘                │
     │ connected            │
     ▼                      │
┌──────────┐   speaking     │
│Listening │───────────►────┤
└────┬─────┘                │
     │ user speech          │
     ▼                      │
┌──────────┐   done         │
│Speaking  │────────────────┘
└──────────┘
```

## Phase 4: Interface Design

5. **Define interfaces**:

```cpp
// Clear callback patterns
using OnNotificationCallback = std::function<void(const NotificationData&)>;
using OnAudioCallback = std::function<void(std::vector<uint8_t>&&)>;

// Clear event flow
class Protocol {
    void SetOnIncomingJsonCallback(...);
    void SetOnIncomingAudioCallback(...);
    void SendText(const std::string& text);
    void SendAudio(const std::vector<uint8_t>& data);
};
```

## Phase 5: Memory Planning

6. **Estimate memory usage**:

| Component | SRAM | PSRAM |
|-----------|------|-------|
| Core      | 20KB | -     |
| WiFi/BT   | 40KB | -     |
| Audio     | 30KB | 200KB |
| Display   | 10KB | 2MB   |
| Available | 50KB | 5.8MB |

## Output
- `plans/design-{feature}.md` - Architecture document
- Sequence diagrams
- State machine updates
- Interface definitions
