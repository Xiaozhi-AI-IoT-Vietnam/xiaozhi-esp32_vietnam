# 📞 Intercom Modes Implementation Guide

## 1. Các Chế Độ Intercom

| Chế độ | Gửi | Nhận | Mô tả | ESP32 |
|--------|-----|------|-------|-------|
| **PTT** | Giữ nút | Auto-play | Walkie-talkie truyền thống | ✅ Recommended |
| **VOX** | Tự động khi nói | Auto-play | Rảnh tay hoàn toàn | ✅ Supported |
| **Full Duplex** | Liên tục | Liên tục | Như điện thoại | ⚠️ Cần AEC |

---

## 2. Kiến Trúc Chế Độ

### 2.1 Enum Definition
```cpp
// application.h
enum IntercomMode {
  kIntercomModePTT = 0,      // Push-to-Talk (mặc định)
  kIntercomModeVOX = 1,      // Voice Activated
  kIntercomModeFullDuplex = 2 // Full Duplex (advanced)
};

struct IntercomContext {
  bool active = false;
  IntercomMode mode = kIntercomModePTT;
  std::string conversation_id;
  std::string target_mac;
  std::string target_name;
  std::string reply_to_mac;
  uint32_t last_activity_ms = 0;
  bool is_transmitting = false;  // Đang gửi audio
  bool is_receiving = false;     // Đang nhận audio
};
```

### 2.2 UI Lựa Chọn Chế Độ

```
┌────────────────────────────┐
│   📞 Intercom Settings     │
├────────────────────────────┤
│                            │
│  ○ PTT (Giữ nút để nói)    │
│  ● VOX (Tự động phát hiện) │
│  ○ Full Duplex (Điện thoại)│
│                            │
│  [Lưu]        [Hủy]        │
└────────────────────────────┘
```

---

## 3. Chi Tiết Từng Chế Độ

### 3.1 PTT (Push-to-Talk) ⭐ Recommended

**Flow:**
```
┌──────────────────┐
│    IDLE          │ ← Màn hình hiện "Giữ BOOT để nói"
└────────┬─────────┘
         │ [BOOT giữ]
         ▼
┌──────────────────┐
│  TRANSMITTING    │ ← Recording audio, gửi UDP/MQTT
└────────┬─────────┘
         │ [Nhả BOOT]
         ▼
┌──────────────────┐
│    SENDING       │ ← Gửi audio buffer cuối cùng
└────────┬─────────┘
         │ [ACK từ server]
         ▼
┌──────────────────┐
│    IDLE          │ ← "Đã gửi ✓" hoặc chờ reply
└──────────────────┘
```

**Implementation:**
```cpp
// Board button handler
boot_button_.OnPress([this]() {
  auto& app = Application::GetInstance();
  if (app.intercom_context_.active && 
      app.intercom_context_.mode == kIntercomModePTT) {
    app.StartIntercomTransmit();
  }
});

boot_button_.OnRelease([this]() {
  auto& app = Application::GetInstance();
  if (app.intercom_context_.is_transmitting) {
    app.StopIntercomTransmit();
  }
});

// Application.cc
void Application::StartIntercomTransmit() {
  intercom_context_.is_transmitting = true;
  
  auto display = Board::GetInstance().GetDisplay();
  display->SetChatMessage("system", "🎤 Đang nói...");
  
  // Start audio capture and stream
  audio_service_.StartCapture([this](const AudioData& data) {
    SendIntercomAudio(data);
  });
}

void Application::StopIntercomTransmit() {
  intercom_context_.is_transmitting = false;
  audio_service_.StopCapture();
  
  auto display = Board::GetInstance().GetDisplay();
  display->SetChatMessage("system", "✓ Đã gửi");
  
  // Flush remaining buffer
  FlushIntercomAudioBuffer();
}
```

**Pros:**
- Đơn giản, đáng tin cậy nhất
- Tiết kiệm bandwidth (chỉ gửi khi nói)
- Không cần AEC
- Không nhầm lẫn audio

**Cons:**
- Cần giữ nút
- Không tự nhiên như điện thoại

---

### 3.2 VOX (Voice Activated)

**Flow:**
```
┌──────────────────┐
│    IDLE          │ ← Màn hình hiện "Nói để gửi..."
└────────┬─────────┘
         │ [VAD detect voice]
         ▼
┌──────────────────┐
│  TRANSMITTING    │ ← Recording, gửi audio
└────────┬─────────┘
         │ [Silence > 2s]
         ▼
┌──────────────────┐
│    SENDING       │ ← Gửi buffer cuối
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│    IDLE          │ ← Chờ tiếp hoặc exit
└──────────────────┘
```

