# Firmware Guide: Intercom (Walkie-Talkie) Feature

> **Document Version:** 1.0  
> **Created:** 2026-02-03  
> **Target:** Xiaozhi ESP32 Firmware  
> **Status:** ✅ Implemented

---

## 📋 Tổng quan

Tính năng **Intercom** cho phép các thiết bị trong cùng tài khoản gửi tin nhắn voice cho nhau, tương tự bộ đàm.

### Flow hoạt động

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                           INTERCOM MESSAGE FLOW                               │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Device A (Sender)                          Device B (Receiver)              │
│  ─────────────────                          ───────────────────              │
│                                                                              │
│  1. User: "Gọi phòng ngủ - con ơi ăn cơm"                                    │
│         │                                                                    │
│         ▼                                                                    │
│  2. AI parse → intercom.send(target="phòng ngủ", message="con ơi ăn cơm")   │
│         │                                                                    │
│         ▼                                                                    │
│  3. Backend gửi MQTT                                                         │
│     device/{mac_b}/server  ────────────────► Nhận message type="intercom"   │
│                                                      │                       │
│                                                      ▼                       │
│                                              4. Firmware xử lý:              │
│                                                 a) Phát TTS thông báo        │
│                                                 b) Auto-listen 10s           │
│                                                      │                       │
│                                                      ▼                       │
│                                              5. User B nói: "Dạ biết rồi"   │
│                                                      │                       │
│  6. Nhận message type="intercom_reply"  ◄────────────┘                       │
│         │                                                                    │
│         ▼                                                                    │
│  7. Firmware xử lý:                                                          │
│     a) Phát TTS phản hồi                                                     │
│     b) Auto-listen (optional)                                                │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## 1. MQTT Message Format

### 1.1 Incoming: `type: "intercom"` (Tin nhắn đến)

Firmware nhận message này khi có ai đó gọi đến thiết bị:

```json
{
    "type": "intercom",
    "from_device_name": "phòng khách",
    "from_device_id": "uuid-of-sender-device",
    "message": "con ơi ăn cơm đi",
    "conversation_id": "abc123def456",
    "reply_to_mac": "aa:bb:cc:dd:ee:ff",
    "timestamp": "2026-02-03T21:00:00Z"
}
```

### 1.2 Incoming: `type: "intercom_reply"` (Phản hồi đến)

Firmware nhận message này khi thiết bị kia phản hồi:

```json
{
    "type": "intercom_reply",
    "from_device_name": "phòng ngủ",
    "from_device_id": "uuid-of-replier-device",
    "message": "dạ con biết rồi",
    "conversation_id": "abc123def456",
    "timestamp": "2026-02-03T21:00:05Z"
}
```

**Các trường quan trọng:**

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | `"intercom"` hoặc `"intercom_reply"` |
| `from_device_name` | string | Tên thiết bị gửi (để thông báo) |
| `message` | string | Nội dung tin nhắn |
| `conversation_id` | string | ID cuộc hội thoại (để reply) |
| `reply_to_mac` | string | MAC address thiết bị gửi |

---

## 2. Implementation Details

### 2.1 Data Structures

**IntercomData** (mqtt_notification.h):
```cpp
struct IntercomData {
  std::string type;              // "intercom" or "intercom_reply"
  std::string from_device_name;  // Name of sender device
  std::string from_device_id;    // UUID of sender device
  std::string message;           // Voice message content
  std::string conversation_id;   // Conversation ID for reply tracking
  std::string reply_to_mac;      // MAC address to reply to
  bool is_reply;                 // true if this is a reply message
};
```

**IntercomContext** (application.h):
```cpp
struct IntercomContext {
  bool active = false;
  std::string conversation_id;
  std::string reply_to_mac;
  std::string from_name;
  uint32_t last_activity_ms = 0;
};
```

### 2.2 Handler Functions

| Function | File | Purpose |
|----------|------|---------|
| `ParseIntercom()` | mqtt_notification.cc | Parse JSON → IntercomData |
| `OnIntercom()` | application.cc | Router - route to message/reply handler |
| `HandleIntercomMessage()` | application.cc | Handle incoming message, TTS, auto-listen |
| `HandleIntercomReply()` | application.cc | Handle reply, TTS |
| `StartIntercomListen()` | application.cc | Start listening for reply |
| `EndIntercomSession()` | application.cc | End session, cleanup context |

### 2.3 Flow trong Code

