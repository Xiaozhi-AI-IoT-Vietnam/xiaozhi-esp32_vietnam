# Server Backend Guide - Xiaozhi ESP32

## Tổng quan

Tài liệu này hướng dẫn implement server backend để giao tiếp với firmware Xiaozhi ESP32.

---

## 1. Kiến trúc hệ thống

```
┌─────────────────┐     MQTT (1883)      ┌─────────────────┐
│   ESP32 Device  │◄───────────────────►│  MQTT Broker    │
│                 │                      │  (Mosquitto)    │
└────────┬────────┘                      └────────┬────────┘
         │                                        │
         │ UDP Audio                              │ Subscribe/Publish
         │                                        │
         ▼                                        ▼
┌─────────────────┐                      ┌─────────────────┐
│  UDP Server     │◄────────────────────►│  Backend Server │
│  (Audio Stream) │                      │  (Node/Python)  │
└─────────────────┘                      └─────────────────┘
```

---

## 2. Vấn đề "Hết thời gian chờ phản hồi"

### Nguyên nhân

Khi device gửi **"hello" message**, server phải phản hồi **"hello response"** trong vòng **10 giây**.

### Flow kết nối

```
Device                        Server
  │                             │
  │───── MQTT: "hello" ────────►│
  │      {                      │
  │        "type": "hello",     │
  │        "version": 3,        │
  │        "transport": "udp",  │
  │        "features": {...},   │
  │        "audio_params": {...}│
  │      }                      │
  │                             │
  │ (Server phải phản hồi       │
  │  trong 10 giây)             │
  │                             │
  │◄──── MQTT: "hello" ─────────│
  │      {                      │
  │        "type": "hello",     │
  │        "session_id": "xxx", │
  │        "transport": "udp",  │
  │        "udp": {             │
  │          "ip": "1.2.3.4",   │
  │          "port": 8888       │
  │        },                   │
  │        "audio_params": {...}│
  │      }                      │
  │                             │
  │═══════ UDP Audio ══════════►│
  │◄═════ UDP Audio ════════════│
  │                             │
```

### Server cần implement

```python
# 1. Subscribe topic: device/{mac_address}/client2server
# 2. Khi nhận message type="hello", tạo session và UDP endpoint
# 3. Publish response về topic: device/{mac_address}/server2client

import json
import paho.mqtt.client as mqtt

def on_message(client, userdata, msg):
    topic = msg.topic
    payload = json.loads(msg.payload)
    
    # Extract MAC from topic: device/aa:bb:cc:dd:ee:ff/client2server
    parts = topic.split('/')
    mac_address = parts[1]
    
    if payload.get('type') == 'hello':
        # Create session and allocate UDP port
        session_id = create_session(mac_address)
        udp_server = allocate_udp_endpoint()
        
        # Build response
        response = {
            "type": "hello",
            "session_id": session_id,
            "transport": "udp",
            "udp": {
                "ip": udp_server.ip,
                "port": udp_server.port
            },
            "audio_params": {
                "format": "opus",
                "sample_rate": 16000,
                "channels": 1,
                "frame_duration": 60
            },
            "aes_key": generate_aes_key(),  # Base64 encoded 128-bit key
            "sample_rate": 16000
        }
        
        # Publish response - MUST be within 10 seconds!
        response_topic = f"device/{mac_address}/server2client"
        client.publish(response_topic, json.dumps(response))

# Subscribe pattern
client.subscribe("device/+/client2server")
```

---

## 3. MQTT Topics (ACTUAL từ Firmware)

### ⚠️ QUAN TRỌNG: Topics thực tế từ logs

Từ firmware logs:
```
I (8720) MQTT: MQTT Topics: publish=device-server, subscribe=device/98:a3:16:e8:df:48/#
I (28600) MQTT: Sending HELLO to topic [device-server]: {...}
```

### Topics hiện tại

| Direction | Topic | Description |
|-----------|-------|-------------|
| Device → Server | `device-server` | Tất cả devices publish đến topic CHUNG này |
| Server → Device | `device/{mac}/#` | Device subscribe wildcard topic |

### Server cần:

1. **Subscribe** topic: `device-server`
2. **Lấy MAC từ client_id**: MQTT client_id có format `device_98_a3_16_e8_df_48` → MAC là `98:a3:16:e8:df:48`
3. **Publish response** về topic match với device subscribe pattern

### Ví dụ extract MAC từ client_id:

```python
def extract_mac_from_client_id(client_id):
    # client_id: "device_98_a3_16_e8_df_48"
    if client_id.startswith("device_"):
        mac_parts = client_id[7:].split("_")  # Remove "device_" prefix
        return ":".join(mac_parts)  # Convert to "98:a3:16:e8:df:48"
    return None
```

### Topics response:

Firmware subscribe `device/98:a3:16:e8:df:48/#`, nên server có thể publish đến:
- `device/98:a3:16:e8:df:48/server` (cho push notifications)
- `device/98:a3:16:e8:df:48/hello` (cho hello response)  
- Hoặc bất kỳ sub-topic nào của `device/{mac}/`

---

## 4. Message Types

### 4.1 Hello (Handshake)