**Implementation:**
```cpp
void Application::StartIntercomVOX() {
  intercom_context_.mode = kIntercomModeVOX;
  
  auto display = Board::GetInstance().GetDisplay();
  display->SetChatMessage("system", "🎤 Nói để gửi tin nhắn...");
  
  // Enable VAD (Voice Activity Detection)
  audio_service_.EnableVAD(true);
  audio_service_.SetVADCallback([this](bool voice_detected) {
    if (voice_detected && !intercom_context_.is_transmitting) {
      StartIntercomTransmit();
    } else if (!voice_detected && intercom_context_.is_transmitting) {
      // Silence detected, wait 2s then stop
      Schedule([this]() {
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (!audio_service_.IsVoiceActive()) {
          StopIntercomTransmit();
        }
      });
    }
  });
  
  audio_service_.StartListening();
}
```

**VAD Parameters:**
```cpp
// Recommended settings for Intercom VOX
#define VOX_THRESHOLD_DB      -40   // Voice detection threshold
#define VOX_HOLD_TIME_MS      2000  // Silence before stop
#define VOX_MIN_SPEECH_MS     200   // Min speech to trigger
```

**Pros:**
- Rảnh tay hoàn toàn
- Tự nhiên hơn PTT

**Cons:**
- Có thể nhầm tiếng ồn
- Dễ bị trigger bởi TV, tiếng khác
- Cần tune VAD cho từng môi trường

---

### 3.3 Full Duplex (Advanced)

**Flow:**
```
┌──────────────────────────────────────────┐
│              CONNECTED                    │
│                                          │
│  ┌─────────────┐    ┌─────────────┐      │
│  │ TX Thread   │    │ RX Thread   │      │
│  │ Mic → UDP   │    │ UDP → Spk   │      │
│  │   out       │    │   in        │      │
│  └─────────────┘    └─────────────┘      │
│         ↓                  ↑             │
│    ┌────────────────────────────┐        │
│    │        AEC Engine          │        │
│    │   (Echo Cancellation)      │        │
│    └────────────────────────────┘        │
└──────────────────────────────────────────┘
```

**Requirements:**
- ✅ AEC-enabled board (Dual mic hoặc hardware AEC)
- ✅ Low-latency UDP streaming
- ✅ Jitter buffer
- ✅ Opus codec (low bandwidth, low latency)

**Implementation:**
```cpp
void Application::StartIntercomFullDuplex(const IntercomContact& target) {
  if (!audio_service_.HasAEC()) {
    ESP_LOGW(TAG, "Full Duplex requires AEC-enabled board!");
    // Fallback to PTT
    intercom_context_.mode = kIntercomModePTT;
    return;
  }
  
  intercom_context_.mode = kIntercomModeFullDuplex;
  intercom_context_.is_transmitting = true;
  intercom_context_.is_receiving = true;
  
  // Enable AEC
  audio_service_.EnableAEC(true);
  
  // Start bidirectional UDP stream
  udp_intercom_.Connect(target.ip, target.port);
  
  // TX Task: Mic → Encode → UDP
  xTaskCreate([](void* arg) {
    auto* app = (Application*)arg;
    while (app->intercom_context_.active) {
      AudioData data = app->audio_service_.CaptureFrame();
      OpusFrame encoded = OpusEncode(data);
      app->udp_intercom_.Send(encoded);
      vTaskDelay(pdMS_TO_TICKS(20)); // 20ms frames
    }
    vTaskDelete(NULL);
  }, "intercom_tx", 4096, this, 5, NULL);
  
  // RX Task: UDP → Decode → Speaker
  xTaskCreate([](void* arg) {
    auto* app = (Application*)arg;
    while (app->intercom_context_.active) {
      OpusFrame encoded = app->udp_intercom_.Receive();
      AudioData decoded = OpusDecode(encoded);
      // AEC processes this before output
      app->audio_service_.PlayWithAEC(decoded);
    }
    vTaskDelete(NULL);
  }, "intercom_rx", 4096, this, 5, NULL);
}
```

**Pros:**
- Trải nghiệm như điện thoại
- Tự nhiên nhất

**Cons:**
- Phức tạp nhất
- Cần AEC (không phải board nào cũng có)
- Bandwidth cao hơn
- Latency sensitive

---

## 4. UI Selection Implementation

### 4.1 IntercomSettingsUI Class
```cpp
// intercom_settings_ui.h
class IntercomSettingsUI {
public:
  void Show();
  void Hide();
  void SetMode(IntercomMode mode);
  IntercomMode GetMode() const;
  
  void SetOnModeChanged(std::function<void(IntercomMode)> cb);
  
private:
  lv_obj_t* container_ = nullptr;
  lv_obj_t* radio_ptt_ = nullptr;
  lv_obj_t* radio_vox_ = nullptr;
  lv_obj_t* radio_fullduplex_ = nullptr;
  IntercomMode current_mode_ = kIntercomModePTT;
  
  std::function<void(IntercomMode)> on_mode_changed_;
};
```

### 4.2 Integration with Intercom Contacts UI
```cpp
// Thêm button Settings vào IntercomContactsUI
void IntercomContactsUI::CreateSettingsButton() {
  lv_obj_t* btn = lv_btn_create(container_);
  lv_obj_set_size(btn, 40, 40);
  lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -10, 10);
  
  lv_obj_t* label = lv_label_create(btn);
  lv_label_set_text(label, LV_SYMBOL_SETTINGS);
  
  lv_obj_add_event_cb(btn, [](lv_event_t* e) {
    IntercomContactsUI* ui = (IntercomContactsUI*)lv_event_get_user_data(e);
    ui->ShowSettingsMenu();
  }, LV_EVENT_CLICKED, this);
}
```

