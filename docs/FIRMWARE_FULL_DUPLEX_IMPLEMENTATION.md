# 🎙️ Firmware Full Duplex Intercom Implementation Guide

> **Version**: 1.0.0  
> **Date**: 2026-02-05  
> **Status**: Production Ready (Backend) - Implementation Required (Firmware)

---

## 📋 Tổng Quan

Document này hướng dẫn implement **Full Duplex Intercom** cho ESP32 firmware, cho phép 2 thiết bị giao tiếp voice realtime qua UDP relay.

### Kiến Trúc

```
┌─────────────┐         ┌─────────────┐         ┌─────────────┐
│  Device A   │◄───────►│   Server    │◄───────►│  Device B   │
│  (Caller)   │  MQTT   │  (Relay)    │  MQTT   │  (Callee)   │
└──────┬──────┘         └──────┬──────┘         └──────┬──────┘
       │                       │                       │
       │      UDP Audio        │      UDP Audio        │
       └───────────────────────┴───────────────────────┘
                    Full Duplex Relay
```

---

## 🔧 1. Device States

Thêm các state mới vào `DeviceState` enum:

```cpp
// application.h hoặc device_state.h
enum DeviceState {
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    
    // ============ INTERCOM STATES ============
    kDeviceStateIntercomCalling,    // Đang gọi, chờ callee accept
    kDeviceStateIntercomIncoming,   // Có cuộc gọi đến, chờ user accept
    kDeviceStateIntercomActive,     // Full duplex đang hoạt động
};
```

---

## 🔧 2. Intercom Data Structures

```cpp
// intercom_types.h

#include <string>
#include <cstdint>

struct IntercomSession {
    std::string session_id;
    std::string target_mac;
    std::string target_name;
    
    // UDP Config
    std::string udp_server;
    uint16_t udp_port;
    
    // AES Encryption (32 bytes key, 16 bytes nonce)
    uint8_t aes_key[32];
    uint8_t aes_nonce[16];
    
    // State
    bool is_caller;         // true = caller, false = callee
    bool udp_bound;         // UDP handshake completed
    
    // Timing
    uint32_t start_time;
    uint32_t last_activity;
};

// Global session
extern IntercomSession g_intercom_session;
extern bool g_intercom_active;
```

---

## 🔧 3. MQTT Message Handlers

### 3.1 Parse Incoming Messages

```cpp
// mqtt_handler.cpp

#include <ArduinoJson.h>

void handleMqttMessage(const char* topic, const char* payload) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        ESP_LOGE(TAG, "JSON parse error: %s", error.c_str());
        return;
    }
    
    const char* type = doc["type"] | "";
    
    // ============ INTERCOM MESSAGE HANDLERS ============
    
    if (strcmp(type, "intercom_ready") == 0) {
        handleIntercomReady(doc);
    }
    else if (strcmp(type, "intercom_incoming") == 0) {
        handleIntercomIncoming(doc);
    }
    else if (strcmp(type, "intercom_end") == 0) {
        handleIntercomEnd(doc);
    }
    else if (strcmp(type, "intercom_error") == 0) {
        handleIntercomError(doc);
    }
    
    // ... existing handlers ...
}
```

### 3.2 Handle `intercom_ready` (Caller nhận)

