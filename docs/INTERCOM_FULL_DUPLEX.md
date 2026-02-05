# Full Duplex Intercom Implementation Guide

> **Version:** 2.0  
> **Created:** 2026-02-05  
> **Status:** 📋 Ready for Implementation  
> **Target:** Backend + Firmware (Dev-Internal-srv branch)

---

## 📋 Tổng quan

Tài liệu này mô tả cách triển khai **Full Duplex Intercom** - tính năng cho phép 2 devices nói chuyện 2 chiều real-time, giữ nguyên giọng gốc (không qua TTS).

### Hiện trạng vs Mục tiêu

| Feature | Hiện tại (TTS-based) | Mục tiêu (Full Duplex) |
|---------|---------------------|------------------------|
| Audio Flow | Record → ASR → Text → TTS | Record → **Relay trực tiếp** |
| Giọng nói | Mất (TTS synthetic) | **Giữ nguyên** |
| Latency | Cao (2-4 giây) | **Thấp (<500ms)** |
| Mode | Half-duplex (1 way) | **Full-duplex (2 way)** |
| Hands-free | Không | **Có (AEC built-in)** |

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    FULL DUPLEX INTERCOM ARCHITECTURE                         │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Device A                      Server                      Device B         │
│  ─────────                     ──────                      ─────────        │
│                                                                             │
│  1. Select contact                                                          │
│  2. intercom_hello ──────────►│                                             │
│     {target_mac, transport}   │ Create IntercomSession                      │
│                               │                                             │
│                          ◄────│ intercom_ready                              │
│                               │ {session_id, udp}                           │
│                               │                                             │
│                               │ ─────────────────────────────────────►      │
│                               │ intercom_incoming                           │
│                               │ {from_name, session_id, udp}                │
│                                                                             │
│  3. UDP Bind ─────────────────┼───────────────────────────── UDP Bind      │
│                               │                                             │
│  4. [Recording + Sending]     │                    [Recording + Sending]   │
│     Opus Audio ──────────────►│◄────────────────── Opus Audio              │
│                               │        RELAY                                │
│     Opus Audio ◄──────────────│──────────────────► Opus Audio              │
│     [Playing]                 │                    [Playing]                │
│                                                                             │
│  5. intercom_end ────────────►│◄──────────────────intercom_end             │
│                               │ Cleanup session                             │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 📝 MQTT Message Types

### 1. `intercom_hello` (Device → Server)

Device gửi khi user chọn contact để gọi.

```json
{
    "type": "intercom_hello",
    "target_mac": "90:70:69:14:fe:fc",
    "mac_address": "ac:a7:04:f3:68:14",
    "version": 3,
    "transport": "udp",
    "audio_params": {
        "format": "opus",
        "sample_rate": 16000,
        "channels": 1,
        "frame_duration": 60
    }
}
```

### 2. `intercom_ready` (Server → Caller)

Server xác nhận session đã tạo, trả về UDP params.

```json
{
    "type": "intercom_ready",
    "session_id": "abc123def456",
    "target_device": "phòng ngủ",
    "target_status": "online",
    "udp": {
        "server": "103.xxx.xxx.xxx",
        "port": 8765,
        "key": "0123456789abcdef0123456789abcdef",
        "nonce": "0123456789abcdef01234567"
    }
}
```

### 3. `intercom_incoming` (Server → Callee)

Server thông báo có cuộc gọi đến.

```json
{
    "type": "intercom_incoming",
    "from_device": "ac:a7:04:f3:68:14",
    "from_name": "phòng khách",
    "session_id": "abc123def456",
    "udp": {
        "server": "103.xxx.xxx.xxx",
        "port": 8765,
        "key": "0123456789abcdef0123456789abcdef",
        "nonce": "0123456789abcdef01234567"
    }
}
```

### 4. `intercom_end` (Device ↔ Server)

Kết thúc cuộc gọi.

```json
{
    "type": "intercom_end",
    "session_id": "abc123def456",
    "mac_address": "ac:a7:04:f3:68:14",
    "reason": "user_ended"
}
```

### 5. `intercom_error` (Server → Device)

Báo lỗi.

```json
{
    "type": "intercom_error",
    "error": "target_offline",
    "message": "Thiết bị phòng ngủ đang offline"
}
```

---

# 🖥️ SERVER IMPLEMENTATION

## File: `backend/src/app/services/intercom_session.py`

