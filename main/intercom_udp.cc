#include "intercom_udp.h"

#include <esp_log.h>
#include <lwip/sockets.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>

#define TAG "IntercomUDP"

// Audio parameters
#define OPUS_FRAME_DURATION_MS 60
#define OPUS_SAMPLE_RATE 16000
#define OPUS_CHANNELS 1

// Packet structure: [4 bytes seq][encrypted opus data]
#define PACKET_SEQ_SIZE 4
#define MAX_PACKET_SIZE 512

IntercomUdp::IntercomUdp() {
    mbedtls_aes_init(&aes_ctx_);
    memset(aes_key_, 0, sizeof(aes_key_));
    memset(nonce_counter_, 0, sizeof(nonce_counter_));
    memset(stream_block_, 0, sizeof(stream_block_));
}

IntercomUdp::~IntercomUdp() {
    Disconnect();
    mbedtls_aes_free(&aes_ctx_);
}

bool IntercomUdp::Initialize(const std::string& session_id,
                             const std::string& udp_server,
                             int udp_port,
                             const std::string& aes_key_hex,
                             const std::string& nonce_hex,
                             const std::string& device_mac) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    session_id_ = session_id;
    udp_server_ = udp_server;
    udp_port_ = udp_port;
    device_mac_ = device_mac;
    encryption_enabled_ = false;
    
    ESP_LOGI(TAG, "Initializing: session=%s, server=%s:%d",
             session_id_.c_str(), udp_server_.c_str(), udp_port_);
    
    // Parse AES key and nonce
    if (!aes_key_hex.empty() && !nonce_hex.empty()) {
        if (ParseHexKey(aes_key_hex) && ParseHexNonce(nonce_hex)) {
            // Setup AES context
            int ret = mbedtls_aes_setkey_enc(&aes_ctx_, aes_key_, 128);
            if (ret == 0) {
                encryption_enabled_ = true;
                ESP_LOGI(TAG, "AES encryption enabled");
            } else {
                ESP_LOGW(TAG, "Failed to set AES key: %d", ret);
            }
        } else {
            ESP_LOGW(TAG, "Failed to parse AES key/nonce, encryption disabled");
        }
    } else {
        ESP_LOGW(TAG, "No AES key/nonce provided, encryption disabled");
    }
    
    // Create Opus encoder/decoder
    opus_encoder_ = std::make_unique<OpusEncoderWrapper>(
        OPUS_SAMPLE_RATE, OPUS_CHANNELS, OPUS_FRAME_DURATION_MS);
    opus_encoder_->SetComplexity(0);  // Fastest for low latency
    
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(
        OPUS_SAMPLE_RATE, OPUS_CHANNELS, OPUS_FRAME_DURATION_MS);
    
    return true;
}

bool IntercomUdp::Connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (socket_fd_ >= 0) {
        ESP_LOGW(TAG, "Already connected");
        return true;
    }
    
    // Resolve server address
    struct hostent* server = gethostbyname(udp_server_.c_str());
    if (!server) {
        ESP_LOGE(TAG, "DNS lookup failed for %s", udp_server_.c_str());
        return false;
    }
    
    // Create UDP socket
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd_ < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %s", strerror(errno));
        return false;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Set receive timeout
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 100ms
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    // Setup server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(udp_port_);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    
    // Connect socket (for UDP this just sets the default destination)
    if (connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to connect: %s", strerror(errno));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    // Send HELLO binding message
    std::string hello = "HELLO:" + session_id_ + ":" + device_mac_;
    if (send(socket_fd_, hello.c_str(), hello.length(), 0) < 0) {
        ESP_LOGE(TAG, "Failed to send HELLO: %s", strerror(errno));
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    ESP_LOGI(TAG, "Sent HELLO: %s", hello.c_str());
    
    // Wait for ACK (with timeout)
    char recv_buf[64];
    int recv_len = recv(socket_fd_, recv_buf, sizeof(recv_buf) - 1, 0);
    if (recv_len > 0) {
        recv_buf[recv_len] = '\0';
        if (strncmp(recv_buf, "ACK", 3) == 0) {
            bound_ = true;
            ESP_LOGI(TAG, "Received ACK, bound successfully");
        } else {
            ESP_LOGW(TAG, "Unexpected response: %s", recv_buf);
        }
    } else {
        // Timeout or error - assume binding worked (server might not send ACK)
        bound_ = true;
        ESP_LOGW(TAG, "No ACK received, assuming bound");
    }
    
    send_seq_ = 0;
    recv_seq_ = 0;
    nc_off_ = 0;
    
    return bound_;
}

void IntercomUdp::Disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    
    bound_ = false;
    encryption_enabled_ = false;
    opus_encoder_.reset();
    opus_decoder_.reset();
    
    ESP_LOGI(TAG, "Disconnected");
}

bool IntercomUdp::SendAudio(std::vector<int16_t>&& pcm) {
    if (!IsConnected() || !opus_encoder_) {
        return false;
    }
    
    // Encode PCM to Opus
    std::vector<uint8_t> opus_data;
    if (!opus_encoder_->Encode(std::move(pcm), opus_data)) {
        return false;
    }
    
    if (opus_data.empty()) {
        // Encoder is buffering
        return true;
    }
    
    // Build packet: [4 bytes seq][encrypted/plain data]
    std::vector<uint8_t> payload;
    
    if (encryption_enabled_) {
        payload = EncryptPacket(opus_data);
    } else {
        payload = std::move(opus_data);
    }
    
    std::vector<uint8_t> packet(PACKET_SEQ_SIZE + payload.size());
    
    // Add sequence number (big-endian)
    uint32_t seq = send_seq_++;
    packet[0] = (seq >> 24) & 0xFF;
    packet[1] = (seq >> 16) & 0xFF;
    packet[2] = (seq >> 8) & 0xFF;
    packet[3] = seq & 0xFF;
    
    // Copy payload
    memcpy(packet.data() + PACKET_SEQ_SIZE, payload.data(), payload.size());
    
    // Send
    std::lock_guard<std::mutex> lock(mutex_);
    ssize_t sent = send(socket_fd_, packet.data(), packet.size(), 0);
    
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send audio: %s", strerror(errno));
        return false;
    }
    
    return true;
}