```cpp
/**
 * Server gửi khi accept cuộc gọi.
 * Caller nhận và bắt đầu UDP binding.
 * 
 * Payload:
 * {
 *   "type": "intercom_ready",
 *   "session_id": "abc123...",
 *   "target_device": "Phòng Ngủ",
 *   "target_status": "online",
 *   "udp": {
 *     "server": "103.78.3.29",
 *     "port": 8765,
 *     "key": "hex_aes_key_64_chars",
 *     "nonce": "hex_nonce_32_chars"
 *   }
 * }
 */
void handleIntercomReady(JsonDocument& doc) {
    ESP_LOGI(TAG, "[Intercom] Received intercom_ready");
    
    // ⚠️ QUAN TRỌNG: KHÔNG RESTART DEVICE!
    
    // Parse session info
    g_intercom_session.session_id = doc["session_id"].as<std::string>();
    g_intercom_session.target_name = doc["target_device"].as<std::string>();
    g_intercom_session.is_caller = true;
    
    // Parse UDP config
    JsonObject udp = doc["udp"];
    g_intercom_session.udp_server = udp["server"].as<std::string>();
    g_intercom_session.udp_port = udp["port"] | 8765;
    
    // Decode AES key from hex
    const char* key_hex = udp["key"] | "";
    const char* nonce_hex = udp["nonce"] | "";
    hexToBytes(key_hex, g_intercom_session.aes_key, 32);
    hexToBytes(nonce_hex, g_intercom_session.aes_nonce, 16);
    
    // Check target status
    const char* status = doc["target_status"] | "offline";
    if (strcmp(status, "offline") == 0) {
        ESP_LOGW(TAG, "[Intercom] Target device is offline!");
        // Có thể hiển thị thông báo trên UI
    }
    
    // Bắt đầu intercom
    startIntercomSession();
}
```

### 3.3 Handle `intercom_incoming` (Callee nhận)

```cpp
/**
 * Có cuộc gọi đến.
 * Callee nhận và hiển thị ringing/auto-answer.
 * 
 * Payload:
 * {
 *   "type": "intercom_incoming",
 *   "from_device": "ac:a7:04:f3:68:14",
 *   "from_name": "Phòng Khách",
 *   "session_id": "abc123...",
 *   "udp": {...}
 * }
 */
void handleIntercomIncoming(JsonDocument& doc) {
    ESP_LOGI(TAG, "[Intercom] Incoming call from %s", 
             doc["from_name"].as<const char*>());
    
    // Parse session info
    g_intercom_session.session_id = doc["session_id"].as<std::string>();
    g_intercom_session.target_mac = doc["from_device"].as<std::string>();
    g_intercom_session.target_name = doc["from_name"].as<std::string>();
    g_intercom_session.is_caller = false;
    
    // Parse UDP config
    JsonObject udp = doc["udp"];
    g_intercom_session.udp_server = udp["server"].as<std::string>();
    g_intercom_session.udp_port = udp["port"] | 8765;
    hexToBytes(udp["key"] | "", g_intercom_session.aes_key, 32);
    hexToBytes(udp["nonce"] | "", g_intercom_session.aes_nonce, 16);
    
    // Auto-answer hoặc hiển thị UI
    #ifdef INTERCOM_AUTO_ANSWER
        // Auto-answer (intercom mode)
        startIntercomSession();
    #else
        // Manual answer - hiển thị UI
        Application::GetInstance().SetDeviceState(kDeviceStateIntercomIncoming);
        displayIncomingCall(g_intercom_session.target_name.c_str());
    #endif
}
```

### 3.4 Handle `intercom_end`

```cpp
/**
 * Bên kia ngắt cuộc gọi hoặc server timeout.
 */
void handleIntercomEnd(JsonDocument& doc) {
    const char* reason = doc["reason"] | "ended";
    ESP_LOGI(TAG, "[Intercom] Call ended: %s", reason);
    
    stopIntercomSession();
    
    // Hiển thị thông báo
    if (strcmp(reason, "timeout") == 0) {
        displayMessage("Cuộc gọi hết thời gian");
    } else if (strcmp(reason, "rejected") == 0) {
        displayMessage("Cuộc gọi bị từ chối");
    } else {
        displayMessage("Cuộc gọi kết thúc");
    }
}
```

### 3.5 Handle `intercom_error`

```cpp
void handleIntercomError(JsonDocument& doc) {
    const char* error = doc["error"] | "unknown";
    const char* message = doc["message"] | "";
    
    ESP_LOGE(TAG, "[Intercom] Error: %s - %s", error, message);
    
    stopIntercomSession();
    displayError(message);
}
```

---

## 🔧 4. Gửi Intercom Messages

### 4.1 Khởi tạo cuộc gọi (User chọn contact)