```python
"""
Intercom Session - Full Duplex Audio Relay
"""
import os
import uuid
import asyncio
from datetime import datetime
from typing import Dict, Optional, Tuple
from dataclasses import dataclass, field
from loguru import logger


@dataclass
class IntercomSession:
    """Represents an active intercom call between two devices."""
    
    session_id: str
    caller_mac: str
    callee_mac: str
    caller_name: str = ""
    callee_name: str = ""
    
    # UDP binding
    caller_addr: Optional[Tuple[str, int]] = None
    callee_addr: Optional[Tuple[str, int]] = None
    
    # Encryption
    aes_key: bytes = field(default_factory=lambda: os.urandom(16))
    aes_nonce: bytes = field(default_factory=lambda: os.urandom(12))
    
    # State
    state: str = "pending"  # pending, active, ended
    created_at: datetime = field(default_factory=datetime.utcnow)
    last_activity: datetime = field(default_factory=datetime.utcnow)
    
    @property
    def aes_key_hex(self) -> str:
        return self.aes_key.hex()
    
    @property
    def aes_nonce_hex(self) -> str:
        return self.aes_nonce.hex()
    
    def bind_udp(self, mac: str, addr: Tuple[str, int]) -> bool:
        """Bind UDP address for a device."""
        if mac == self.caller_mac:
            self.caller_addr = addr
            logger.info(f"[Intercom] Caller {mac} bound to UDP {addr}")
            return True
        elif mac == self.callee_mac:
            self.callee_addr = addr
            logger.info(f"[Intercom] Callee {mac} bound to UDP {addr}")
            return True
        return False
    
    def is_fully_bound(self) -> bool:
        """Check if both devices are bound."""
        return self.caller_addr is not None and self.callee_addr is not None
    
    def get_relay_target(self, from_addr: Tuple[str, int]) -> Optional[Tuple[str, int]]:
        """Get the target address to relay audio to."""
        if from_addr == self.caller_addr:
            return self.callee_addr
        elif from_addr == self.callee_addr:
            return self.caller_addr
        return None
    
    def update_activity(self):
        """Update last activity timestamp."""
        self.last_activity = datetime.utcnow()
        

class IntercomSessionManager:
    """Manages all active intercom sessions."""
    
    _instance = None
    
    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
            cls._instance._sessions: Dict[str, IntercomSession] = {}
            cls._instance._mac_to_session: Dict[str, str] = {}
            cls._instance._addr_to_session: Dict[Tuple[str, int], str] = {}
        return cls._instance
    
    def create_session(
        self,
        caller_mac: str,
        callee_mac: str,
        caller_name: str = "",
        callee_name: str = "",
    ) -> IntercomSession:
        """Create a new intercom session."""
        session_id = uuid.uuid4().hex[:16]
        
        session = IntercomSession(
            session_id=session_id,
            caller_mac=caller_mac,
            callee_mac=callee_mac,
            caller_name=caller_name,
            callee_name=callee_name,
        )
        
        self._sessions[session_id] = session
        self._mac_to_session[caller_mac] = session_id
        self._mac_to_session[callee_mac] = session_id
        
        logger.info(f"[Intercom] Created session {session_id}: {caller_mac} → {callee_mac}")
        return session
    
    def get_session(self, session_id: str) -> Optional[IntercomSession]:
        """Get session by ID."""
        return self._sessions.get(session_id)
    
    def get_session_by_mac(self, mac: str) -> Optional[IntercomSession]:
        """Get session by device MAC."""
        session_id = self._mac_to_session.get(mac)
        if session_id:
            return self._sessions.get(session_id)
        return None
    
    def get_session_by_addr(self, addr: Tuple[str, int]) -> Optional[IntercomSession]:
        """Get session by UDP address."""
        session_id = self._addr_to_session.get(addr)
        if session_id:
            return self._sessions.get(session_id)
        return None
    
    def bind_udp(self, session_id: str, mac: str, addr: Tuple[str, int]) -> bool:
        """Bind UDP address for a device in session."""
        session = self._sessions.get(session_id)
        if session and session.bind_udp(mac, addr):
            self._addr_to_session[addr] = session_id
            if session.is_fully_bound():
                session.state = "active"
                logger.info(f"[Intercom] Session {session_id} is now ACTIVE (full duplex)")
            return True
        return False
    
    def end_session(self, session_id: str, reason: str = "ended") -> None:
        """End and cleanup a session."""
        session = self._sessions.get(session_id)
        if not session:
            return
        
        session.state = "ended"
        
        # Cleanup mappings
        self._mac_to_session.pop(session.caller_mac, None)
        self._mac_to_session.pop(session.callee_mac, None)
        
        if session.caller_addr:
            self._addr_to_session.pop(session.caller_addr, None)
        if session.callee_addr:
            self._addr_to_session.pop(session.callee_addr, None)
        
        self._sessions.pop(session_id, None)
        
        logger.info(f"[Intercom] Session {session_id} ended: {reason}")
    
    def cleanup_stale_sessions(self, max_age_seconds: int = 300) -> int:
        """Cleanup sessions older than max_age_seconds."""
        now = datetime.utcnow()
        stale = []
        
        for session_id, session in self._sessions.items():
            age = (now - session.last_activity).total_seconds()
            if age > max_age_seconds:
                stale.append(session_id)
        
        for session_id in stale:
            self.end_session(session_id, reason="timeout")
        
        return len(stale)
```