```
MqttNotification::HandleMessage()
    ├── strcmp(type, "intercom") == 0
    │   └── ParseIntercom(root, intercom)
    │       └── on_intercom_ callback
    │           └── Application::OnIntercom()
    │               ├── is_reply == false
    │               │   └── HandleIntercomMessage()
    │               │       ├── Store context
    │               │       ├── Display notification
    │               │       ├── Open audio channel
    │               │       └── Send TTS request
    │               └── is_reply == true
    │                   └── HandleIntercomReply()
    │                       ├── Update context
    │                       └── Send TTS request
```

---

## 3. State Machine

```
┌─────────────────────────────────────────────────────────────────┐
│                    INTERCOM STATE MACHINE                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│    ┌─────────┐                                                  │
│    │  IDLE   │ ◄──────────────┐                                 │
│    └────┬────┘                │                                 │
│         │                     │ timeout (30s) or "kết thúc"     │
│         │ receive             │                                 │
│         │ type="intercom"     │                                 │
│         ▼                     │                                 │
│    ┌─────────────────┐        │                                 │
│    │ PLAYING_MESSAGE │        │                                 │
│    │ (TTS playback)  │        │                                 │
│    └────────┬────────┘        │                                 │
│             │                 │                                 │
│             │ TTS done        │                                 │
│             ▼                 │                                 │
│    ┌─────────────────┐        │                                 │
│    │ AUTO_LISTENING  │────────┤                                 │
│    │ (wait for reply)│        │                                 │
│    └────────┬────────┘        │                                 │
│             │                 │                                 │
│             │ user speaks     │                                 │
│             ▼                 │                                 │
│    ┌─────────────────┐        │                                 │
│    │ SENDING_REPLY   │────────┘                                 │
│    │ (relay to other)│                                          │
│    └─────────────────┘                                          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 4. Testing Checklist

### 4.1 Basic Flow

- [ ] Device nhận được `type: "intercom"` message
- [ ] TTS phát: "Tin nhắn từ [tên]: [nội dung]"
- [ ] Display hiển thị: "📞 [tên device]"
- [ ] Sau TTS, tự động chuyển sang listening mode
- [ ] User nói phản hồi → gửi về server thành công
- [ ] Device nhận được `type: "intercom_reply"` và phát TTS

### 4.2 Edge Cases

- [ ] Intercom khi device đang IDLE (chưa có session)
- [ ] Intercom trong khi đang phát nhạc (pause music?)
- [ ] Timeout sau 30s không có phản hồi
- [ ] User nói "kết thúc" để tắt intercom mode
- [ ] Multiple messages trong conversation

### 4.3 Logs to Check

```
[INTERCOM] OnIntercom: type=intercom, from=phòng khách, msg=...
[INTERCOM] Message from phòng khách: con ơi ăn cơm
[INTERCOM] Opening audio channel for TTS...
[INTERCOM] Sending TTS: {"type":"notification_speak","content":"..."}
[INTERCOM] Waiting for TTS, will auto-listen after
```

---

## 5. Files Changed

| File | Changes |
|------|---------|
| `main/mqtt_notification.h` | +28 lines - IntercomData, callbacks |
| `main/mqtt_notification.cc` | +50 lines - Parse intercom messages |
| `main/application.h` | +15 lines - IntercomContext, handlers |
| `main/application.cc` | +210 lines - Handler implementations |

---

## 6. Backend Requirements

Backend cần gửi MQTT message với format:

```json
{
    "type": "intercom",
    "from_device_name": "tên thiết bị gửi",
    "from_device_id": "uuid",
    "message": "nội dung tin nhắn",
    "conversation_id": "unique-id",
    "reply_to_mac": "aa:bb:cc:dd:ee:ff"
}
```

Khi firmware gửi reply, backend nhận:
```json
{
    "type": "notification_speak",
    "content": "phản hồi từ user",
    "intercom": true,
    "conversation_id": "unique-id"
}
```

---

## 7. Configuration (Future)

```cpp
// Intercom configuration (có thể add vào menuconfig)
#define INTERCOM_LISTEN_DURATION_MS  10000   // 10s listening window
#define INTERCOM_TIMEOUT_MS          30000   // 30s conversation timeout
#define INTERCOM_SKIP_WAKEWORD       true    // Skip wake word in intercom mode
#define INTERCOM_NOTIFICATION_SOUND  true    // Play ding before message
```

---

## 8. Related Documents

- **PRD**: `plans/prd-walkie-talkie.md`
- **System Design**: `plans/design-walkie-talkie.md`
- **Backend Service**: `backend/src/app/services/intercom_service.py`
- **MCP Tools**: `backend/src/app/ai/plugins_func/functions/intercom.py`

---

**Version**: 1.0  
**Date**: 2026-02-03  
**Status**: ✅ Firmware Implemented, Pending Backend Integration