**Device gửi:**
```json
{
  "type": "hello",
  "version": 3,
  "transport": "udp",
  "features": {
    "aec": true,
    "mcp": true
  },
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

**Server phản hồi (BẮT BUỘC trong 10s):**
```json
{
  "type": "hello",
  "session_id": "unique-session-id",
  "transport": "udp",
  "udp": {
    "ip": "server-public-ip",
    "port": 8888
  },
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  },
  "aes_key": "base64-encoded-128-bit-key",
  "sample_rate": 16000
}
```

### 4.2 STT Result (Server → Device)

```json
{
  "type": "stt",
  "text": "xin chào"
}
```

### 4.3 TTS Request (Server → Device)

```json
{
  "type": "tts",
  "state": "start"
}
```

Sau đó gửi audio packets qua UDP.

### 4.4 Goodbye (End Session)

**Device gửi:**
```json
{
  "type": "goodbye",
  "session_id": "xxx"
}
```

---

## 5. Push Notifications (Intercom, Reminders)

### 5.1 Notification

```json
{
  "type": "notification",
  "title": "Nhắc nhở",
  "content": "Đến giờ uống thuốc rồi",
  "useTTS": true
}
```

### 5.2 Intercom (Walkie-Talkie)

**Gửi tin nhắn:**
```json
{
  "type": "intercom",
  "from_device_name": "phòng khách",
  "from_device_id": "uuid-of-sender",
  "message": "con ơi ăn cơm đi",
  "conversation_id": "unique-conversation-id",
  "reply_to_mac": "aa:bb:cc:dd:ee:ff"
}
```

**Phản hồi:**
```json
{
  "type": "intercom_reply",
  "from_device_name": "phòng ngủ",
  "from_device_id": "uuid-of-replier",
  "message": "dạ con biết rồi",
  "conversation_id": "unique-conversation-id"
}
```

---

## 6. UDP Audio Protocol

### Packet Format (16 bytes header + encrypted payload)

```
┌──────────┬───────┬─────────────┬──────────┬───────────┬──────────┬──────────────────┐
│ Type (1) │Flags  │ Payload Len │   SSRC   │ Timestamp │ Sequence │ Encrypted Payload│
│   0x01   │ (1)   │    (2)      │   (4)    │    (4)    │   (4)    │    (variable)    │
└──────────┴───────┴─────────────┴──────────┴───────────┴──────────┴──────────────────┘
```

- **AES-128-CTR** encryption
- **Opus** codec, 16kHz, mono
- Frame duration: 60ms

---

## 7. Checklist Debug "Hết thời gian phản hồi"

- [ ] Server có subscribe topic `device/+/client2server`?
- [ ] Server có xử lý message type="hello"?
- [ ] Server có publish response về `device/{mac}/server2client`?
- [ ] Response có gửi trong vòng **10 giây** không?
- [ ] UDP server có running và accessible không?
- [ ] AES key có Base64 encode đúng không?
- [ ] Firewall có mở port UDP không?

---

## 8. Example: Minimal Python Server

```python
import json
import paho.mqtt.client as mqtt
import socket
import threading
import base64
import os

MQTT_BROKER = "localhost"
UDP_PORT = 8888
SERVER_IP = "your-public-ip"

# Generate random AES key
def generate_aes_key():
    return base64.b64encode(os.urandom(16)).decode()

# Handle hello message
def handle_hello(client, mac_address, payload):
    session_id = f"session_{mac_address}_{os.urandom(4).hex()}"
    aes_key = generate_aes_key()
    
    response = {
        "type": "hello",
        "session_id": session_id,
        "transport": "udp",
        "udp": {
            "ip": SERVER_IP,
            "port": UDP_PORT
        },
        "audio_params": {
            "format": "opus",
            "sample_rate": 16000,
            "channels": 1,
            "frame_duration": 60
        },
        "aes_key": aes_key,
        "sample_rate": 16000
    }
    
    response_topic = f"device/{mac_address}/server2client"
    client.publish(response_topic, json.dumps(response))
    print(f"Sent hello response to {mac_address}")
    
    # Store session for audio handling
    sessions[session_id] = {
        "mac": mac_address,
        "aes_key": base64.b64decode(aes_key)
    }

def on_message(client, userdata, msg):
    try:
        topic = msg.topic
        payload = json.loads(msg.payload)
        
        # Extract MAC from topic
        parts = topic.split('/')
        if len(parts) >= 2:
            mac_address = parts[1]
            
            if payload.get('type') == 'hello':
                handle_hello(client, mac_address, payload)
            elif payload.get('type') == 'goodbye':
                print(f"Session ended for {mac_address}")
    except Exception as e:
        print(f"Error handling message: {e}")

def on_connect(client, userdata, flags, rc):
    print(f"Connected to MQTT broker with result code {rc}")
    client.subscribe("device/+/client2server")
    print("Subscribed to device/+/client2server")

# Start UDP server for audio
def udp_server_thread():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", UDP_PORT))
    print(f"UDP server listening on port {UDP_PORT}")
    
    while True:
        data, addr = sock.recvfrom(2048)
        # Process audio packet (decrypt, STT, etc.)
        process_audio_packet(data, addr)

sessions = {}

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(MQTT_BROKER, 1883, 60)

# Start UDP server in background
threading.Thread(target=udp_server_thread, daemon=True).start()

# Run MQTT loop
client.loop_forever()
```

---

## 9. Kết luận

Để fix "Hết thời gian chờ phản hồi":

1. ✅ Đảm bảo server subscribe đúng topic
2. ✅ Xử lý và phản hồi message type="hello" nhanh chóng (< 10s)
3. ✅ UDP endpoint phải accessible từ device
4. ✅ AES key phải valid

Nếu vẫn lỗi, check server logs để xác định vấn đề cụ thể.