---

## File: `backend/src/app/services/mqtt_device_handler.py` (Update)

Add handler for `intercom_hello`:

```python
# Add to existing mqtt_device_handler.py

from .intercom_session import IntercomSessionManager

class MqttDeviceHandler:
    
    def __init__(self):
        # ... existing init ...
        self.intercom_manager = IntercomSessionManager()
    
    async def _handle_message(self, topic: str, payload: dict):
        msg_type = payload.get("type", "")
        
        # ... existing handlers ...
        
        if msg_type == "intercom_hello":
            await self._handle_intercom_hello(topic, payload)
        elif msg_type == "intercom_end":
            await self._handle_intercom_end(topic, payload)
    
    async def _handle_intercom_hello(self, topic: str, payload: dict):
        """Handle intercom call initiation."""
        caller_mac = payload.get("mac_address", "")
        target_mac = payload.get("target_mac", "")
        
        if not caller_mac or not target_mac:
            await self._send_intercom_error(caller_mac, "missing_params", "Missing mac_address or target_mac")
            return
        
        # Get device names
        caller_device = await self._get_device_by_mac(caller_mac)
        callee_device = await self._get_device_by_mac(target_mac)
        
        if not callee_device:
            await self._send_intercom_error(caller_mac, "target_not_found", f"Thiết bị {target_mac} không tìm thấy")
            return
        
        caller_name = caller_device.display_name if caller_device else caller_mac
        callee_name = callee_device.display_name if callee_device else target_mac
        
        # Check if callee is online
        callee_online = await self._is_device_online(target_mac)
        
        # Create session
        session = self.intercom_manager.create_session(
            caller_mac=caller_mac,
            callee_mac=target_mac,
            caller_name=caller_name,
            callee_name=callee_name,
        )
        
        # Get UDP server config
        udp_config = self._get_udp_config(session)
        
        # Send intercom_ready to caller
        await self.mqtt_service.publish(
            topic=f"device/{caller_mac}/server",
            payload={
                "type": "intercom_ready",
                "session_id": session.session_id,
                "target_device": callee_name,
                "target_status": "online" if callee_online else "offline",
                "udp": udp_config,
            }
        )
        
        # Send intercom_incoming to callee
        if callee_online:
            await self.mqtt_service.publish(
                topic=f"device/{target_mac}/server",
                payload={
                    "type": "intercom_incoming",
                    "from_device": caller_mac,
                    "from_name": caller_name,
                    "session_id": session.session_id,
                    "udp": udp_config,
                }
            )
        
        logger.info(f"[Intercom] Call started: {caller_name} → {callee_name} (session: {session.session_id})")
    
    async def _handle_intercom_end(self, topic: str, payload: dict):
        """Handle intercom session end."""
        session_id = payload.get("session_id", "")
        mac = payload.get("mac_address", "")
        reason = payload.get("reason", "user_ended")
        
        session = self.intercom_manager.get_session(session_id)
        if not session:
            return
        
        # Notify the other party
        other_mac = session.callee_mac if mac == session.caller_mac else session.caller_mac
        await self.mqtt_service.publish(
            topic=f"device/{other_mac}/server",
            payload={
                "type": "intercom_end",
                "session_id": session_id,
                "reason": reason,
            }
        )
        
        # End session
        self.intercom_manager.end_session(session_id, reason)
    
    def _get_udp_config(self, session: "IntercomSession") -> dict:
        """Get UDP configuration for session."""
        from ..config.config_loader import load_config
        config = load_config()
        
        udp_host = config.get("server", {}).get("udp_host", "")
        udp_port = config.get("server", {}).get("udp_port", 8765)
        
        # Use public IP if available
        if not udp_host:
            import socket
            udp_host = socket.gethostbyname(socket.gethostname())
        
        return {
            "server": udp_host,
            "port": udp_port,
            "key": session.aes_key_hex,
            "nonce": session.aes_nonce_hex,
        }
    
    async def _send_intercom_error(self, mac: str, error: str, message: str):
        """Send intercom error to device."""
        await self.mqtt_service.publish(
            topic=f"device/{mac}/server",
            payload={
                "type": "intercom_error",
                "error": error,
                "message": message,
            }
        )
```

---

## File: `backend/src/app/services/udp_audio_server.py` (Update)

