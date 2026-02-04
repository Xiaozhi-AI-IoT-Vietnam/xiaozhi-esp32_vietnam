# Firmware Intercom Contacts API

## 🎯 Trigger: Double-Press Wakeup Button

| Action | Behavior |
|--------|----------|
| **1x Press** | Normal wakeup → Start voice conversation với AI |
| **2x Press** (trong 500ms) | Open Intercom Contacts List UI |

### Button Detection Code

```cpp
// === Double-Press Detection ===
#define DOUBLE_PRESS_TIMEOUT 500  // ms

unsigned long lastPressTime = 0;
int pressCount = 0;

void IRAM_ATTR onWakeupButtonPress() {
    unsigned long now = millis();
    
    if (now - lastPressTime < DOUBLE_PRESS_TIMEOUT) {
        pressCount++;
    } else {
        pressCount = 1;
    }
    lastPressTime = now;
    
    if (pressCount >= 2) {
        pressCount = 0;
        openIntercomContacts();  // ← Double-press detected!
    }
}

void setup() {
    attachInterrupt(digitalPinToInterrupt(WAKEUP_PIN), onWakeupButtonPress, FALLING);
}

// Handle single press after timeout
void loop() {
    if (pressCount == 1 && millis() - lastPressTime > DOUBLE_PRESS_TIMEOUT) {
        pressCount = 0;
        startVoiceConversation();  // ← Single press
    }
}
```

### Intercom UI Flow

```
┌─────────────────────────────────────────────────────────┐
│             DOUBLE-PRESS INTERCOM FLOW                  │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  User double-press wakeup button                        │
│           │                                             │
│           ▼                                             │
│  ┌─────────────────────┐                                │
│  │  Fetching contacts  │ ← Show loading animation       │
│  │       ...           │                                │
│  └─────────────────────┘                                │
│           │                                             │
│           ▼ GET /api/v1/friends/intercom-contacts       │
│  ┌─────────────────────┐                                │
│  │ ● 2.8 LCD (Tôi)     │ ← Green dot = online           │
│  │ ○ 2MIC Hoài         │ ← Gray dot = offline           │
│  │ ● Phòng khách       │                                │
│  │                     │                                │
│  │ [▲] [▼] Select      │ ← Navigate with buttons        │
│  │ [OK] Call           │                                │
│  └─────────────────────┘                                │
│           │                                             │
│           ▼ User selects contact                        │
│  ┌─────────────────────┐                                │
│  │ 📞 Calling...       │                                │
│  │    2MIC Hoài        │                                │
│  │                     │                                │
│  │ [🎤] Recording...   │ ← User speaks message          │
│  └─────────────────────┘                                │
│           │                                             │
│           ▼ Send via MCP                                │
│  ┌─────────────────────┐                                │
│  │ ✓ Đã gửi!           │                                │
│  │   "Xin chào..."     │                                │
│  └─────────────────────┘                                │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

---


## API Endpoint

```
GET /api/v1/friends/intercom-contacts
```

### Authentication
Requires Bearer token from device authentication.

### Query Parameters
| Param | Type | Description |
|-------|------|-------------|
| `device_mac` | string | (Optional) Current device MAC to exclude from list |

### Response
```json
{
  "success": true,
  "contacts": [
    {
      "id": "019be06e-199d-793f-a7bb-a45c71217099",
      "name": "2.8 LCD",
      "mac": "98:a3:16:e8:df:48",
      "type": "own",
      "owner": "Tôi",
      "status": "online"
    },
    {
      "id": "019c2738-1b3c-7955-bf27-691841f126f5",
      "name": "2MIC Hoài",
      "mac": "90:70:69:14:fe:fc",
      "type": "friend",
      "owner": "Hoài",
      "status": "online"
    }
  ],
  "total": 2
}
```

### Contact Object
| Field | Type | Description |
|-------|------|-------------|
| `id` | string | Device UUID for API calls |
| `name` | string | Device display name |
| `mac` | string | Device MAC address |
| `type` | string | `"own"` = user's device, `"friend"` = friend's device |
| `owner` | string | Owner name ("Tôi" for own devices) |
| `status` | string | `"online"`, `"offline"`, or `"unknown"` |

---

## Firmware Usage Example

### 1. Fetch Contacts List

```cpp
#include <HTTPClient.h>
#include <ArduinoJson.h>

