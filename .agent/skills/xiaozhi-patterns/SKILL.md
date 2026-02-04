---
name: Xiaozhi Application Patterns
description: Core application patterns for Xiaozhi ESP32 firmware. Use when working with application.cc, state machine, callbacks, or scheduling.
triggers:
  - application.cc
  - state machine
  - SetDeviceState
  - Schedule
  - OnMqtt
  - callback
---

# Xiaozhi Application Patterns

## State Machine

### Device States
```cpp
enum DeviceState {
    kDeviceStateUnknown,
    kDeviceStateIdle,       // Waiting for wake word
    kDeviceStateConnecting, // Opening audio channel
    kDeviceStateListening,  // Recording user speech
    kDeviceStateSpeaking    // Playing AI response
};
```

### State Transitions
```cpp
void Application::SetDeviceState(DeviceState state) {
    device_state_ = state;
    auto display = Board::GetInstance().GetDisplay();
    
    switch (state) {
    case kDeviceStateIdle:
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", ""); // Clear messages
        audio_service_.EnableVoiceProcessing(false);
        audio_service_.EnableWakeWordDetection(true);
        break;
        
    case kDeviceStateConnecting:
        display->SetStatus(Lang::Strings::CONNECTING);
        display->SetChatMessage("system", "");
        break;
        
    case kDeviceStateListening:
        display->SetStatus(Lang::Strings::LISTENING);
        audio_service_.EnableVoiceProcessing(true);
        audio_service_.EnableWakeWordDetection(false);
        break;
        
    case kDeviceStateSpeaking:
        display->SetStatus(Lang::Strings::SPEAKING);
        break;
    }
}
```

## Schedule Pattern

### Why Schedule?
Callbacks from protocols/audio run on different threads. UI and state must be updated on main thread.

### Usage
```cpp
// From any callback
protocol_->SetOnIncomingJsonCallback([this](const cJSON* json) {
    // DON'T access display/state directly here!
    
    // DO schedule to main thread
    std::string message = extract_message(json);
    Schedule([this, message]() {
        auto display = Board::GetInstance().GetDisplay();
        display->SetChatMessage("assistant", message.c_str());
    });
});
```

### Implementation
```cpp
void Application::Schedule(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    main_tasks_.push_back(std::move(callback));
    xEventGroupSetBits(event_group_, SCHEDULE_BIT);
}

// In main loop
void Application::MainLoop() {
    while (running_) {
        // Wait for events
        xEventGroupWaitBits(event_group_, ALL_BITS, pdTRUE, pdFALSE, portMAX_DELAY);
        
        // Process scheduled tasks
        std::vector<std::function<void()>> tasks;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks.swap(main_tasks_);
        }
        for (auto& task : tasks) {
            task();
        }
    }
}
```

## MQTT Notification Pattern

### Receiving Notifications
```cpp
void Application::OnMqttNotification(const MqttNotificationData &notification) {
    auto display = Board::GetInstance().GetDisplay();
    
    // Display notification
    if (!notification.title.empty()) {
        display->ShowNotification(notification.title.c_str());
    }
    if (!notification.content.empty()) {
        display->SetChatMessage("assistant", notification.content.c_str());
    }
    
    // Handle TTS
    if (notification.useLLM && !notification.content.empty()) {
        if (protocol_->IsAudioChannelOpened()) {
            // Already in conversation - send directly
            protocol_->SendMcpMessage("notification_speak", notification.content);
        } else {
            // Need to open channel first
            pending_notification_ = notification.content;
            protocol_->OpenAudioChannel();
        }
    } else {
        // Just play notification sound
        audio_service_.PlaySound(Lang::Sounds::OGG_NOTIFY);
    }
}
```

## Protocol Integration

### Setting Up Callbacks
```cpp
void Application::SetupProtocolCallbacks() {
    // JSON messages from server
    protocol_->SetOnIncomingJsonCallback([this](const cJSON* json) {
        HandleIncomingJson(json);
    });
    
    // Audio from server (TTS)
    protocol_->SetOnIncomingAudioCallback([this](std::vector<uint8_t>&& data) {
        audio_service_.PlayOpusData(std::move(data));
    });
    
    // Connection state
    protocol_->SetOnAudioChannelOpenedCallback([this]() {
        SetDeviceState(kDeviceStateListening);
    });
    
    protocol_->SetOnAudioChannelClosedCallback([this]() {
        SetDeviceState(kDeviceStateIdle);
    });
}
```

## Error Handling

### Graceful Degradation
```cpp
void Application::HandleError(const std::string& error) {
    ESP_LOGE(TAG, "Error: %s", error.c_str());
    
    // Show error on display
    auto display = Board::GetInstance().GetDisplay();
    display->SetChatMessage("system", error.c_str());
    
    // Play error sound
    audio_service_.PlaySound(Lang::Sounds::OGG_ERROR);
    
    // Reset to idle state
    if (protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    SetDeviceState(kDeviceStateIdle);
}
```

## Common Issues

### 1. UI not updating
**Cause**: Updating UI from non-main thread
**Fix**: Use `Schedule()` wrapper

### 2. State stuck
**Cause**: Missing state transition on error
**Fix**: Always reset to idle on errors

### 3. Memory leak
**Cause**: Callbacks capturing `this` without care
**Fix**: Ensure objects outlive callbacks or use weak references