Add intercom relay logic:

```python
# Add to existing udp_audio_server.py

from .intercom_session import IntercomSessionManager

class UdpAudioServer:
    
    def __init__(self):
        # ... existing init ...
        self.intercom_manager = IntercomSessionManager()
    
    async def handle_packet(self, data: bytes, addr: Tuple[str, int]):
        """Handle incoming UDP packet."""
        
        # Check if this is a BIND packet: "HELLO:session_id:mac_address"
        if data.startswith(b"HELLO:"):
            await self._handle_intercom_bind(data, addr)
            return
        
        # Check if this is an intercom session
        session = self.intercom_manager.get_session_by_addr(addr)
        if session:
            await self._relay_intercom_audio(session, data, addr)
            return
        
        # Fall through to existing AI audio handling
        await self._handle_ai_audio(data, addr)
    
    async def _handle_intercom_bind(self, data: bytes, addr: Tuple[str, int]):
        """Handle intercom UDP bind request."""
        try:
            parts = data.decode().split(":")
            if len(parts) >= 3:
                session_id = parts[1]
                mac = parts[2]
                
                if self.intercom_manager.bind_udp(session_id, mac, addr):
                    # Send ACK
                    await self._send_udp(addr, b"ACK")
                    logger.info(f"[Intercom] Bound {mac} to {addr} for session {session_id}")
                else:
                    await self._send_udp(addr, b"ERR:invalid_session")
        except Exception as e:
            logger.error(f"[Intercom] Bind error: {e}")
    
    async def _relay_intercom_audio(
        self, 
        session: "IntercomSession", 
        data: bytes, 
        from_addr: Tuple[str, int]
    ):
        """Relay audio between intercom participants."""
        target_addr = session.get_relay_target(from_addr)
        
        if target_addr:
            # Relay directly - no processing!
            await self._send_udp(target_addr, data)
            session.update_activity()
        else:
            logger.warning(f"[Intercom] No target for {from_addr} in session {session.session_id}")
    
    async def _send_udp(self, addr: Tuple[str, int], data: bytes):
        """Send UDP packet."""
        self.transport.sendto(data, addr)
```

---

# 📱 FIRMWARE IMPLEMENTATION

## File: `main/device_state.h` (Update)

```cpp
#ifndef _DEVICE_STATE_H_
#define _DEVICE_STATE_H_

enum DeviceState {
    kDeviceStateUnknown,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    kDeviceStateUpgrading,
    kDeviceStateActivating,
    kDeviceStateAudioTesting,
    kDeviceStateFatalError,
    // NEW: Intercom states
    kDeviceStateIntercomCalling,   // Đang gọi, chờ kết nối
    kDeviceStateIntercomActive,    // Đang trong cuộc gọi (full duplex)
    kDeviceStateIntercomIncoming,  // Có cuộc gọi đến
};

#endif // _DEVICE_STATE_H_
```

---

## File: `main/mqtt_notification.h` (Update)

Add new Intercom message types:

```cpp
// Add to existing IntercomData struct

struct IntercomData {
  std::string type;              // "intercom", "intercom_reply", "intercom_ready", "intercom_incoming", "intercom_end", "intercom_error"
  std::string from_device_name;
  std::string from_device_id;
  std::string message;
  std::string conversation_id;
  std::string reply_to_mac;
  bool is_reply;
  
  // NEW: Full Duplex fields
  std::string session_id;
  std::string target_device;
  std::string target_status;     // "online" / "offline"
  std::string error;
  std::string error_message;
  
  // UDP config
  struct {
    std::string server;
    int port = 0;
    std::string key;
    std::string nonce;
  } udp;
};
```

---

## File: `main/mqtt_notification.cc` (Update)

Add parsing for new message types:

```cpp
void MqttNotification::ParseIntercom(const cJSON *root, IntercomData &intercom) {
    // ... existing parsing ...
    
    // NEW: Parse session_id
    auto session_id = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(session_id)) {
        intercom.session_id = session_id->valuestring;
    }
    
    // NEW: Parse target_device
    auto target_device = cJSON_GetObjectItem(root, "target_device");
    if (cJSON_IsString(target_device)) {
        intercom.target_device = target_device->valuestring;
    }
    
    // NEW: Parse target_status
    auto target_status = cJSON_GetObjectItem(root, "target_status");
    if (cJSON_IsString(target_status)) {
        intercom.target_status = target_status->valuestring;
    }
    
    // NEW: Parse error
    auto error = cJSON_GetObjectItem(root, "error");
    if (cJSON_IsString(error)) {
        intercom.error = error->valuestring;
    }
    
    auto error_message = cJSON_GetObjectItem(root, "message");
    if (cJSON_IsString(error_message) && intercom.type == "intercom_error") {
        intercom.error_message = error_message->valuestring;
    }
    
    // NEW: Parse UDP config
    auto udp = cJSON_GetObjectItem(root, "udp");
    if (cJSON_IsObject(udp)) {
        auto server = cJSON_GetObjectItem(udp, "server");
        auto port = cJSON_GetObjectItem(udp, "port");
        auto key = cJSON_GetObjectItem(udp, "key");
        auto nonce = cJSON_GetObjectItem(udp, "nonce");
        
        if (cJSON_IsString(server)) intercom.udp.server = server->valuestring;
        if (cJSON_IsNumber(port)) intercom.udp.port = port->valueint;
        if (cJSON_IsString(key)) intercom.udp.key = key->valuestring;
        if (cJSON_IsString(nonce)) intercom.udp.nonce = nonce->valuestring;
    }
}

void MqttNotification::HandleMessage(const char *topic, const char *data, int len) {
    // ... existing code ...
    
    auto type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) {
        cJSON_Delete(root);
        return;
    }
    
    // Handle all intercom-related messages
    if (strcmp(type->valuestring, "intercom") == 0 ||
        strcmp(type->valuestring, "intercom_reply") == 0 ||
        strcmp(type->valuestring, "intercom_ready") == 0 ||
        strcmp(type->valuestring, "intercom_incoming") == 0 ||
        strcmp(type->valuestring, "intercom_end") == 0 ||
        strcmp(type->valuestring, "intercom_error") == 0) {
        
        IntercomData intercom;
        intercom.type = type->valuestring;
        ParseIntercom(root, intercom);
        
        if (on_intercom_) {
            on_intercom_(intercom);
        }
    }
    
    // ... rest of existing code ...
}
```

---

## File: `main/application.h` (Update)

Add Full Duplex Intercom context:

```cpp
#ifdef CONFIG_ENABLE_MQTT_NOTIFICATIONS
  // Full Duplex Intercom context
  struct FullDuplexIntercomContext {
    bool active = false;
    std::string session_id;
    std::string target_mac;
    std::string target_name;
    
    // UDP
    std::string udp_server;
    int udp_port = 0;
    std::string aes_key;
    std::string aes_nonce;
    
    // Tasks
    TaskHandle_t send_task = nullptr;
    TaskHandle_t recv_task = nullptr;
    
    // State
    bool udp_bound = false;
    uint32_t last_activity_ms = 0;
  };
  FullDuplexIntercomContext fd_intercom_;
  
  // Full Duplex handlers
  void HandleIntercomReady(const IntercomData &data);
  void HandleIntercomIncoming(const IntercomData &data);
  void HandleIntercomEndMsg(const IntercomData &data);
  void HandleIntercomError(const IntercomData &data);
  
  void StartFullDuplexIntercom();
  void StopFullDuplexIntercom(const std::string &reason = "user_ended");
  
  static void IntercomSendTask(void* param);
  static void IntercomRecvTask(void* param);
#endif
```

---

## File: `main/application.cc` (Update)

### 1. Update OnIntercom handler

```cpp
void Application::OnIntercom(const IntercomData &intercom) {
  ESP_LOGI(TAG, "[INTERCOM] OnIntercom: type=%s, from=%s",
           intercom.type.c_str(), intercom.from_device_name.c_str());
  
  // Route based on type
  if (intercom.type == "intercom_ready") {
    HandleIntercomReady(intercom);
  }
  else if (intercom.type == "intercom_incoming") {
    HandleIntercomIncoming(intercom);
  }
  else if (intercom.type == "intercom_end") {
    HandleIntercomEndMsg(intercom);
  }
  else if (intercom.type == "intercom_error") {
    HandleIntercomError(intercom);
  }
  else if (intercom.is_reply) {
    HandleIntercomReply(intercom);  // Existing TTS-based
  }
  else {
    HandleIntercomMessage(intercom);  // Existing TTS-based
  }
}
```

### 2. Add Full Duplex handlers