struct IntercomContact {
    String id;
    String name;
    String mac;
    String owner;
    bool isOnline;
};

std::vector<IntercomContact> contacts;

void fetchIntercomContacts() {
    HTTPClient http;
    
    String url = "https://xiaozhi-ai-iot.vn/api/v1/friends/intercom-contacts";
    url += "?device_mac=" + WiFi.macAddress();
    
    http.begin(url);
    http.addHeader("Authorization", "Bearer " + deviceToken);
    
    int httpCode = http.GET();
    
    if (httpCode == 200) {
        String payload = http.getString();
        
        DynamicJsonDocument doc(4096);
        deserializeJson(doc, payload);
        
        JsonArray contactsArray = doc["contacts"];
        contacts.clear();
        
        for (JsonObject c : contactsArray) {
            IntercomContact contact;
            contact.id = c["id"].as<String>();
            contact.name = c["name"].as<String>();
            contact.mac = c["mac"].as<String>();
            contact.owner = c["owner"].as<String>();
            contact.isOnline = (c["status"] == "online");
            contacts.push_back(contact);
        }
        
        Serial.printf("Loaded %d contacts\n", contacts.size());
    }
    
    http.end();
}
```

### 2. Display Contact List on LCD

```cpp
void showContactList() {
    display.clear();
    display.setFont(ArialMT_Plain_16);
    
    int y = 0;
    for (int i = 0; i < min(5, (int)contacts.size()); i++) {
        String status = contacts[i].isOnline ? "●" : "○";
        String line = status + " " + contacts[i].name;
        if (contacts[i].owner != "Tôi") {
            line += " (" + contacts[i].owner + ")";
        }
        display.drawString(0, y, line);
        y += 20;
    }
    
    display.display();
}
```

### 3. Send Intercom Message

```cpp
void sendIntercom(int contactIndex, String message) {
    if (contactIndex >= contacts.size()) return;
    
    IntercomContact& target = contacts[contactIndex];
    
    // Build MCP message
    DynamicJsonDocument doc(1024);
    doc["type"] = "mcp";
    doc["session_id"] = currentSessionId;
    
    JsonObject payload = doc.createNestedObject("payload");
    payload["action"] = "intercom_send";
    payload["target_mac"] = target.mac;
    payload["message"] = message;
    
    String json;
    serializeJson(doc, json);
    
    // Publish via MQTT
    mqtt.publish("device-server", json.c_str());
    
    Serial.printf("Sent intercom to %s: %s\n", 
                  target.name.c_str(), 
                  message.c_str());
}
```

### 4. Voice-Triggered Intercom (via AI)

User can say:
- "Gọi cho Hoài" → AI matches "Hoài" with contacts list
- "Nhắn tin cho 2.8 LCD" → AI finds device by name
- "Gửi tin nhắn cho thiết bị phòng khách" → AI matches by name

The AI plugin `intercom_send` handles this automatically when enabled.

---

## Integration with Voice Commands

The backend already has AI plugins for intercom:

- `intercom_send(target, message)` - Send message to target
- `intercom_reply(message)` - Reply to last received message  
- `intercom_end()` - End intercom session

These plugins can use the contacts list to resolve target names.

---

## Firmware Flow

```
┌─────────────────────────────────────────────────────────┐
│                   FIRMWARE INTERCOM FLOW                │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  1. Device boots / user opens Intercom menu             │
│     └─→ Call GET /api/v1/friends/intercom-contacts      │
│                                                         │
│  2. Display contact list on LCD                         │
│     └─→ Show name, owner, online status                 │
│                                                         │
│  3. User selects contact (button/touch/voice)           │
│     └─→ Get target MAC from contacts[i].mac             │
│                                                         │
│  4. User records/speaks message                         │
│     └─→ Convert to text (ASR) or use predefined         │
│                                                         │
│  5. Send via MCP                                        │
│     └─→ MQTT publish {"action":"intercom_send",...}     │
│                                                         │
│  6. Backend processes                                   │
│     └─→ Sends notification_speak to target device       │
│                                                         │
│  7. Target device receives TTS audio                    │
│     └─→ Plays message, can reply                        │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

---

*API created: 2026-02-04*