```cpp
/**
 * Gọi khi user chọn một contact từ danh sách.
 * Gửi intercom_hello đến server.
 */
void initiateIntercomCall(const char* targetMac) {
    ESP_LOGI(TAG, "[Intercom] Initiating call to %s", targetMac);
    
    // Set state
    Application::GetInstance().SetDeviceState(kDeviceStateIntercomCalling);
    g_intercom_session.target_mac = targetMac;
    g_intercom_session.start_time = millis();
    
    // Build MQTT message
    JsonDocument doc;
    doc["type"] = "mcp";  // ⚠️ Wrap trong MCP container
    doc["session_id"] = generateSessionId(); // UUID
    
    JsonObject payload = doc["payload"].to<JsonObject>();
    payload["type"] = "intercom_hello";
    payload["target_mac"] = targetMac;
    payload["mac_address"] = getDeviceMac();
    payload["version"] = 3;
    payload["transport"] = "udp";
    
    JsonObject audio = payload["audio_params"].to<JsonObject>();
    audio["format"] = "opus";
    audio["sample_rate"] = 16000;
    audio["channels"] = 1;
    audio["frame_duration"] = 60;
    
    // Serialize and publish
    std::string json;
    serializeJson(doc, json);
    
    mqtt_client->publish("device-server", json.c_str(), json.length(), 1);
    
    ESP_LOGI(TAG, "[Intercom] Sent intercom_hello");
    
    // UI: Hiển thị "Đang gọi..."
    displayMessage("Đang gọi...");
}
```

### 4.2 Kết thúc cuộc gọi

```cpp
/**
 * Gọi khi user nhấn nút ngắt hoặc timeout.
 */
void endIntercomCall(const char* reason = "user_ended") {
    if (!g_intercom_active && 
        Application::GetInstance().GetDeviceState() != kDeviceStateIntercomCalling) {
        return; // Không có cuộc gọi active
    }
    
    ESP_LOGI(TAG, "[Intercom] Ending call: %s", reason);
    
    // Gửi MQTT
    JsonDocument doc;
    doc["type"] = "intercom_end";
    doc["session_id"] = g_intercom_session.session_id;
    doc["mac_address"] = getDeviceMac();
    doc["reason"] = reason;
    
    std::string json;
    serializeJson(doc, json);
    mqtt_client->publish("device-server", json.c_str(), json.length(), 1);
    
    // Stop session
    stopIntercomSession();
}
```

---

## 🔧 5. UDP Session Management

### 5.1 Bắt đầu Session

```cpp
#include <WiFiUdp.h>
#include <mbedtls/aes.h>
#include <opus.h>

WiFiUDP intercom_udp;
TaskHandle_t intercom_send_task = nullptr;
TaskHandle_t intercom_recv_task = nullptr;

void startIntercomSession() {
    ESP_LOGI(TAG, "[Intercom] Starting session %s", 
             g_intercom_session.session_id.c_str());
    
    g_intercom_active = true;
    g_intercom_session.udp_bound = false;
    
    // Kết nối UDP
    intercom_udp.begin(0); // Random local port
    
    // Gửi HELLO bind packet
    sendUdpBind();
    
    // Chờ ACK (với timeout)
    if (!waitForUdpAck(3000)) {
        ESP_LOGE(TAG, "[Intercom] UDP bind timeout!");
        stopIntercomSession();
        return;
    }
    
    g_intercom_session.udp_bound = true;
    ESP_LOGI(TAG, "[Intercom] UDP bound successfully");
    
    // Set state
    Application::GetInstance().SetDeviceState(kDeviceStateIntercomActive);
    
    // Start audio tasks
    xTaskCreatePinnedToCore(intercomSendTask, "intercom_tx", 8192, 
                            nullptr, 5, &intercom_send_task, 1);
    xTaskCreatePinnedToCore(intercomReceiveTask, "intercom_rx", 8192, 
                            nullptr, 5, &intercom_recv_task, 0);
    
    ESP_LOGI(TAG, "[Intercom] Full duplex active!");
}
```

### 5.2 UDP Binding