```cpp
void Application::HandleIntercomReady(const IntercomData &data) {
  if (device_state_ != kDeviceStateIntercomCalling) {
    ESP_LOGW(TAG, "[INTERCOM] Unexpected intercom_ready");
    return;
  }
  
  ESP_LOGI(TAG, "[INTERCOM] Ready! Session: %s, Target: %s (%s)",
           data.session_id.c_str(), data.target_device.c_str(), data.target_status.c_str());
  
  // Save session info
  fd_intercom_.session_id = data.session_id;
  fd_intercom_.target_name = data.target_device;
  fd_intercom_.udp_server = data.udp.server;
  fd_intercom_.udp_port = data.udp.port;
  fd_intercom_.aes_key = data.udp.key;
  fd_intercom_.aes_nonce = data.udp.nonce;
  
  // Show status
  auto display = Board::GetInstance().GetDisplay();
  if (data.target_status == "offline") {
    display->SetChatMessage("system", ("📞 " + data.target_device + " (Offline)").c_str());
  } else {
    display->SetChatMessage("system", ("📞 Đang gọi " + data.target_device + "...").c_str());
  }
  
  // Start full duplex
  StartFullDuplexIntercom();
}

void Application::HandleIntercomIncoming(const IntercomData &data) {
  ESP_LOGI(TAG, "[INTERCOM] Incoming call from %s", data.from_device_name.c_str());
  
  // Save session info
  fd_intercom_.session_id = data.session_id;
  fd_intercom_.target_mac = data.from_device_id;
  fd_intercom_.target_name = data.from_device_name;
  fd_intercom_.udp_server = data.udp.server;
  fd_intercom_.udp_port = data.udp.port;
  fd_intercom_.aes_key = data.udp.key;
  fd_intercom_.aes_nonce = data.udp.nonce;
  
  // Show notification
  auto display = Board::GetInstance().GetDisplay();
  display->ShowNotification(("📞 Cuộc gọi từ " + data.from_device_name).c_str());
  
  // Play notification sound
  audio_service_.PlaySound(Lang::Sounds::OGG_VIBRATION);
  
  // Auto-answer and start full duplex
  SetDeviceState(kDeviceStateIntercomIncoming);
  
  // Small delay then auto-answer
  Schedule([this]() {
    vTaskDelay(pdMS_TO_TICKS(500));
    StartFullDuplexIntercom();
  });
}

void Application::HandleIntercomEndMsg(const IntercomData &data) {
  ESP_LOGI(TAG, "[INTERCOM] Call ended: %s", data.session_id.c_str());
  StopFullDuplexIntercom("remote_ended");
}

void Application::HandleIntercomError(const IntercomData &data) {
  ESP_LOGE(TAG, "[INTERCOM] Error: %s - %s", data.error.c_str(), data.error_message.c_str());
  
  auto display = Board::GetInstance().GetDisplay();
  display->ShowNotification(data.error_message.c_str(), 3000);
  
  StopFullDuplexIntercom("error");
}
```

### 3. Start Full Duplex Intercom

```cpp
void Application::StartFullDuplexIntercom() {
  if (fd_intercom_.active) {
    ESP_LOGW(TAG, "[INTERCOM] Already active");
    return;
  }
  
  fd_intercom_.active = true;
  SetDeviceState(kDeviceStateIntercomActive);
  
  auto display = Board::GetInstance().GetDisplay();
  display->SetStatus("📞 Intercom");
  display->SetChatMessage("system", ("🎤 Rảnh tay với " + fd_intercom_.target_name).c_str());
  
  // Start send task (continuous recording)
  xTaskCreate(
    IntercomSendTask,
    "intercom_send",
    8192,
    this,
    5,
    &fd_intercom_.send_task
  );
  
  // Start receive task (continuous playback)
  xTaskCreate(
    IntercomRecvTask,
    "intercom_recv",
    8192,
    this,
    5,
    &fd_intercom_.recv_task
  );
  
  ESP_LOGI(TAG, "[INTERCOM] Full duplex started");
}

void Application::StopFullDuplexIntercom(const std::string &reason) {
  if (!fd_intercom_.active) {
    return;
  }
  
  ESP_LOGI(TAG, "[INTERCOM] Stopping: %s", reason.c_str());
  
  fd_intercom_.active = false;
  
  // Stop tasks
  if (fd_intercom_.send_task) {
    vTaskDelete(fd_intercom_.send_task);
    fd_intercom_.send_task = nullptr;
  }
  if (fd_intercom_.recv_task) {
    vTaskDelete(fd_intercom_.recv_task);
    fd_intercom_.recv_task = nullptr;
  }
  
  // Send intercom_end if we initiated
  if (reason == "user_ended") {
    // TODO: Send via MQTT
  }
  
  // Reset context
  fd_intercom_.session_id = "";
  fd_intercom_.target_mac = "";
  fd_intercom_.target_name = "";
  fd_intercom_.udp_bound = false;
  
  // Return to idle
  SetDeviceState(kDeviceStateIdle);
  
  auto display = Board::GetInstance().GetDisplay();
  display->ShowNotification("Kết thúc cuộc gọi", 2000);
}
```

### 4. Audio Tasks

