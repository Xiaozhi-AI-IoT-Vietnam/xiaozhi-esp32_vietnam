# Backend Guide: Intercom (Walkie-Talkie) Feature

> **Document Version:** 1.0  
> **Created:** 2026-02-03  
> **Target:** Xiaozhi Backend Server  
> **Status:** 📋 Ready for Implementation

---

## 📋 Tổng quan

Backend cần implement các components sau để hỗ trợ tính năng Intercom:

1. **MCP Tool `intercom.send`** - Gửi tin nhắn đến device khác
2. **IntercomService** - Quản lý conversations
3. **Device Resolver** - Tìm device theo tên
4. **Reply Handler** - Xử lý phản hồi từ device

---

## 1. MCP Tool: `intercom.send`

### 1.1 Function Definition

```python
# backend/src/app/ai/plugins_func/functions/intercom.py

from app.services.intercom_service import IntercomService
from app.core.mcp import tool

@tool("intercom.send")
async def intercom_send(
    conn,  # Connection context
    target_device: str,  # Tên device đích (vd: "phòng ngủ")
    message: str         # Nội dung tin nhắn
) -> str:
    """
    Gửi tin nhắn voice đến thiết bị khác trong cùng account.
    
    Trigger phrases:
    - "Gọi phòng ngủ - con ơi ăn cơm đi"
    - "Nhắn phòng làm việc - về ăn cơm"
    - "Nói với phòng khách - có khách đến"
    
    Args:
        target_device: Tên thiết bị đích
        message: Nội dung tin nhắn cần gửi
        
    Returns:
        Kết quả gửi tin nhắn
    """
    service = IntercomService()
    
    # Get sender info from connection context
    sender_device_id = conn.device_id
    sender_device_name = conn.device_name or "thiết bị"
    sender_mac = conn.headers.get("device-id", "")
    user_id = conn.headers.get("user-id", "")
    
    result = await service.send_intercom(
        from_device_id=sender_device_id,
        from_device_name=sender_device_name,
        from_mac=sender_mac,
        target_device_name=target_device,
        message=message,
        user_id=user_id,
    )
    
    if result.success:
        return f"Đã gửi tin nhắn đến {target_device}: {message}"
    else:
        return f"Không thể gửi tin nhắn: {result.error}"
```

### 1.2 Tool Registration

```python
# backend/src/app/ai/plugins_func/__init__.py

from .functions.intercom import intercom_send

# Add to tool registry
TOOLS = [
    # ... existing tools
    intercom_send,
]
```

### 1.3 LLM Prompt Hint

Add to system prompt để LLM nhận diện intent:

```
## Intercom (Bộ đàm)
Khi user muốn gửi tin nhắn đến thiết bị khác, sử dụng tool intercom.send.
Patterns:
- "Gọi [device] - [message]" → intercom.send(target_device="[device]", message="[message]")
- "Nhắn [device] - [message]" → intercom.send(...)
- "Nói với [device] - [message]" → intercom.send(...)

Ví dụ:
- "Gọi phòng ngủ - con ơi ăn cơm đi" → intercom.send("phòng ngủ", "con ơi ăn cơm đi")
```

---

## 2. IntercomService

### 2.1 Service Implementation