```cpp
/**
 * Gửi HELLO packet để bind UDP với server.
 * Format: HELLO:session_id:mac_address
 */
void sendUdpBind() {
    std::string hello = "HELLO:" + g_intercom_session.session_id + ":" + getDeviceMac();
    
    intercom_udp.beginPacket(
        g_intercom_session.udp_server.c_str(), 
        g_intercom_session.udp_port
    );
    intercom_udp.write((const uint8_t*)hello.c_str(), hello.length());
    intercom_udp.endPacket();
    
    ESP_LOGI(TAG, "[Intercom] Sent UDP bind: %s", hello.c_str());
}

/**
 * Chờ ACK từ server.
 */
bool waitForUdpAck(uint32_t timeout_ms) {
    uint32_t start = millis();
    uint8_t buffer[64];
    
    while (millis() - start < timeout_ms) {
        int packetSize = intercom_udp.parsePacket();
        if (packetSize > 0) {
            int len = intercom_udp.read(buffer, sizeof(buffer) - 1);
            buffer[len] = 0;
            
            if (strcmp((char*)buffer, "ACK") == 0) {
                ESP_LOGI(TAG, "[Intercom] Received ACK");
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    return false;
}
```

### 5.3 Stop Session

```cpp
void stopIntercomSession() {
    ESP_LOGI(TAG, "[Intercom] Stopping session");
    
    g_intercom_active = false;
    
    // Stop tasks
    if (intercom_send_task) {
        vTaskDelete(intercom_send_task);
        intercom_send_task = nullptr;
    }
    if (intercom_recv_task) {
        vTaskDelete(intercom_recv_task);
        intercom_recv_task = nullptr;
    }
    
    // Close UDP
    intercom_udp.stop();
    
    // Reset state
    g_intercom_session = IntercomSession();
    Application::GetInstance().SetDeviceState(kDeviceStateIdle);
}
```

---

## 🔧 6. Audio Tasks (Full Duplex)

### 6.1 Mic → UDP (Send Task)

```cpp
/**
 * Task: Capture audio từ mic, encode, encrypt, gửi qua UDP.
 */
void intercomSendTask(void* param) {
    ESP_LOGI(TAG, "[Intercom TX] Task started");
    
    // Opus encoder
    int error;
    OpusEncoder* encoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &error);
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(5));
    
    // AES context
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, g_intercom_session.aes_key, 256);
    
    // Buffers
    int16_t pcm_buffer[960]; // 60ms @ 16kHz
    uint8_t opus_buffer[256];
    uint8_t encrypted[272]; // 16 nonce + 256 data
    
    while (g_intercom_active) {
        // 1. Capture audio từ I2S mic
        size_t bytes_read = 0;
        esp_err_t err = i2s_read(I2S_MIC_PORT, pcm_buffer, sizeof(pcm_buffer), 
                                  &bytes_read, pdMS_TO_TICKS(100));
        
        if (err != ESP_OK || bytes_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        // 2. Encode với Opus
        int opus_len = opus_encode(encoder, pcm_buffer, 960, opus_buffer, sizeof(opus_buffer));
        if (opus_len < 0) {
            ESP_LOGE(TAG, "[Intercom TX] Opus encode error: %d", opus_len);
            continue;
        }
        
        // 3. Encrypt với AES-CTR
        // Prepend nonce (first 16 bytes), then encrypted data
        memcpy(encrypted, g_intercom_session.aes_nonce, 16);
        
        // Increment nonce for next packet
        incrementNonce(g_intercom_session.aes_nonce);
        
        size_t nc_off = 0;
        uint8_t stream_block[16] = {0};
        mbedtls_aes_crypt_ctr(&aes, opus_len, &nc_off, encrypted, 
                              stream_block, opus_buffer, encrypted + 16);
        
        int total_len = 16 + opus_len;
        
        // 4. Send qua UDP
        intercom_udp.beginPacket(
            g_intercom_session.udp_server.c_str(), 
            g_intercom_session.udp_port
        );
        intercom_udp.write(encrypted, total_len);
        intercom_udp.endPacket();
        
        g_intercom_session.last_activity = millis();
    }
    
    // Cleanup
    opus_encoder_destroy(encoder);
    mbedtls_aes_free(&aes);
    
    ESP_LOGI(TAG, "[Intercom TX] Task ended");
    vTaskDelete(nullptr);
}
```

