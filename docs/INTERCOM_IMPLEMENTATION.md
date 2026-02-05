# 📞 Intercom Implementation Guide

## 1. Tổng quan Kiến trúc

```
┌─────────────────┐         ┌──────────────┐         ┌─────────────────┐
│   Device A      │         │   Server     │         │   Device B      │
│   (Sender)      │         │   (Relay)    │         │   (Receiver)    │
├─────────────────┤         ├──────────────┤         ├─────────────────┤
│ 1. User selects │         │              │         │                 │
│    contact B    │─────────│──────────────│─────────│                 │
│                 │         │              │         │                 │
│ 2. Record audio │         │              │         │                 │
│    (10s max)    │─────────│──────────────│─────────│                 │
│                 │         │              │         │                 │
│ 3. Send MQTT    │────────►│ 4. Route msg │────────►│ 5. Receive MQTT │
│    {intercom}   │         │    to B      │         │    {intercom}   │
│                 │         │              │         │                 │
│                 │         │              │         │ 6. Play TTS     │
│                 │         │              │         │    "Tin từ A"   │
│                 │         │              │         │                 │
│                 │◄────────│ 8. Route     │◄────────│ 7. Record reply │
│ 9. Play reply   │         │    reply     │         │    (auto-listen)│
└─────────────────┘         └──────────────┘         └─────────────────┘
```

## 2. Khác biệt: Chatbot vs Intercom

| Aspect | Chatbot | Intercom |
|--------|---------|----------|
| **Protocol** | WebSocket (bidirectional) | MQTT (pub/sub) |
| **Audio** | Stream to AI server | P2P via server relay |
| **Session** | AI conversation | Human-to-human |
| **Wake** | Wake word or button | Direct call UI |
| **Backend** | LLM processing | Simple routing |

## 3. Server API Cần Thiết

### 3.1 Lấy danh sách contacts (đã có ✅)
```http
GET /api/v1/device/intercom-contacts
Authorization: Bearer <token>
device-id: <mac_address>

Response:
{
  "success": true,
  "contacts": [
    {
      "id": "uuid",
      "name": "Phòng khách",
      "mac": "aa:bb:cc:dd:ee:ff",
      "owner": "Hoài",
      "status": "online",
      "type": "friend"
    }
  ]
}
```

### 3.2 Gửi tin nhắn Intercom (CẦN IMPLEMENT ❌)
```http
POST /api/v1/intercom/send
Authorization: Bearer <token>
Content-Type: application/json

{
  "target_mac": "aa:bb:cc:dd:ee:ff",
  "message": "Con ơi ăn cơm đi",  // Text (từ STT)
  "audio_url": "https://...",     // Optional: audio file
  "conversation_id": "uuid"
}

Response:
{
  "success": true,
  "conversation_id": "abc123"
}
```

### 3.3 MQTT Push Format (Server → Device)
```json
Topic: device/<device_id>/notification

// Tin nhắn mới:
{
  "type": "intercom",
  "from_device_name": "Phòng ngủ",
  "from_device_id": "uuid-sender",
  "from_mac": "sender_mac",
  "message": "Con ơi ăn cơm đi",
  "conversation_id": "abc123",
  "timestamp": "2026-02-05T08:00:00Z"
}

// Reply:
{
  "type": "intercom_reply",
  "from_device_name": "Phòng khách",
  "from_device_id": "uuid-replier",
  "message": "Dạ con biết rồi",
  "conversation_id": "abc123"
}
```

## 4. Firmware Implementation

### 4.1 Current State (Incomplete)
```cpp
// application.cc - Chỉ show notification, CHƯA gửi intercom thật
void Application::OnIntercomContactSelected(const IntercomContact& contact) {
  Schedule([...]() {
    intercom_contacts_ui_.Hide();
    display->ShowNotification("Đang gọi...", 2000);
    // TODO: Implement actual intercom call
  });
}
```

### 4.2 Full Implementation Required

#### A. Thêm IntercomContext vào application.h:
```cpp
struct IntercomContext {
  bool active = false;
  std::string conversation_id;
  std::string target_mac;
  std::string target_name;
  std::string reply_to_mac;  // Khi nhận tin, dùng để reply
  uint32_t last_activity_ms = 0;
};

class Application {
  // ...
  IntercomContext intercom_context_;
  
  void SendIntercomMessage(const std::string& target_mac, const std::string& message);
  void HandleIntercomReceived(const IntercomData& data);
};
```

