#ifndef _INTERCOM_UDP_H_
#define _INTERCOM_UDP_H_

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <memory>

#include <opus_encoder.h>
#include <opus_decoder.h>
#include <mbedtls/aes.h>

#include "audio_codec.h"

/**
 * @brief Full Duplex Intercom UDP Handler
 * 
 * Manages UDP socket communication for real-time audio streaming
 * between two devices with Opus encoding and AES encryption.
 */
class IntercomUdp {
public:
    IntercomUdp();
    ~IntercomUdp();
    
    /**
     * @brief Initialize with session parameters
     * @param session_id Session ID from server
     * @param udp_server UDP relay server IP
     * @param udp_port UDP relay server port
     * @param aes_key_hex AES-128 key in hex format (32 chars)
     * @param nonce_hex AES nonce/IV in hex format (32 chars)
     * @param device_mac Current device MAC address
     * @return true if initialization successful
     */
    bool Initialize(const std::string& session_id,
                   const std::string& udp_server,
                   int udp_port,
                   const std::string& aes_key_hex,
                   const std::string& nonce_hex,
                   const std::string& device_mac);
    
    /**
     * @brief Connect to UDP server and send binding
     * @return true if connected and bound
     */
    bool Connect();
    
    /**
     * @brief Disconnect and cleanup
     */
    void Disconnect();
    
    /**
     * @brief Check if connected
     */
    bool IsConnected() const { return socket_fd_ >= 0 && bound_; }
    
    /**
     * @brief Encode PCM audio and send via UDP
     * @param pcm PCM audio samples (16-bit, 16kHz, mono)
     * @return true if sent successfully
     */
    bool SendAudio(std::vector<int16_t>&& pcm);
    
    /**
     * @brief Receive audio from UDP
     * @param pcm Output PCM audio samples
     * @param timeout_ms Timeout in milliseconds
     * @return true if received data
     */
    bool ReceiveAudio(std::vector<int16_t>& pcm, int timeout_ms = 100);
    
    /**
     * @brief Get the socket file descriptor (for select/poll)
     */
    int GetSocketFd() const { return socket_fd_; }
    
private:
    // Configuration
    std::string session_id_;
    std::string udp_server_;
    int udp_port_ = 0;
    std::string device_mac_;
    
    // Socket
    int socket_fd_ = -1;
    bool bound_ = false;
    
    // Opus codec
    std::unique_ptr<OpusEncoderWrapper> opus_encoder_;
    std::unique_ptr<OpusDecoderWrapper> opus_decoder_;
    
    // AES encryption (using CTR mode like MQTT protocol)
    mbedtls_aes_context aes_ctx_;
    uint8_t aes_key_[16];
    uint8_t nonce_counter_[16];
    uint8_t stream_block_[16];
    size_t nc_off_ = 0;
    
    // Sequence number for packets
    uint32_t send_seq_ = 0;
    uint32_t recv_seq_ = 0;
    
    // Thread safety
    mutable std::mutex mutex_;
    
    // Helper methods
    bool ParseHexKey(const std::string& hex_key);
    bool ParseHexNonce(const std::string& hex_nonce);
    std::vector<uint8_t> EncryptPacket(const std::vector<uint8_t>& data);
    std::vector<uint8_t> DecryptPacket(const std::vector<uint8_t>& data);
    
    // Encryption flag
    bool encryption_enabled_ = false;
};

#endif // _INTERCOM_UDP_H_