### 4.3 Persist Mode to NVS
```cpp
void Application::SetIntercomMode(IntercomMode mode) {
  intercom_context_.mode = mode;
  
  Settings settings("intercom", true);
  settings.SetInt("mode", (int)mode);
  
  ESP_LOGI(TAG, "📞 Intercom mode set to: %s", 
           mode == kIntercomModePTT ? "PTT" :
           mode == kIntercomModeVOX ? "VOX" : "Full Duplex");
}

IntercomMode Application::GetIntercomMode() {
  Settings settings("intercom", false);
  return (IntercomMode)settings.GetInt("mode", kIntercomModePTT);
}
```

---

## 5. Recommendation by Board

| Board | Recommended Mode | Reason |
|-------|-----------------|--------|
| **Cube 1.54"** | PTT | No AEC, single mic |
| **1.83" TFT (1st)** | VOX or PTT | Dual mic, good VAD |
| **LCD 2.8" Touch** | PTT | Touch + Button combo |
| **Boards với AEC** | Full Duplex | Full experience |

---

## 6. Server Protocol Extension

### 6.1 Mode Announcement
```json
// Device → Server (khi bắt đầu call)
{
  "type": "intercom",
  "action": "call_start",
  "target_mac": "aa:bb:cc:dd:ee:ff",
  "mode": "ptt",  // "ptt" | "vox" | "fullduplex"
  "conversation_id": "uuid"
}

// Server → Target Device
{
  "type": "intercom_incoming",
  "from_mac": "sender_mac",
  "from_name": "Phòng khách",
  "mode": "ptt",
  "conversation_id": "uuid"
}
```

### 6.2 Audio Streaming
```json
// PTT/VOX: Chunked audio messages
{
  "type": "intercom_audio",
  "conversation_id": "uuid",
  "chunk_id": 1,
  "audio_opus": "<base64>",
  "is_final": false
}

// Full Duplex: UDP stream (không qua MQTT)
// Direct UDP: device_a:5060 ←→ device_b:5060
```

---

## 7. Implementation Priority

### Phase 1: PTT (Recommended First)
- [ ] Button press/release detection
- [ ] Audio capture on press
- [ ] Send via MQTT on release
- [ ] Receive and auto-play

### Phase 2: VOX
- [ ] VAD integration
- [ ] Threshold tuning
- [ ] Hold time configuration

### Phase 3: Full Duplex (Optional)
- [ ] AEC board detection
- [ ] UDP streaming
- [ ] Opus codec
- [ ] Jitter buffer

---

## 8. Quick Implementation: PTT Mode

Đây là implementation nhanh nhất để test:

```cpp
// 1. Khi chọn contact, chuyển sang PTT mode
void Application::OnIntercomContactSelected(const IntercomContact& contact) {
  Schedule([this, contact]() {
    intercom_contacts_ui_.Hide();
    
    intercom_context_.active = true;
    intercom_context_.mode = kIntercomModePTT;
    intercom_context_.target_mac = contact.mac;
    intercom_context_.target_name = contact.name;
    
    auto display = Board::GetInstance().GetDisplay();
    display->SetChatMessage("system", 
      ("📞 " + contact.name + "\nGiữ BOOT để nói...").c_str());
  });
}

// 2. Board handler cho PTT
boot_button_.OnPress([this]() {
  auto& app = Application::GetInstance();
  if (app.IsIntercomActive() && app.GetIntercomMode() == kIntercomModePTT) {
    ESP_LOGI(TAG, "🎤 PTT: Start transmit");
    app.StartIntercomTransmit();
  }
});

boot_button_.OnRelease([this]() {
  auto& app = Application::GetInstance();
  if (app.IsIntercomTransmitting()) {
    ESP_LOGI(TAG, "🎤 PTT: Stop transmit");
    app.StopIntercomTransmit();
  }
});

// 3. Transmit functions
void Application::StartIntercomTransmit() {
  intercom_context_.is_transmitting = true;
  
  auto display = Board::GetInstance().GetDisplay();
  display->SetChatMessage("system", "🔴 Đang ghi âm...");
  
  SetListeningMode(kListeningModeManualStop);
  StartListening();
}

void Application::StopIntercomTransmit() {
  intercom_context_.is_transmitting = false;
  StopListening();
  
  // Send the recorded audio via MQTT
  SendIntercomAudioBuffer();
  
  auto display = Board::GetInstance().GetDisplay();
  display->SetChatMessage("system", "✅ Đã gửi!\nChờ phản hồi...");
}
```

---

**Summary:** PTT là mode đơn giản và đáng tin cậy nhất để implement trước. VOX cần tune VAD. Full Duplex cần board có AEC.