#### B. Sửa OnIntercomContactSelected:
```cpp
void Application::OnIntercomContactSelected(const IntercomContact& contact) {
  Schedule([this, contact]() {
    ESP_LOGI(TAG, "📞 Starting intercom to: %s", contact.name.c_str());
    
    // 1. Hide UI
    intercom_contacts_ui_.Hide();
    
    // 2. Set intercom context (QUAN TRỌNG: để phân biệt với chatbot)
    intercom_context_.active = true;
    intercom_context_.target_mac = contact.mac;
    intercom_context_.target_name = contact.name;
    intercom_context_.conversation_id = GenerateUUID();
    
    // 3. Show recording UI
    auto display = Board::GetInstance().GetDisplay();
    if (display) {
      display->SetChatMessage("system", 
        ("🎤 Nói tin nhắn cho " + contact.name + "...").c_str());
    }
    
    // 4. Start listening (10s auto-stop)
    SetListeningMode(kListeningModeAutoStop);
    // NOTE: StartListening() sẽ check intercom_context_.active
    // để quyết định gửi qua AI hay Intercom
    StartListening();
  });
}
```

#### C. Sửa Audio Processing để phân biệt Intercom vs Chatbot:
```cpp
void Application::OnAudioInput(const std::string& text) {
  if (intercom_context_.active) {
    // Đây là tin nhắn INTERCOM - gửi qua MQTT
    SendIntercomMessage(intercom_context_.target_mac, text);
    
    // Show confirmation
    auto display = Board::GetInstance().GetDisplay();
    display->ShowNotification("✅ Đã gửi!", 2000);
    
    // Clear context
    intercom_context_.active = false;
  } else {
    // Đây là CHATBOT bình thường - gửi qua WebSocket AI
    protocol_->SendText(text);
  }
}
```

#### D. Gửi Intercom qua MQTT:
```cpp
void Application::SendIntercomMessage(const std::string& target_mac, 
                                       const std::string& message) {
  if (!mqtt_notification_) return;
  
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "intercom");
  cJSON_AddStringToObject(root, "action", "send");
  cJSON_AddStringToObject(root, "target_mac", target_mac.c_str());
  cJSON_AddStringToObject(root, "message", message.c_str());
  cJSON_AddStringToObject(root, "conversation_id", 
                          intercom_context_.conversation_id.c_str());
  cJSON_AddStringToObject(root, "from_mac", SystemInfo::GetMacAddress().c_str());
  
  char* json = cJSON_PrintUnformatted(root);
  if (json) {
    mqtt_notification_->PublishIntercom(json);
    free(json);
  }
  cJSON_Delete(root);
}
```

#### E. Nhận tin nhắn Intercom (đã có handler):
```cpp
// mqtt_notification.cc đã parse "type": "intercom"
// Application::OnIntercom sẽ được gọi

void Application::OnIntercom(const IntercomData& data) {
  Schedule([this, data]() {
    ESP_LOGI(TAG, "📩 Intercom from %s: %s", 
             data.from_device_name.c_str(), data.message.c_str());
    
    // 1. Save context for reply
    intercom_context_.active = true;
    intercom_context_.reply_to_mac = data.from_mac;
    intercom_context_.from_name = data.from_device_name;
    intercom_context_.conversation_id = data.conversation_id;
    
    // 2. Play TTS notification
    std::string announcement = "Tin nhắn từ " + data.from_device_name + 
                               ": " + data.message;
    protocol_->SendNotificationSpeak(announcement, true);  // intercom=true
    
    // 3. After TTS, auto-listen for reply (10s)
    // Được handle trong OnTtsFinish
  });
}

void Application::OnTtsFinish() {
  if (intercom_context_.active) {
    // Auto-listen for reply after playing intercom message
    auto display = Board::GetInstance().GetDisplay();
    display->SetChatMessage("system", 
      ("🎤 Trả lời " + intercom_context_.from_name + "...").c_str());
    
    SetListeningMode(kListeningModeAutoStop);
    StartListening();
  }
}
```

## 5. Testing Checklist

### Firmware:
- [ ] Hiển thị danh sách contacts ✅
- [ ] Chọn contact qua touch/button ✅ (fix Schedule)
- [ ] Record audio khi chọn contact
- [ ] Gửi tin nhắn qua MQTT
- [ ] Nhận tin nhắn MQTT push
- [ ] Play TTS khi nhận
- [ ] Auto-listen để reply

### Server:
- [ ] API `/intercom-contacts` ✅
- [ ] API `/intercom/send`
- [ ] MQTT routing giữa devices
- [ ] Store conversation history (optional)

## 6. Fallback: Demo Mode

Nếu chưa có server Intercom, có thể test với demo:
```cpp
void Application::OnIntercomContactSelected(const IntercomContact& contact) {
  Schedule([this, contact]() {
    intercom_contacts_ui_.Hide();
    
    // Demo: Giả lập nhận tin nhắn từ contact đó
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    auto display = Board::GetInstance().GetDisplay();
    display->ShowNotification(
      ("📩 Demo: " + contact.name + " says Hello!").c_str(), 3000);
  });
}
```

---

**Status:** Feature đang ở giai đoạn UI complete, cần implement backend routing.