bool IntercomUdp::ReceiveAudio(std::vector<int16_t>& pcm, int timeout_ms) {
    if (!IsConnected() || !opus_decoder_) {
        return false;
    }
    
    uint8_t recv_buf[MAX_PACKET_SIZE];
    
    // Use select for timeout
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(socket_fd_, &read_fds);
    
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    int result = select(socket_fd_ + 1, &read_fds, nullptr, nullptr, &tv);
    if (result <= 0) {
        return false;  // Timeout or error
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    ssize_t recv_len = recv(socket_fd_, recv_buf, sizeof(recv_buf), 0);
    
    if (recv_len < PACKET_SEQ_SIZE + 1) {
        return false;  // Too short or error
    }
    
    // Extract sequence number
    uint32_t seq = (recv_buf[0] << 24) | (recv_buf[1] << 16) | 
                   (recv_buf[2] << 8) | recv_buf[3];
    
    // Check for out-of-order packets (simple check)
    if (seq < recv_seq_ && recv_seq_ - seq < 100) {
        ESP_LOGW(TAG, "Out-of-order packet: got %lu, expected >= %lu", seq, recv_seq_);
        // Still process it for now
    }
    recv_seq_ = seq + 1;
    
    // Extract encrypted/plain data
    std::vector<uint8_t> payload(recv_buf + PACKET_SEQ_SIZE, 
                                  recv_buf + recv_len);
    
    // Decrypt if encryption is enabled
    std::vector<uint8_t> opus_data;
    if (encryption_enabled_) {
        opus_data = DecryptPacket(payload);
    } else {
        opus_data = std::move(payload);
    }
    
    // Decode Opus to PCM
    if (!opus_decoder_->Decode(std::move(opus_data), pcm)) {
        ESP_LOGE(TAG, "Failed to decode Opus");
        return false;
    }
    
    return true;
}

bool IntercomUdp::ParseHexKey(const std::string& hex_key) {
    if (hex_key.length() != 32) {
        ESP_LOGE(TAG, "Invalid AES key length: %d (expected 32)", (int)hex_key.length());
        return false;
    }
    
    for (size_t i = 0; i < 16; i++) {
        unsigned int byte;
        if (sscanf(hex_key.c_str() + i * 2, "%02x", &byte) != 1) {
            ESP_LOGE(TAG, "Failed to parse hex key at position %d", (int)i);
            return false;
        }
        aes_key_[i] = static_cast<uint8_t>(byte);
    }
    
    ESP_LOGI(TAG, "AES key parsed successfully");
    return true;
}

bool IntercomUdp::ParseHexNonce(const std::string& hex_nonce) {
    if (hex_nonce.length() != 32) {
        ESP_LOGE(TAG, "Invalid nonce length: %d (expected 32)", (int)hex_nonce.length());
        return false;
    }
    
    for (size_t i = 0; i < 16; i++) {
        unsigned int byte;
        if (sscanf(hex_nonce.c_str() + i * 2, "%02x", &byte) != 1) {
            ESP_LOGE(TAG, "Failed to parse hex nonce at position %d", (int)i);
            return false;
        }
        nonce_counter_[i] = static_cast<uint8_t>(byte);
    }
    
    ESP_LOGI(TAG, "AES nonce parsed successfully");
    return true;
}

std::vector<uint8_t> IntercomUdp::EncryptPacket(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> encrypted(data.size());
    
    // Create a copy of nonce for this encryption operation
    uint8_t nonce[16];
    memcpy(nonce, nonce_counter_, 16);
    
    // Add sequence number to nonce to make it unique per packet
    // Use last 4 bytes of nonce for counter
    uint32_t counter = send_seq_;
    nonce[12] = (counter >> 24) & 0xFF;
    nonce[13] = (counter >> 16) & 0xFF;
    nonce[14] = (counter >> 8) & 0xFF;
    nonce[15] = counter & 0xFF;
    
    size_t nc_off = 0;
    uint8_t stream_block[16];
    memset(stream_block, 0, 16);
    
    mbedtls_aes_crypt_ctr(&aes_ctx_, data.size(), &nc_off, nonce,
                          stream_block, data.data(), encrypted.data());
    
    return encrypted;
}

std::vector<uint8_t> IntercomUdp::DecryptPacket(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> decrypted(data.size());
    
    // Create a copy of nonce for this decryption operation
    uint8_t nonce[16];
    memcpy(nonce, nonce_counter_, 16);
    
    // Add sequence number to nonce to make it unique per packet
    uint32_t counter = recv_seq_ - 1;  // recv_seq_ was already incremented
    nonce[12] = (counter >> 24) & 0xFF;
    nonce[13] = (counter >> 16) & 0xFF;
    nonce[14] = (counter >> 8) & 0xFF;
    nonce[15] = counter & 0xFF;
    
    size_t nc_off = 0;
    uint8_t stream_block[16];
    memset(stream_block, 0, 16);
    
    mbedtls_aes_crypt_ctr(&aes_ctx_, data.size(), &nc_off, nonce,
                          stream_block, data.data(), decrypted.data());
    
    return decrypted;
}
