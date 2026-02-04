---
name: ESP32 Audio Development
description: Skill for audio processing on ESP32. Covers I2S, audio codecs (ES8311, ES8388), Opus encoding/decoding, VAD, wake word detection, and audio streaming. Use when working with microphone, speaker, or audio processing.
triggers:
  - audio
  - i2s
  - codec
  - opus
  - es8311
  - microphone
  - speaker
  - tts
  - stt
  - vad
  - wake word
---

# ESP32 Audio Development Skill

## Overview
Audio processing on ESP32 including I2S communication, audio codecs, and speech processing.

## I2S Configuration

### Basic I2S Setup
```cpp
#include "driver/i2s_std.h"

i2s_chan_handle_t tx_handle;
i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
i2s_new_channel(&chan_cfg, &tx_handle, NULL);

i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
        .mclk = GPIO_NUM_NC,
        .bclk = GPIO_BCLK,
        .ws = GPIO_WS,
        .dout = GPIO_DOUT,
        .din = GPIO_DIN,
    },
};
i2s_channel_init_std_mode(tx_handle, &std_cfg);
i2s_channel_enable(tx_handle);
```

### Duplex (Record + Playback)
```cpp
i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
// tx_handle for playback, rx_handle for recording
```

## Audio Codec (ES8311)

### Initialization
```cpp
#include "es8311.h"

es8311_codec_config_t config = {
    .i2c_port = I2C_NUM_0,
    .i2c_addr = ES8311_I2C_ADDR,
};
es8311_init(&config);
es8311_set_bits_per_sample(ES8311_BITS_16);
es8311_set_sample_rate(16000);
es8311_set_voice_volume(80);
```

## Opus Codec

### Encoding (PCM → Opus)
```cpp
#include "opus.h"

OpusEncoder *encoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &error);
opus_encoder_ctl(encoder, OPUS_SET_BITRATE(16000));

int16_t pcm_frame[160]; // 10ms at 16kHz
uint8_t opus_frame[256];
int opus_len = opus_encode(encoder, pcm_frame, 160, opus_frame, sizeof(opus_frame));
```

### Decoding (Opus → PCM)
```cpp
OpusDecoder *decoder = opus_decoder_create(16000, 1, &error);

int16_t pcm_out[1920]; // 60ms at 16kHz
int samples = opus_decode(decoder, opus_data, opus_len, pcm_out, 960, 0);
```

## Resampling
```cpp
#include "speex_resampler.h"

SpeexResamplerState *resampler = speex_resampler_init(
    1,      // channels
    24000,  // input rate
    16000,  // output rate
    5,      // quality (0-10)
    &error
);

speex_resampler_process_int(resampler, 0, in_data, &in_len, out_data, &out_len);
```

## Wake Word Detection

### ESP-SR (AFE)
```cpp
#include "esp_afe_sr_models.h"
#include "esp_afe_sr_iface.h"

esp_afe_sr_iface_t *afe_handle = &ESP_AFE_SR_HANDLE;
afe_config_t config = AFE_CONFIG_DEFAULT();
afe_handle->create_from_config(&config);

// Feed audio data
afe_handle->feed(audio_data, audio_len);

// Check for wake word
afe_fetch_result_t *res = afe_handle->fetch();
if (res && res->wakeup_state == WAKENET_DETECTED) {
    // Wake word detected!
}
```

## Xiaozhi Audio Service Pattern

### Audio Service Interface
```cpp
class AudioService {
public:
    void PlaySound(const std::string& sound_path);
    void EnableVoiceProcessing(bool enable);
    void EnableWakeWordDetection(bool enable);
    bool IsAudioProcessorRunning();
};

// Usage
audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
audio_service_.EnableVoiceProcessing(true);
```

### Audio Callbacks
```cpp
protocol_->SetOnIncomingAudioCallback([this](std::vector<uint8_t>&& audio_data) {
    // Decode Opus and play
    audio_service_.PlayOpusData(audio_data);
});

protocol_->SetOnAudioSendCallback([this]() {
    // Get PCM from microphone
    return audio_service_.GetEncodedAudioData();
});
```

## Common Audio Formats
| Format | Sample Rate | Bits | Channels | Use Case |
|--------|-------------|------|----------|----------|
| PCM    | 16kHz       | 16   | 1        | Wake word, STT |
| PCM    | 24kHz       | 16   | 2        | Playback |
| Opus   | 16kHz       | -    | 1        | Network streaming |

## Memory Tips
- Use PSRAM for audio buffers
- Process in small chunks (10-60ms)
- Double buffering for smooth playback

## References
- ESP-ADF: https://github.com/espressif/esp-adf
- ESP-SR: https://github.com/espressif/esp-sr
- Opus: https://opus-codec.org/