```cpp
void Application::IntercomSendTask(void* param) {
  auto* app = static_cast<Application*>(param);
  auto& ctx = app->fd_intercom_;
  
  ESP_LOGI(TAG, "[INTERCOM] Send task started");
  
  // Create UDP socket
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    ESP_LOGE(TAG, "[INTERCOM] Failed to create UDP socket");
    vTaskDelete(NULL);
    return;
  }
  
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(ctx.udp_port);
  inet_pton(AF_INET, ctx.udp_server.c_str(), &server_addr.sin_addr);
  
  // Send HELLO to bind
  std::string hello = "HELLO:" + ctx.session_id + ":" + SystemInfo::GetMacAddress();
  sendto(sock, hello.c_str(), hello.length(), 0, 
         (struct sockaddr*)&server_addr, sizeof(server_addr));
  
  ESP_LOGI(TAG, "[INTERCOM] Sent HELLO, waiting for ACK...");
  
  // Wait for ACK
  char ack_buf[64];
  struct sockaddr_in from_addr;
  socklen_t from_len = sizeof(from_addr);
  
  struct timeval tv;
  tv.tv_sec = 5;
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  
  int len = recvfrom(sock, ack_buf, sizeof(ack_buf) - 1, 0,
                     (struct sockaddr*)&from_addr, &from_len);
  if (len > 0) {
    ack_buf[len] = '\0';
    if (strcmp(ack_buf, "ACK") == 0) {
      ctx.udp_bound = true;
      ESP_LOGI(TAG, "[INTERCOM] UDP bound successfully");
    }
  }
  
  if (!ctx.udp_bound) {
    ESP_LOGE(TAG, "[INTERCOM] Failed to bind UDP");
    close(sock);
    vTaskDelete(NULL);
    return;
  }
  
  // Remove timeout for streaming
  tv.tv_sec = 0;
  tv.tv_usec = 100000; // 100ms
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  
  // Start audio capture
  auto& audio = app->audio_service_;
  
  while (ctx.active) {
    // Read from mic (60ms frames = 960 samples @ 16kHz)
    // Encode with Opus
    // Encrypt with AES-128-GCM
    // Send via UDP
    
    // TODO: Implement audio capture and encoding
    
    vTaskDelay(pdMS_TO_TICKS(60));  // 60ms frame
  }
  
  close(sock);
  ESP_LOGI(TAG, "[INTERCOM] Send task ended");
  vTaskDelete(NULL);
}

void Application::IntercomRecvTask(void* param) {
  auto* app = static_cast<Application*>(param);
  auto& ctx = app->fd_intercom_;
  
  ESP_LOGI(TAG, "[INTERCOM] Recv task started");
  
  // Create UDP socket for receiving
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    ESP_LOGE(TAG, "[INTERCOM] Failed to create recv UDP socket");
    vTaskDelete(NULL);
    return;
  }
  
  // Bind to any port (server knows our address from HELLO)
  struct sockaddr_in local_addr;
  local_addr.sin_family = AF_INET;
  local_addr.sin_port = htons(0);
  local_addr.sin_addr.s_addr = INADDR_ANY;
  
  if (bind(sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
    ESP_LOGE(TAG, "[INTERCOM] Failed to bind recv socket");
    close(sock);
    vTaskDelete(NULL);
    return;
  }
  
  uint8_t recv_buf[1500];
  struct sockaddr_in from_addr;
  socklen_t from_len = sizeof(from_addr);
  
  while (ctx.active) {
    // Wait for UDP packet
    int len = recvfrom(sock, recv_buf, sizeof(recv_buf), 0,
                       (struct sockaddr*)&from_addr, &from_len);
    
    if (len > 0) {
      // Decrypt with AES-128-GCM
      // Decode Opus
      // Push to audio output
      
      // TODO: Implement audio decryption and playback
      
      ctx.last_activity_ms = esp_timer_get_time() / 1000;
    }
  }
  
  close(sock);
  ESP_LOGI(TAG, "[INTERCOM] Recv task ended");
  vTaskDelete(NULL);
}
```

### 5. Update OnIntercomContactSelected