### 6.2 UDP → Speaker (Receive Task)

```cpp
/**
 * Task: Nhận UDP, decrypt, decode, play to speaker.
 */
void intercomReceiveTask(void* param) {
    ESP_LOGI(TAG, "[Intercom RX] Task started");
    
    // Opus decoder
    int error;
    OpusDecoder* decoder = opus_decoder_create(16000, 1, &error);
    
    // AES context
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, g_intercom_session.aes_key, 256);
    
    // Buffers
    uint8_t udp_buffer[512];
    uint8_t decrypted[256];
    int16_t pcm_buffer[960];
    
    while (g_intercom_active) {
        int packetSize = intercom_udp.parsePacket();
        if (packetSize <= 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        
        int len = intercom_udp.read(udp_buffer, sizeof(udp_buffer));
        if (len < 17) { // Minimum: 16 nonce + 1 data
            continue;
        }
        
        // 1. Extract nonce (first 16 bytes)
        uint8_t nonce[16];
        memcpy(nonce, udp_buffer, 16);
        
        int encrypted_len = len - 16;
        
        // 2. Decrypt với AES-CTR
        size_t nc_off = 0;
        uint8_t stream_block[16] = {0};
        mbedtls_aes_crypt_ctr(&aes, encrypted_len, &nc_off, nonce, 
                              stream_block, udp_buffer + 16, decrypted);
        
        // 3. Decode với Opus
        int samples = opus_decode(decoder, decrypted, encrypted_len, pcm_buffer, 960, 0);
        if (samples < 0) {
            ESP_LOGE(TAG, "[Intercom RX] Opus decode error: %d", samples);
            continue;
        }
        
        // 4. Play to speaker via I2S
        size_t bytes_written = 0;
        i2s_write(I2S_SPEAKER_PORT, pcm_buffer, samples * sizeof(int16_t), 
                  &bytes_written, pdMS_TO_TICKS(100));
        
        g_intercom_session.last_activity = millis();
    }
    
    // Cleanup
    opus_decoder_destroy(decoder);
    mbedtls_aes_free(&aes);
    
    ESP_LOGI(TAG, "[Intercom RX] Task ended");
    vTaskDelete(nullptr);
}
```

---

## 🔧 7. Helper Functions

```cpp
/**
 * Convert hex string to bytes.
 */
void hexToBytes(const char* hex, uint8_t* bytes, size_t len) {
    for (size_t i = 0; i < len && hex[i*2] && hex[i*2+1]; i++) {
        char byte_str[3] = {hex[i*2], hex[i*2+1], 0};
        bytes[i] = (uint8_t)strtol(byte_str, nullptr, 16);
    }
}

/**
 * Increment nonce (128-bit counter).
 */
void incrementNonce(uint8_t* nonce) {
    for (int i = 15; i >= 0; i--) {
        if (++nonce[i] != 0) break;
    }
}

/**
 * Generate random session ID.
 */
std::string generateSessionId() {
    uint8_t random_bytes[16];
    esp_fill_random(random_bytes, sizeof(random_bytes));
    
    char hex[33];
    for (int i = 0; i < 16; i++) {
        sprintf(hex + i*2, "%02x", random_bytes[i]);
    }
    return std::string(hex);
}

/**
 * Get device MAC address.
 */
const char* getDeviceMac() {
    static char mac[18];
    uint8_t baseMac[6];
    esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
    sprintf(mac, "%02x:%02x:%02x:%02x:%02x:%02x",
            baseMac[0], baseMac[1], baseMac[2],
            baseMac[3], baseMac[4], baseMac[5]);
    return mac;
}
```

---

## 🔧 8. UI Integration

### 8.1 Contact Selection (Double Press or Menu)

```cpp
// Trong button handler hoặc UI menu
void onContactSelected(const char* contactMac) {
    initiateIntercomCall(contactMac);
}

// Trong contact list view
void displayContactList() {
    // Fetch từ API đã có: GET /api/v1/device/intercom-contacts
    // Hiển thị danh sách contacts
    // Khi user chọn → onContactSelected(mac)
}
```

