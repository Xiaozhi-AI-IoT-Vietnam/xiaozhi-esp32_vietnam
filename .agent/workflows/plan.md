---
description: Lập kế hoạch và tạo PRD cho feature mới
---

# /plan - ESP32 Firmware Planning

## Khi nào sử dụng
- Thiết kế tính năng mới
- Architectural changes
- Trước khi implement features phức tạp

## Phase 1: Understand Requirements

1. **Clarify requirements**:
   - What is the user trying to achieve?
   - What are the constraints (memory, latency, etc.)?
   - What existing code will be affected?

2. **Research**:
   - Check existing implementation patterns in codebase
   - Review relevant skills:
     - `.agent/skills/esp32-firmware/SKILL.md`
     - `.agent/skills/esp32-audio/SKILL.md`
     - `.agent/skills/lvgl-display/SKILL.md`
     - `.agent/skills/esp32-mqtt-protocol/SKILL.md`

## Phase 2: Design

3. **Create design document** in `plans/design-{feature}.md`:

```markdown
# {Feature Name} Design

## Overview
Brief description of the feature.

## Requirements
- Functional requirements
- Non-functional requirements (memory, latency)

## Affected Components
- [ ] application.cc
- [ ] protocols/mqtt_protocol.cc
- [ ] display/
- [ ] audio/

## Implementation Plan

### Step 1: {Component/Task}
- What to change
- Code location
- Expected changes

### Step 2: {Component/Task}
...

## State Machine Changes
```
Current: Idle → Connecting → Listening → Speaking
New:     Idle → Connecting → Listening → Speaking → {NewState}
```

## Memory Impact
- Estimated additional SRAM: X KB
- PSRAM usage: Y KB

## Testing Plan
1. Test case 1
2. Test case 2

## Rollback Plan
- How to rollback if issues arise
```

## Phase 3: Review

4. **Validate design**:
   - Memory constraints met?
   - Thread safety considered?
   - State machine consistent?

5. **Create task breakdown** in `plans/tasks-{feature}.md`

## ESP32 Design Considerations

### Memory Budget
```
Total SRAM: ~320KB
Available: ~100KB (after FreeRTOS, WiFi, audio)
PSRAM: 8MB (for buffers, UI)
```

### Performance Targets
```
Audio latency: <100ms
UI update: 60fps
Wake word detection: <500ms
```

### Power Considerations
```
WiFi connected: ~100mA
Audio active: ~150mA
Deep sleep: <10µA
```

## Output
- `plans/design-{feature}.md` - Design document
- `plans/tasks-{feature}.md` - Task breakdown