```cpp
void Application::OnIntercomContactSelected(const IntercomContact& contact) {
  Schedule([this, contact]() {
    ESP_LOGI(TAG, "📞 Selected contact: %s (MAC: %s)", contact.name.c_str(), contact.mac.c_str());
    
    // Hide Intercom UI
    intercom_contacts_ui_.Hide();
    
    // Set state to calling
    SetDeviceState(kDeviceStateIntercomCalling);
    fd_intercom_.target_mac = contact.mac;
    fd_intercom_.target_name = contact.name;
    
    // Show calling UI
    auto display = Board::GetInstance().GetDisplay();
    display->SetChatMessage("system", ("📞 Đang gọi " + contact.name + "...").c_str());
    
    // Send intercom_hello via MQTT
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "intercom_hello");
    cJSON_AddStringToObject(root, "target_mac", contact.mac.c_str());
    cJSON_AddStringToObject(root, "mac_address", SystemInfo::GetMacAddress().c_str());
    cJSON_AddNumberToObject(root, "version", 3);
    cJSON_AddStringToObject(root, "transport", "udp");
    
    // Audio params
    cJSON *audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio_params, "channels", 1);
    cJSON_AddNumberToObject(audio_params, "frame_duration", 60);
    cJSON_AddItemToObject(root, "audio_params", audio_params);
    
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
      ESP_LOGI(TAG, "[INTERCOM] Sending intercom_hello: %s", json_str);
      
      // Publish via MQTT
      if (mqtt_notification_) {
        mqtt_notification_->PublishToServer(json_str);
      }
      
      free(json_str);
    }
    cJSON_Delete(root);
  });
}
```

---

## 📋 Implementation Checklist

### Server Tasks

| # | Task | File | Priority | Est. |
|---|------|------|----------|------|
| S1 | IntercomSession class | `intercom_session.py` | P0 | 2h |
| S2 | IntercomSessionManager | `intercom_session.py` | P0 | 1h |
| S3 | `intercom_hello` handler | `mqtt_device_handler.py` | P0 | 2h |
| S4 | `intercom_end` handler | `mqtt_device_handler.py` | P0 | 1h |
| S5 | UDP intercom bind | `udp_audio_server.py` | P0 | 2h |
| S6 | UDP intercom relay | `udp_audio_server.py` | P0 | 2h |
| S7 | Session cleanup | `intercom_session.py` | P1 | 1h |
| **Total** | | | | **~11h** |

### Firmware Tasks

| # | Task | File | Priority | Est. |
|---|------|------|----------|------|
| F1 | Add new DeviceStates | `device_state.h` | P0 | 30m |
| F2 | Update IntercomData struct | `mqtt_notification.h` | P0 | 30m |
| F3 | Parse new message types | `mqtt_notification.cc` | P0 | 1h |
| F4 | Add FullDuplexIntercomContext | `application.h` | P0 | 30m |
| F5 | HandleIntercomReady | `application.cc` | P0 | 1h |
| F6 | HandleIntercomIncoming | `application.cc` | P0 | 1h |
| F7 | StartFullDuplexIntercom | `application.cc` | P0 | 2h |
| F8 | IntercomSendTask (UDP) | `application.cc` | P0 | 3h |
| F9 | IntercomRecvTask (UDP) | `application.cc` | P0 | 3h |
| F10 | Update OnIntercomContactSelected | `application.cc` | P0 | 1h |
| F11 | StopFullDuplexIntercom | `application.cc` | P0 | 1h |
| F12 | Exit on back button | `application.cc` | P1 | 30m |
| **Total** | | | | **~15h** |

---

## 🔄 Testing

### Test 1: Basic Call

1. Device A: Double-press → Select contact B
2. Check: Device A shows "Đang gọi B..."
3. Check: Device B shows "Cuộc gọi từ A"
4. Check: Both devices enter full duplex mode
5. Speak on A → Hear on B
6. Speak on B → Hear on A
7. Exit on either device

### Test 2: Offline Target

1. Device A: Call offline Device B
2. Check: Shows "B (Offline)"
3. Check: Still allows call (for future offline message)

### Test 3: Timeout

1. Start call, don't speak for 5 min
2. Check: Session auto-ends

---

## 💡 Additional Suggestions

### 1. Mode Fallback

```cpp
// In StartFullDuplexIntercom()
if (!audio_service_.HasAEC()) {
  ESP_LOGW(TAG, "Board không hỗ trợ AEC, chuyển sang PTT mode");
  fd_intercom_.mode = kIntercomModePTT;
  return StartPTTIntercom();
}
```

### 2. Visual Indicator

```cpp
// Show audio level indicator during call
void UpdateIntercomUI() {
  int mic_level = audio_service_.GetMicLevel();
  int spk_level = audio_service_.GetSpeakerLevel();
  
  display->SetIntercomLevels(mic_level, spk_level);
}
```

### 3. Call Duration Timer

```cpp
void Application::IntercomTimerTask(void* param) {
  uint32_t start_time = millis();
  while (fd_intercom_.active) {
    uint32_t elapsed = (millis() - start_time) / 1000;
    int minutes = elapsed / 60;
    int seconds = elapsed % 60;
    
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", minutes, seconds);
    display->SetCallDuration(time_str);
    
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  vTaskDelete(NULL);
}
```

---

**Version:** 2.1  
**Date:** 2026-02-05  
**Status:** 📋 Ready for Implementation