### 8.2 Incoming Call UI

```cpp
void displayIncomingCall(const char* callerName) {
    // Hiển thị UI ringing
    // Buttons: Accept / Reject
    
    // Auto-answer timer (nếu enable)
    #ifdef INTERCOM_AUTO_ANSWER_DELAY
    xTimerCreate("auto_answer", pdMS_TO_TICKS(INTERCOM_AUTO_ANSWER_DELAY),
                 pdFALSE, nullptr, [](TimerHandle_t) {
        if (Application::GetInstance().GetDeviceState() == kDeviceStateIntercomIncoming) {
            startIntercomSession();
        }
    });
    #endif
}

void onAcceptCall() {
    startIntercomSession();
}

void onRejectCall() {
    endIntercomCall("rejected");
}
```

### 8.3 Active Call UI

```cpp
void displayActiveCall() {
    // Hiển thị:
    // - Target name
    // - Call duration
    // - End call button
    
    // Volume indicator (optional)
}
```

---

## ⚠️ 9. Troubleshooting

### Device Restart khi nhận MQTT

**Nguyên nhân có thể:**
1. JSON parsing lỗi → Out of memory
2. Chưa handle message type → Fall vào default restart
3. Stack overflow trong handler

**Fix:**
```cpp
// Đảm bảo message handlers không restart device
void handleIntercomReady(JsonDocument& doc) {
    // ❌ KHÔNG GỌI: ESP.restart()
    // ❌ KHÔNG GỌI: rebootDevice()
    // ❌ KHÔNG THROW exception
    
    try {
        // Parse và xử lý
    } catch (...) {
        ESP_LOGE(TAG, "Error handling intercom_ready");
        // Log error, KHÔNG restart
    }
}
```

### UDP Bind Timeout

**Kiểm tra:**
1. Firewall cho phép UDP outbound
2. Server IP và port đúng
3. Session ID match

### Audio Quality Issues

**Tuning:**
```cpp
// Opus settings
opus_encoder_ctl(encoder, OPUS_SET_BITRATE(24000)); // 16k-32k
opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(5));   // 1-10
opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10)); // Expected loss

// Buffer sizes
#define OPUS_FRAME_SIZE 960  // 60ms @ 16kHz
#define AUDIO_BUFFER_MS 100  // Playback buffer
```

---

## 📋 10. Implementation Checklist

### Phase 1: Basic MQTT
- [ ] Add device states enum
- [ ] Handle `intercom_ready` - **NO RESTART**
- [ ] Handle `intercom_incoming` - **NO RESTART**
- [ ] Handle `intercom_end`
- [ ] Handle `intercom_error`
- [ ] Send `intercom_hello` khi chọn contact
- [ ] Send `intercom_end` khi ngắt cuộc gọi

### Phase 2: UDP Binding
- [ ] UDP socket setup
- [ ] Send `HELLO:session_id:mac`
- [ ] Wait for ACK
- [ ] Handle timeout

### Phase 3: Audio Pipeline
- [ ] Opus encoder setup
- [ ] Opus decoder setup
- [ ] AES encryption
- [ ] AES decryption
- [ ] Send task (Mic → UDP)
- [ ] Receive task (UDP → Speaker)

### Phase 4: UI/UX
- [ ] Contact list display
- [ ] Calling state UI
- [ ] Incoming call UI
- [ ] Active call UI
- [ ] End call button

---

## 📞 Server API Reference

| Endpoint | Method | Description |
|----------|--------|-------------|
| `GET /api/v1/device/intercom-contacts` | GET | Lấy danh sách contacts |
| MQTT `device-server` | PUB | Gửi `intercom_hello` |
| MQTT `device/{mac}/server` | SUB | Nhận `intercom_ready`, `intercom_incoming`, etc. |
| UDP `server:port` | BIND | `HELLO:session:mac` → `ACK` |
| UDP `server:port` | AUDIO | Encrypted Opus packets |

---

**Document Version**: 1.0.0  
**Last Updated**: 2026-02-05  
**Author**: Bizino AI DEV