```python
# backend/src/app/services/intercom_service.py

import uuid
from datetime import datetime
from typing import Dict, Optional
from dataclasses import dataclass

from app.services.mqtt_service import MqttService
from app.services.device_service import DeviceService

@dataclass
class IntercomResult:
    success: bool
    error: str = ""
    conversation_id: str = ""

@dataclass
class IntercomConversation:
    conversation_id: str
    initiator_device_id: str
    initiator_mac: str
    initiator_name: str
    target_device_id: str
    target_mac: str
    target_name: str
    user_id: str
    state: str  # "active", "waiting_reply", "ended"
    started_at: datetime
    last_activity: datetime

class IntercomService:
    """Manages voice relay between devices."""
    
    # In-memory storage (use Redis for production)
    _conversations: Dict[str, IntercomConversation] = {}
    _device_to_conversation: Dict[str, str] = {}  # device_id -> conv_id
    
    def __init__(self):
        self.mqtt = MqttService()
        self.device_service = DeviceService()
    
    async def send_intercom(
        self,
        from_device_id: str,
        from_device_name: str,
        from_mac: str,
        target_device_name: str,
        message: str,
        user_id: str,
    ) -> IntercomResult:
        """
        Send intercom message to target device.
        
        Steps:
        1. Find target device by name in user's devices
        2. Create conversation
        3. Send MQTT message to target device
        4. Return result
        """
        
        # 1. Find target device
        target_device = await self._resolve_device_by_name(
            user_id=user_id,
            device_name=target_device_name,
            exclude_device_id=from_device_id
        )
        
        if not target_device:
            return IntercomResult(
                success=False,
                error=f"Không tìm thấy thiết bị '{target_device_name}'"
            )
        
        if not target_device.is_online:
            return IntercomResult(
                success=False,
                error=f"Thiết bị '{target_device_name}' không trực tuyến"
            )
        
        # 2. Create conversation
        conversation_id = str(uuid.uuid4())[:16]
        conversation = IntercomConversation(
            conversation_id=conversation_id,
            initiator_device_id=from_device_id,
            initiator_mac=from_mac,
            initiator_name=from_device_name,
            target_device_id=target_device.id,
            target_mac=target_device.mac_address,
            target_name=target_device.display_name,
            user_id=user_id,
            state="active",
            started_at=datetime.utcnow(),
            last_activity=datetime.utcnow(),
        )
        
        self._conversations[conversation_id] = conversation
        self._device_to_conversation[from_device_id] = conversation_id
        self._device_to_conversation[target_device.id] = conversation_id
        
        # 3. Send MQTT message to target device
        mqtt_payload = {
            "type": "intercom",
            "from_device_name": from_device_name,
            "from_device_id": from_device_id,
            "message": message,
            "conversation_id": conversation_id,
            "reply_to_mac": from_mac,
            "timestamp": datetime.utcnow().isoformat() + "Z"
        }
        
        topic = f"device/{target_device.mac_address}/server"
        await self.mqtt.publish(topic, mqtt_payload)
        
        return IntercomResult(
            success=True,
            conversation_id=conversation_id
        )
    
    async def handle_reply(
        self,
        from_device_id: str,
        from_device_name: str,
        conversation_id: str,
        message: str,
    ) -> IntercomResult:
        """
        Handle reply from device and relay to initiator.
        """
        
        # Get conversation
        conversation = self._conversations.get(conversation_id)
        if not conversation:
            return IntercomResult(
                success=False,
                error="Cuộc hội thoại không tồn tại"
            )
        
        # Determine recipient (the other party)
        if from_device_id == conversation.initiator_device_id:
            recipient_mac = conversation.target_mac
            recipient_name = conversation.target_name
        else:
            recipient_mac = conversation.initiator_mac
            recipient_name = conversation.initiator_name
        
        # Update activity
        conversation.last_activity = datetime.utcnow()
        
        # Send reply to recipient
        mqtt_payload = {
            "type": "intercom_reply",
            "from_device_name": from_device_name,
            "from_device_id": from_device_id,
            "message": message,
            "conversation_id": conversation_id,
            "timestamp": datetime.utcnow().isoformat() + "Z"
        }
        
        topic = f"device/{recipient_mac}/server"
        await self.mqtt.publish(topic, mqtt_payload)
        
        return IntercomResult(success=True, conversation_id=conversation_id)
    
    async def end_conversation(
        self,
        conversation_id: str,
        reason: str = "user_ended"
    ) -> None:
        """End conversation and cleanup."""
        conversation = self._conversations.get(conversation_id)
        if conversation:
            conversation.state = "ended"
            # Cleanup mappings
            self._device_to_conversation.pop(conversation.initiator_device_id, None)
            self._device_to_conversation.pop(conversation.target_device_id, None)
            self._conversations.pop(conversation_id, None)
    
    async def _resolve_device_by_name(
        self,
        user_id: str,
        device_name: str,
        exclude_device_id: str = None
    ) -> Optional[any]:
        """
        Find device by name within user's account.
        
        Matching priority:
        1. Exact match on display_name
        2. Partial match (contains)
        3. Fuzzy match (similarity > 0.7)
        """
        devices = await self.device_service.get_user_devices(user_id)
        
        # Filter out sender
        if exclude_device_id:
            devices = [d for d in devices if d.id != exclude_device_id]
        
        # 1. Exact match
        for device in devices:
            if device.display_name.lower() == device_name.lower():
                return device
        
        # 2. Partial match
        for device in devices:
            if device_name.lower() in device.display_name.lower():
                return device
        
        # 3. Fuzzy match (optional - use difflib or similar)
        # from difflib import SequenceMatcher
        # for device in devices:
        #     ratio = SequenceMatcher(None, device_name.lower(), device.display_name.lower()).ratio()
        #     if ratio > 0.7:
        #         return device
        
        return None
```

---

## 3. MQTT Message Handler

### 3.1 Handle Reply from Device

Khi firmware gửi reply thông qua `notification_speak` với `intercom: true`:

```python
# backend/src/app/services/mqtt_device_handler.py

async def handle_device_message(self, mac: str, payload: dict):
    msg_type = payload.get("type")
    
    if msg_type == "notification_speak":
        # Check if this is an intercom reply
        if payload.get("intercom"):
            await self._handle_intercom_speak(mac, payload)
        else:
            await self._handle_normal_speak(mac, payload)

async def _handle_intercom_speak(self, mac: str, payload: dict):
    """Handle intercom reply from device."""
    conversation_id = payload.get("conversation_id", "")
    content = payload.get("content", "")
    
    # Get device info
    device = await self.device_service.get_by_mac(mac)
    if not device:
        return
    
    # Relay reply
    service = IntercomService()
    await service.handle_reply(
        from_device_id=device.id,
        from_device_name=device.display_name,
        conversation_id=conversation_id,
        message=content,
    )
```

---

## 4. MQTT Topics

### 4.1 Server → Device (Push)

**Topic:** `device/{mac_address}/server`

```json
// Intercom message
{
    "type": "intercom",
    "from_device_name": "phòng khách",
    "from_device_id": "uuid",
    "message": "con ơi ăn cơm đi",
    "conversation_id": "abc123",
    "reply_to_mac": "aa:bb:cc:dd:ee:ff",
    "timestamp": "2026-02-03T21:00:00Z"
}

// Intercom reply
{
    "type": "intercom_reply",
    "from_device_name": "phòng ngủ",
    "from_device_id": "uuid",
    "message": "dạ con biết rồi",
    "conversation_id": "abc123",
    "timestamp": "2026-02-03T21:00:05Z"
}
```

### 4.2 Device → Server (Reply via existing channel)

Device gửi thông qua audio channel đang mở:

```json
{
    "type": "notification_speak",
    "content": "phản hồi của user",
    "intercom": true,
    "conversation_id": "abc123"
}
```

---

## 5. Database Schema (Optional)

```sql
-- Bảng lưu lịch sử intercom (optional)
CREATE TABLE intercom_messages (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    conversation_id VARCHAR(32) NOT NULL,
    sender_device_id UUID REFERENCES devices(id),
    sender_name VARCHAR(255),
    recipient_device_id UUID REFERENCES devices(id),
    recipient_name VARCHAR(255),
    message TEXT,
    direction VARCHAR(10), -- 'outgoing' or 'incoming'
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Index for quick lookup
CREATE INDEX idx_intercom_conversation ON intercom_messages(conversation_id);
CREATE INDEX idx_intercom_sender ON intercom_messages(sender_device_id);
```

---

## 6. API Endpoints (Optional)

```python
# backend/src/app/api/routes/intercom.py

from fastapi import APIRouter, HTTPException
from app.services.intercom_service import IntercomService

router = APIRouter(prefix="/api/intercom", tags=["intercom"])

@router.post("/send")
async def send_intercom(
    target_device: str,
    message: str,
    current_user = Depends(get_current_user),
    current_device = Depends(get_current_device),
):
    """Send intercom message via REST API (optional)."""
    service = IntercomService()
    result = await service.send_intercom(
        from_device_id=current_device.id,
        from_device_name=current_device.display_name,
        from_mac=current_device.mac_address,
        target_device_name=target_device,
        message=message,
        user_id=current_user.id,
    )
    
    if not result.success:
        raise HTTPException(status_code=400, detail=result.error)
    
    return {"success": True, "conversation_id": result.conversation_id}

@router.get("/conversations")
async def list_conversations(
    current_user = Depends(get_current_user),
):
    """List active conversations (optional)."""
    service = IntercomService()
    return service.get_user_conversations(current_user.id)
```

---

## 7. Implementation Steps

### Phase 1: Core (Day 1-2)
1. [ ] Create `intercom_service.py`
2. [ ] Create `intercom.py` MCP tool
3. [ ] Register tool in function registry
4. [ ] Add LLM prompt hints

### Phase 2: MQTT Handler (Day 2-3)
5. [ ] Update MQTT handler for `notification_speak` with `intercom: true`
6. [ ] Implement reply relay logic

### Phase 3: Testing (Day 3-4)
7. [ ] Test send intercom flow
8. [ ] Test reply flow
9. [ ] Test timeout handling
10. [ ] Test device name resolution

### Phase 4: Polish (Day 4-5)
11. [ ] Add logging
12. [ ] Add error handling
13. [ ] Add conversation cleanup (timeout)
14. [ ] Optional: Add database persistence

---

## 8. Testing Commands

### 8.1 Test via MQTT (Manual)

```bash
# Send intercom to device
mosquitto_pub -h mqtt.example.com \
  -t "device/aa:bb:cc:dd:ee:ff/server" \
  -m '{"type":"intercom","from_device_name":"phòng khách","message":"test message","conversation_id":"test123","reply_to_mac":"11:22:33:44:55:66"}'
```

### 8.2 Test via Voice

1. Wake device A: "OK Xiaozhi"
2. Say: "Gọi phòng ngủ - con ơi ăn cơm đi"
3. Check device B receives message
4. Device B auto-listens, user replies
5. Check device A receives reply

---

## 9. Security Considerations

1. **Account Isolation** - Only devices within same user account can intercom
2. **Device Validation** - Verify device ownership before sending
3. **Rate Limiting** - Max 10 intercom messages per minute per device
4. **Message Size** - Max 500 characters per message
5. **No Logging of Content** - Message content not logged (privacy)

---

## 10. Error Handling

| Error | Response |
|-------|----------|
| Device not found | "Không tìm thấy thiết bị '[name]'" |
| Device offline | "Thiết bị '[name]' không trực tuyến" |
| Same device | "Không thể gọi chính mình" |
| Rate limited | "Vui lòng chờ ít giây rồi thử lại" |
| Conversation expired | "Cuộc hội thoại đã kết thúc" |

---

**Document Version:** 1.0  
**Created:** 2026-02-03  
**Status:** 📋 Ready for Backend Implementation
