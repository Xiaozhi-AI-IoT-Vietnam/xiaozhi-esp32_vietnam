#include "application.h"
#include "assets.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "display.h"
#include "esp32_sd_music.h"
#include "features/weather/weather_ui.h"
#include "mcp_server.h"
#include "mqtt_protocol.h"
#include "ota_server.h"
#include "sd_card.h"
#include "settings.h"
#include "system_info.h"
#include "websocket_protocol.h"
#include "wifi_station.h"
#include <arpa/inet.h>
#include <cJSON.h>
#include <cmath>
#include <cstring>
#include <driver/gpio.h>
#include <esp_log.h>
#include <font_awesome.h>
#include <qrcode.h>
#include <model_path.h>
#define TAG "Application"

static const char *const STATE_STRINGS[] = {
    "unknown",    "starting",      "configuring", "idle",
    "connecting", "listening",     "speaking",    "upgrading",
    "activating", "audio_testing", "fatal_error", "invalid_state"};

Application::Application() {
  event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error                                                                         \
    "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
  aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
  aec_mode_ = kAecOnServerSide;
#else
  aec_mode_ = kAecOff;
#endif

  esp_timer_create_args_t clock_timer_args = {
      .callback =
          [](void *arg) {
            Application *app = (Application *)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
          },
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "clock_timer",
      .skip_unhandled_events = true};
  esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
  if (clock_timer_handle_ != nullptr) {
    esp_timer_stop(clock_timer_handle_);
    esp_timer_delete(clock_timer_handle_);
  }
  vEventGroupDelete(event_group_);
}

void Application::CheckAssetsVersion() {
  auto &board = Board::GetInstance();
  auto display = board.GetDisplay();
  auto &assets = Assets::GetInstance();

  if (!assets.partition_valid()) {
    ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
    return;
  }

  Settings settings("assets", true);
  // Check if there is a new assets need to be downloaded
  std::string download_url = settings.GetString("download_url");

  if (!download_url.empty()) {
    settings.EraseKey("download_url");

    char message[256];
    snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS,
             download_url.c_str());
    Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down",
          Lang::Sounds::OGG_UPGRADE);

    // Wait for the audio service to be idle for 3 seconds
    vTaskDelay(pdMS_TO_TICKS(3000));
    SetDeviceState(kDeviceStateUpgrading);
    board.SetPowerSaveMode(false);
    display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

    bool success = assets.Download(
        download_url, [display](int progress, size_t speed) -> void {
          std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress,
                     speed / 1024);
            display->SetChatMessage("system", buffer);
          }).detach();
        });

    board.SetPowerSaveMode(true);
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (!success) {
      Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED,
            "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
      vTaskDelay(pdMS_TO_TICKS(2000));
      return;
    }
  }

  // Apply assets
  assets.Apply();
  display->SetChatMessage("system", "");
  display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion(Ota &ota) {
  const int MAX_RETRY = 10;
  int retry_count = 0;
  int retry_delay = 10; // Initial retry delay is 10 seconds

  auto &board = Board::GetInstance();
  while (true) {
    SetDeviceState(kDeviceStateActivating);
    auto display = board.GetDisplay();
    display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

    std::string url = CONFIG_OTA_URL;
    if (!ota.CheckVersion(url)) {
      retry_count++;
      if (retry_count >= MAX_RETRY) {
        ESP_LOGE(TAG, "Too many retries, exit version check");
        return;
      }

      char buffer[256];
      snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED,
               retry_delay, ota.GetCheckVersionUrl().c_str());
      Alert(Lang::Strings::ERROR, buffer, "cloud_slash",
            Lang::Sounds::OGG_EXCLAMATION);

      ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)",
               retry_delay, retry_count, MAX_RETRY);
      for (int i = 0; i < retry_delay; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (device_state_ == kDeviceStateIdle) {
          break;
        }
      }
      retry_delay *= 2; // The delay time doubles after each retry.
      continue;
    }

    ota.CheckVersion(std::string() = "");

    retry_count = 0;
    retry_delay = 10; // Reset retry delay time

    if (ota.HasNewVersion()) {
      if (UpgradeFirmware(ota)) {
        return; // This line will never be reached after reboot
      }
      // If upgrade failed, continue to normal operation (don't break, just fall
      // through)
    }

    // No new version, mark the current version as valid
    ota.MarkCurrentVersionValid();
    if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
      xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
      // Exit the loop if done checking new version
      break;
    }

    display->SetStatus(Lang::Strings::ACTIVATION);
    // Activation code is shown to the user and waiting for the user to input
    if (ota.HasActivationCode()) {
      ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
    }

    // This will block the loop until the activation is done or timeout
    // Use timeout from server (default 30s), each iteration is 3 seconds
    int timeout_ms = ota.GetActivationTimeoutMs();
    int max_iterations = timeout_ms / 3000; // 3 seconds per iteration
    if (max_iterations < 1)
      max_iterations = 1;

    for (int i = 0; i < max_iterations; ++i) {
      ESP_LOGI(TAG, "Activating... %d/%d (timeout: %dms)", i + 1,
               max_iterations, timeout_ms);
      esp_err_t err = ota.Activate();
      if (err == ESP_OK) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
        break;
      } else if (err == ESP_ERR_TIMEOUT) {
        vTaskDelay(pdMS_TO_TICKS(3000));
      } else {
        vTaskDelay(pdMS_TO_TICKS(10000));
      }
      if (device_state_ == kDeviceStateIdle) {
        break;
      }
    }
  }
}

void Application::ShowActivationCode(const std::string &code,
                                     const std::string &message) {
  struct digit_sound {
    char digit;
    const std::string_view &sound;
  };
  static const std::array<digit_sound, 10> digit_sounds{
      {digit_sound{'0', Lang::Sounds::OGG_0},
       digit_sound{'1', Lang::Sounds::OGG_1},
       digit_sound{'2', Lang::Sounds::OGG_2},
       digit_sound{'3', Lang::Sounds::OGG_3},
       digit_sound{'4', Lang::Sounds::OGG_4},
       digit_sound{'5', Lang::Sounds::OGG_5},
       digit_sound{'6', Lang::Sounds::OGG_6},
       digit_sound{'7', Lang::Sounds::OGG_7},
       digit_sound{'8', Lang::Sounds::OGG_8},
       digit_sound{'9', Lang::Sounds::OGG_9}}};

  // This sentence uses 9KB of SRAM, so we need to wait for it to finish
  Alert(Lang::Strings::ACTIVATION, message.c_str(), "link",
        Lang::Sounds::OGG_ACTIVATION);

  for (const auto &digit : code) {
    auto it = std::find_if(
        digit_sounds.begin(), digit_sounds.end(),
        [digit](const digit_sound &ds) { return ds.digit == digit; });
    if (it != digit_sounds.end()) {
      audio_service_.PlaySound(it->sound);
    }
  }
}

void Application::Alert(const char *status, const char *message,
                        const char *emotion, const std::string_view &sound) {
  ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
  auto display = Board::GetInstance().GetDisplay();
  display->SetStatus(status);
  display->SetEmotion(emotion);
  display->SetChatMessage("system", message);
  if (!sound.empty()) {
    audio_service_.PlaySound(sound);
  }
}

void Application::DismissAlert() {
  if (device_state_ == kDeviceStateIdle) {
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(Lang::Strings::STANDBY);
    display->SetEmotion("neutral");
    display->SetChatMessage("system", "");
  }
}

void Application::ToggleChatState() {
  // Block AI chat when Intercom list is visible
  if (IsIntercomContactsVisible()) {
    ESP_LOGI(TAG, "Intercom UI is visible - ignoring chat toggle");
    return;
  }
  
  if (device_state_ == kDeviceStateActivating) {
    SetDeviceState(kDeviceStateIdle);
    return;
  } else if (device_state_ == kDeviceStateWifiConfiguring) {
    audio_service_.EnableAudioTesting(true);
    SetDeviceState(kDeviceStateAudioTesting);
    return;
  } else if (device_state_ == kDeviceStateAudioTesting) {
    audio_service_.EnableAudioTesting(false);
    SetDeviceState(kDeviceStateWifiConfiguring);
    return;
  }

  if (!protocol_) {
    ESP_LOGE(TAG, "Protocol not initialized");
    return;
  }

  if (device_state_ == kDeviceStateIdle) {
    Schedule([this]() {
      if (!protocol_->IsAudioChannelOpened()) {
        SetDeviceState(kDeviceStateConnecting);
        if (!protocol_->OpenAudioChannel()) {
          return;
        }
      }

      SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                            : kListeningModeRealtime);
    });
  } else if (device_state_ == kDeviceStateSpeaking) {
    Schedule([this]() { AbortSpeaking(kAbortReasonNone); });
  } else if (device_state_ == kDeviceStateListening) {
    Schedule([this]() { protocol_->CloseAudioChannel(); });
  } else if (device_state_ == kDeviceStateConnecting) {
    // Cancel connecting state (e.g., from notification TTS timeout)
    Schedule([this]() {
      protocol_->CloseAudioChannel();
      SetDeviceState(kDeviceStateIdle);
    });
  }
}

void Application::ShowIntercomContacts() {
  ESP_LOGI(TAG, "📞 Opening Intercom Contacts");
  
  // Show UI immediately with loading state
  Schedule([this]() {
    ESP_LOGI(TAG, "📞 Schedule: Creating Intercom UI");
    
    // Initialize UI callbacks if not done
    InitIntercomContactsUI();
    
    // Show loading state with demo contact
    std::vector<IntercomContact> loading_contacts;
    IntercomContact loading;
    loading.id = "0";
    loading.name = "Đang tải...";
    loading.mac = "";
    loading.owner = "";
    loading.is_online = false;
    loading.is_own_device = false;
    loading_contacts.push_back(loading);
    
    intercom_contacts_ui_.SetContacts(loading_contacts);
    intercom_contacts_ui_.Show();
    
    ESP_LOGI(TAG, "📞 Intercom UI shown with loading state");
  });
  
  // Fetch contacts in background task with larger stack
  xTaskCreate([](void* param) {
    Application* app = static_cast<Application*>(param);
    
    std::vector<IntercomContact> contacts;
    
    ESP_LOGI("Intercom", "📞 Background task started");
    
    if (!WifiStation::GetInstance().IsConnected()) {
      ESP_LOGW("Intercom", "📞 WiFi not connected!");
    } else {
      ESP_LOGI("Intercom", "📞 WiFi connected, checking token...");
      Settings settings("wifi", false);
      std::string token = settings.GetString("access_token", "");
      
      if (token.empty()) {
        ESP_LOGW("Intercom", "📞 No access_token found in NVS!");
      } else {
        ESP_LOGI("Intercom", "📞 Token found (len=%d)", (int)token.length());
        auto& board = Board::GetInstance();
        auto network = board.GetNetwork();
        auto http = network->CreateHttp(10000);
        
        std::string mac = SystemInfo::GetMacAddress();
        std::string url = "https://xiaozhi-ai-iot.vn/api/v1/device/intercom-contacts";
        
        http->SetHeader("Content-Type", "application/json");
        http->SetHeader("device-id", mac.c_str());
        http->SetHeader("Authorization", std::string("Bearer ") + token);
        
        ESP_LOGI("Intercom", "📞 GET %s", url.c_str());
        ESP_LOGI("Intercom", "📞 device-id: %s", mac.c_str());
        
        if (!http->Open("GET", url)) {
          ESP_LOGE("Intercom", "📞 Failed to open HTTP connection!");
        } else {
          int status_code = http->GetStatusCode();
          ESP_LOGI("Intercom", "📞 API Status: %d", status_code);
          
          if (status_code == 200) {
            std::string body = http->ReadAll();
            ESP_LOGI("Intercom", "📞 Response: %.200s", body.c_str());
            
            cJSON* root = cJSON_Parse(body.c_str());
            if (root) {
              cJSON* contacts_arr = cJSON_GetObjectItem(root, "contacts");
              if (cJSON_IsArray(contacts_arr)) {
                int count = cJSON_GetArraySize(contacts_arr);
                for (int i = 0; i < count; i++) {
                  cJSON* item = cJSON_GetArrayItem(contacts_arr, i);
                  
                  IntercomContact c;
                  cJSON* id = cJSON_GetObjectItem(item, "id");
                  cJSON* name = cJSON_GetObjectItem(item, "name");
                  cJSON* contact_mac = cJSON_GetObjectItem(item, "mac");
                  cJSON* owner = cJSON_GetObjectItem(item, "owner");
                  cJSON* type = cJSON_GetObjectItem(item, "type");
                  cJSON* status = cJSON_GetObjectItem(item, "status");
                  
                  if (cJSON_IsString(id)) c.id = id->valuestring;
                  if (cJSON_IsString(name)) c.name = name->valuestring;
                  if (cJSON_IsString(contact_mac)) c.mac = contact_mac->valuestring;
                  if (cJSON_IsString(owner)) c.owner = owner->valuestring;
                  if (cJSON_IsString(type)) c.is_own_device = (strcmp(type->valuestring, "own") == 0);
                  if (cJSON_IsString(status)) c.is_online = (strcmp(status->valuestring, "online") == 0);
                  
                  contacts.push_back(c);
                  ESP_LOGI("Intercom", "📞 Contact: %s (%s)", c.name.c_str(), c.mac.c_str());
                }
              }
              cJSON_Delete(root);
            }
          }
          http->Close();
        }
      }
    }
    
    // If no contacts fetched, use demo
    if (contacts.empty()) {
      ESP_LOGI("Intercom", "📞 No contacts from server, using demo");
      IntercomContact c1;
      c1.id = "1";
      c1.name = "Demo - LCD 2.8";
      c1.mac = "00:00:00:00:00:01";
      c1.owner = "Demo";
      c1.is_online = false;
      c1.is_own_device = true;
      contacts.push_back(c1);
    }
    
    // Update UI on main thread
    auto contacts_copy = new std::vector<IntercomContact>(std::move(contacts));
    app->Schedule([app, contacts_copy]() {
      // Always update UI - don't check visibility since task may finish before UI is shown
      app->intercom_contacts_ui_.SetContacts(*contacts_copy);
      ESP_LOGI("Intercom", "📞 Updated UI with %d contacts", (int)contacts_copy->size());
      delete contacts_copy;
    });
    
    vTaskDelete(NULL);
  }, "intercom_fetch", 8192, this, 5, NULL);  // 8KB stack
}

void Application::HideIntercomContacts() {
  intercom_contacts_ui_.Hide();
}

void Application::InitIntercomContactsUI() {
  static bool initialized = false;
  if (initialized) return;
  initialized = true;
  
  // Set up callbacks
  intercom_contacts_ui_.OnContactSelected([this](const IntercomContact& contact) {
    OnIntercomContactSelected(contact);
  });
  
  intercom_contacts_ui_.OnCancel([this]() {
    ESP_LOGI(TAG, "Intercom contacts cancelled");
  });
}

void Application::OnIntercomContactSelected(const IntercomContact& contact) {
  // Must schedule to main thread - button callback runs on different thread!
  Schedule([this, contact_name = contact.name, contact_mac = contact.mac]() {
    ESP_LOGI(TAG, "📞 Selected contact: %s (MAC: %s)", contact_name.c_str(), contact_mac.c_str());
    
    // Hide Intercom UI (safe on main thread)
    intercom_contacts_ui_.Hide();
    
    // Show simple notification
    auto display = Board::GetInstance().GetDisplay();
    if (display) {
      display->ShowNotification("Đang gọi...", 2000);
    }
    
    ESP_LOGI(TAG, "📞 Intercom call to: %s (%s)", contact_name.c_str(), contact_mac.c_str());
    
    // TODO: Implement actual intercom call via MQTT/UDP
  });
}


void Application::StartListening() {
  if (device_state_ == kDeviceStateActivating) {
    SetDeviceState(kDeviceStateIdle);
    return;
  } else if (device_state_ == kDeviceStateWifiConfiguring) {
    audio_service_.EnableAudioTesting(true);
    SetDeviceState(kDeviceStateAudioTesting);
    return;
  }

  if (!protocol_) {
    ESP_LOGE(TAG, "Protocol not initialized");
    return;
  }

  if (device_state_ == kDeviceStateIdle) {
    Schedule([this]() {
      if (!protocol_->IsAudioChannelOpened()) {
        SetDeviceState(kDeviceStateConnecting);
        if (!protocol_->OpenAudioChannel()) {
          return;
        }
      }

      SetListeningMode(kListeningModeManualStop);
    });
  } else if (device_state_ == kDeviceStateSpeaking) {
    Schedule([this]() {
      AbortSpeaking(kAbortReasonNone);
      SetListeningMode(kListeningModeManualStop);
    });
  }
}

void Application::StopListening() {
  if (device_state_ == kDeviceStateAudioTesting) {
    audio_service_.EnableAudioTesting(false);
    SetDeviceState(kDeviceStateWifiConfiguring);
    return;
  }

  const std::array<int, 3> valid_states = {
      kDeviceStateListening,
      kDeviceStateSpeaking,
      kDeviceStateIdle,
  };
  // If not valid, do nothing
  if (std::find(valid_states.begin(), valid_states.end(), device_state_) ==
      valid_states.end()) {
    return;
  }

  Schedule([this]() {
    if (device_state_ == kDeviceStateListening) {
      protocol_->SendStopListening();
      SetDeviceState(kDeviceStateIdle);
    }
  });
}

void Application::Start() {
  auto &board = Board::GetInstance();
  SetDeviceState(kDeviceStateStarting);

  /* Setup the display */
  auto display = board.GetDisplay();

  // Print board name/version info
  display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

#if (0) // Test QR code display
  // Capture display pointer for callback
  static Display *s_display = display;
  esp_qrcode_config_t qrcode_cfg = {.display_func =
                                        [](esp_qrcode_handle_t qrcode) {
                                          if (s_display && qrcode) {
                                            s_display->DisplayQRCode(qrcode,
                                                                     nullptr);
                                          }
                                        },
                                    .max_qrcode_version = 10,
                                    .qrcode_ecc_level = ESP_QRCODE_ECC_MED};

  // Create URL format for QR code
  std::string qr_text = "1234567890";
  esp_err_t err = esp_qrcode_generate(&qrcode_cfg, qr_text.c_str());
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to generate test QR code");
  }
  return;
#endif

  /* Setup the audio service */
  auto codec = board.GetAudioCodec();
  audio_service_.Initialize(codec);
  audio_service_.Start();
  
  // Load wake word models from srmodels partition
  // This is needed when assets partition is disabled
  srmodel_list_t* models = esp_srmodel_init("model");
  if (models != nullptr) {
    audio_service_.SetModelsList(models);
    ESP_LOGI(TAG, "Wake word models loaded from srmodels partition");
  } else {
    ESP_LOGW(TAG, "No wake word models found - barge-in disabled");
  }

  AudioServiceCallbacks callbacks;
  callbacks.on_send_queue_available = [this]() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
  };
  callbacks.on_wake_word_detected = [this](const std::string &wake_word) {
    xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
  };
  callbacks.on_vad_change = [this](bool speaking) {
    xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
  };
  audio_service_.SetCallbacks(callbacks);

  // Start the main event loop task with priority 3
  xTaskCreate(
      [](void *arg) {
        ((Application *)arg)->MainEventLoop();
        vTaskDelete(NULL);
      },
      "main_event_loop", 1024 * 3 + 512, this, 3,
      &main_event_loop_task_handle_);

  /* Start the clock timer to update the status bar */
  esp_timer_start_periodic(clock_timer_handle_, 1000000);

  /* Wait for the network to be ready */
  board.StartNetwork();

  music_ = new Esp32Music();
  if (music_ != nullptr) {
    music_->Initialize();
  }

  radio_ = new Esp32Radio();
  if (radio_ != nullptr) {
    radio_->Initialize();
  }

#ifdef CONFIG_SD_CARD_ENABLE
  auto sd_card = board.GetSdCard();
  if (sd_card != nullptr) {
    if (sd_card->Initialize() == ESP_OK) {
      ESP_LOGI(TAG, "SD card mounted successfully");
      sd_music_ = new Esp32SdMusic();
      sd_music_->Initialize(sd_card);
      sd_music_->loadTrackList();
    } else {
      ESP_LOGW(TAG, "Failed to mount SD card");
    }
  }
#endif

  // Update the status bar immediately to show the network state
  display->UpdateStatusBar(true);

  // Check for new assets version
  CheckAssetsVersion();

  // Check for new firmware version or get the MQTT broker address
  Ota ota;
  CheckNewVersion(ota);

  // Start the OTA server
  auto &ota_server = ota::OtaServer::GetInstance();
  if (ota_server.Start() == ESP_OK) {
    ESP_LOGI(TAG, "OTA server started successfully");
  } else {
    ESP_LOGE(TAG, "Failed to start OTA server");
  }

  // Initialize the protocol
  display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

  auto &wifi_station = WifiStation::GetInstance();
  std::string ssid = "SSID: " + wifi_station.GetSsid();
  std::string ip_address = "IP: " + wifi_station.GetIpAddress();
  display->SetChatMessage("assistant", ssid.c_str());
  display->SetChatMessage("assistant", ip_address.c_str());
  vTaskDelay(pdMS_TO_TICKS(2000));

  // Add MCP common tools before initializing the protocol
  auto &mcp_server = McpServer::GetInstance();
  mcp_server.AddCommonTools();
  mcp_server.AddUserOnlyTools();

  if (ota.HasMqttConfig()) {
    protocol_ = std::make_unique<MqttProtocol>();
  } else if (ota.HasWebsocketConfig()) {
    protocol_ = std::make_unique<WebsocketProtocol>();
  } else {
    ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
    protocol_ = std::make_unique<MqttProtocol>();
  }

  protocol_->OnConnected([this]() { DismissAlert(); });

  protocol_->OnNetworkError([this](const std::string &message) {
    last_error_message_ = message;
    xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
  });
  protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
    ESP_LOGI(TAG, "OnIncomingAudio: state=%d, packet_size=%zu", device_state_,
             packet->payload.size());
    if (device_state_ == kDeviceStateSpeaking) {
      audio_service_.PushPacketToDecodeQueue(std::move(packet));
      ESP_LOGD(TAG, "Audio packet pushed to decode queue");
    } else {
      ESP_LOGW(TAG, "Dropping audio packet - not in speaking state");
    }
  });
  protocol_->OnAudioChannelOpened([this, codec, &board]() {
    board.SetPowerSaveMode(false);
    if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
      ESP_LOGW(TAG,
               "Server sample rate %d does not match device output sample rate "
               "%d, resampling may cause distortion",
               protocol_->server_sample_rate(), codec->output_sample_rate());
    }
  });
  protocol_->OnAudioChannelClosed([this, &board]() {
    board.SetPowerSaveMode(true);
    Schedule([this]() {
      auto display = Board::GetInstance().GetDisplay();
      display->SetChatMessage("system", "");
      SetDeviceState(kDeviceStateIdle);
    });
  });
  protocol_->OnIncomingJson([this, display](const cJSON *root) {
    // Parse JSON data
    auto type = cJSON_GetObjectItem(root, "type");
    if (strcmp(type->valuestring, "tts") == 0) {
      auto state = cJSON_GetObjectItem(root, "state");
      if (strcmp(state->valuestring, "start") == 0) {
        Schedule([this]() {
          aborted_ = false;

          // If device is idle and audio channel not open, open it first
          // This handles push notifications where server sends TTS directly
          if (device_state_ == kDeviceStateIdle) {
            if (!protocol_->IsAudioChannelOpened()) {
              ESP_LOGI(TAG,
                       "TTS received while IDLE, opening audio channel...");
              if (!protocol_->OpenAudioChannel()) {
                ESP_LOGE(TAG, "Failed to open audio channel for TTS");
                return;
              }
            }
          }

          if (device_state_ == kDeviceStateIdle ||
              device_state_ == kDeviceStateListening ||
              device_state_ == kDeviceStateConnecting) {
            SetDeviceState(kDeviceStateSpeaking);
          }
        });
      } else if (strcmp(state->valuestring, "stop") == 0) {
        Schedule([this]() {
          if (device_state_ == kDeviceStateSpeaking) {
            if (listening_mode_ == kListeningModeManualStop) {
              SetDeviceState(kDeviceStateIdle);
            } else {
              SetDeviceState(kDeviceStateListening);
            }
          }
        });
      } else if (strcmp(state->valuestring, "sentence_start") == 0) {
        auto text = cJSON_GetObjectItem(root, "text");
        if (cJSON_IsString(text)) {
          ESP_LOGI(TAG, "<< %s", text->valuestring);
          Schedule([this, display, message = std::string(text->valuestring)]() {
            display->SetChatMessage("assistant", message.c_str());
          });
        }
      }
    } else if (strcmp(type->valuestring, "stt") == 0) {
      auto text = cJSON_GetObjectItem(root, "text");
      if (cJSON_IsString(text)) {
        ESP_LOGI(TAG, ">> %s", text->valuestring);
        Schedule([this, display, message = std::string(text->valuestring)]() {
          display->SetChatMessage("user", message.c_str());
        });
      }
    } else if (strcmp(type->valuestring, "llm") == 0) {
      auto emotion = cJSON_GetObjectItem(root, "emotion");
      if (cJSON_IsString(emotion)) {
        Schedule(
            [this, display, emotion_str = std::string(emotion->valuestring)]() {
              display->SetEmotion(emotion_str.c_str());
            });
      }
    } else if (strcmp(type->valuestring, "mcp") == 0) {
      auto payload = cJSON_GetObjectItem(root, "payload");
      if (cJSON_IsObject(payload)) {
        McpServer::GetInstance().ParseMessage(payload);
      }
    } else if (strcmp(type->valuestring, "system") == 0) {
      auto command = cJSON_GetObjectItem(root, "command");
      if (cJSON_IsString(command)) {
        ESP_LOGI(TAG, "System command: %s", command->valuestring);
        if (strcmp(command->valuestring, "reboot") == 0) {
          // Do a reboot if user requests a OTA update
          Schedule([this]() { Reboot(); });
        } else {
          ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
        }
      }
    } else if (strcmp(type->valuestring, "alert") == 0) {
      auto status = cJSON_GetObjectItem(root, "status");
      auto message = cJSON_GetObjectItem(root, "message");
      auto emotion = cJSON_GetObjectItem(root, "emotion");
      if (cJSON_IsString(status) && cJSON_IsString(message) &&
          cJSON_IsString(emotion)) {
        Alert(status->valuestring, message->valuestring, emotion->valuestring,
              Lang::Sounds::OGG_VIBRATION);
      } else {
        ESP_LOGW(TAG, "Alert command requires status, message and emotion");
      }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
    } else if (strcmp(type->valuestring, "custom") == 0) {
      auto payload = cJSON_GetObjectItem(root, "payload");
      ESP_LOGI(TAG, "Received custom message: %s",
               cJSON_PrintUnformatted(root));
      if (cJSON_IsObject(payload)) {
        Schedule(
            [this, display,
             payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
              display->SetChatMessage("system", payload_str.c_str());
            });
      } else {
        ESP_LOGW(TAG, "Invalid custom message format: missing payload");
      }
#endif
    } else if (strcmp(type->valuestring, "notification") == 0) {
      // Notification messages are handled by OnMqttNotification callback
      // This is called via mqtt_notification.cc, so just log and skip here
      ESP_LOGD(TAG, "Notification message - handled by OnMqttNotification");
    } else if (strcmp(type->valuestring, "goodbye") == 0) {
      ESP_LOGI(TAG, "Received goodbye from server");
      Schedule([this]() {
        if (protocol_->IsAudioChannelOpened()) {
          protocol_->CloseAudioChannel();
        }
        SetDeviceState(kDeviceStateIdle);
      });
    } else {
      ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
    }
  });
  bool protocol_started = protocol_->Start();

  SystemInfo::PrintHeapStats();

#ifdef CONFIG_ENABLE_MQTT_NOTIFICATIONS
  // Initialize MQTT push notifications client
  InitializeMqttNotifications();
#endif

  SetDeviceState(kDeviceStateIdle);

  has_server_time_ = ota.HasServerTime();
  if (protocol_started) {
    std::string message =
        std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
    display->ShowNotification(message.c_str());
    display->SetChatMessage("system", "");
    // Play the success sound to indicate the device is ready
    audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
  }
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    main_tasks_.push_back(std::move(callback));
  }
  xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop() {
  while (true) {
    auto bits = xEventGroupWaitBits(
        event_group_,
        MAIN_EVENT_SCHEDULE | MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED | MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK | MAIN_EVENT_ERROR,
        pdTRUE, pdFALSE, portMAX_DELAY);

    if (bits & MAIN_EVENT_ERROR) {
      SetDeviceState(kDeviceStateIdle);
      Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark",
            Lang::Sounds::OGG_EXCLAMATION);
    }

    if (bits & MAIN_EVENT_SEND_AUDIO) {
      while (auto packet = audio_service_.PopPacketFromSendQueue()) {
        if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
          break;
        }
      }
    }

    if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
      OnWakeWordDetected();
    }

    if (bits & MAIN_EVENT_VAD_CHANGE) {
      if (device_state_ == kDeviceStateListening) {
        auto led = Board::GetInstance().GetLed();
        led->OnStateChanged();
      }
    }

    if (bits & MAIN_EVENT_SCHEDULE) {
      std::unique_lock<std::mutex> lock(mutex_);
      auto tasks = std::move(main_tasks_);
      lock.unlock();
      for (auto &task : tasks) {
        task();
      }
    }

    if (bits & MAIN_EVENT_CLOCK_TICK) {
      clock_ticks_++;
      auto display = Board::GetInstance().GetDisplay();
      display->UpdateStatusBar();

      // Print the debug info every 10 seconds
      if (clock_ticks_ % 10 == 0) {
        // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
        // SystemInfo::PrintTaskList();
        SystemInfo::PrintHeapStats();
      }
#ifdef CONFIG_WEATHER_IDLE_DISPLAY_ENABLE
      if (device_state_ == kDeviceStateIdle) {
        if ((music_ && music_->IsPlaying()) ||
            (radio_ && radio_->IsPlaying()) ||
            (sd_music_ && sd_music_->IsPlaying())) {
          // When music/radio is playing, hide the idle screen
          display->HideIdleCard();
        } else {
          // Update the clock every second
          UpdateIdleDisplay();

          // Weather fetch logic: call at the 5th second after boot OR every 30
          // minutes (1800 seconds)
          if (clock_ticks_ == 5 || clock_ticks_ % 1800 == 0) {
            ESP_LOGI(TAG, "Khoi tao Task lay thoi tiet...");
            xTaskCreate(
                [](void *arg) {
                  auto &weather_service = WeatherService::GetInstance();
                  if (weather_service.FetchWeatherData()) {
                    Application *app = static_cast<Application *>(arg);
                    app->UpdateIdleDisplay();
                  }
                  vTaskDelete(NULL);
                },
                "weather_task", 1024 * 4, this, 5, NULL);
          }
        }
      }
#endif
    }
  }
}

void Application::OnWakeWordDetected() {
  if (!protocol_) {
    return;
  }
  
  // Block wake word when Intercom UI is visible
  if (IsIntercomContactsVisible()) {
    ESP_LOGI(TAG, "Wake word ignored - Intercom UI is visible");
    return;
  }

  if (device_state_ == kDeviceStateIdle) {
    audio_service_.EncodeWakeWord();

    if (!protocol_->IsAudioChannelOpened()) {
      SetDeviceState(kDeviceStateConnecting);
      if (!protocol_->OpenAudioChannel()) {
        audio_service_.EnableWakeWordDetection(true);
        return;
      }
    }

    auto wake_word = audio_service_.GetLastWakeWord();
    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
    // Encode and send the wake word data to the server
    while (auto packet = audio_service_.PopWakeWordPacket()) {
      protocol_->SendAudio(std::move(packet));
    }
    // Set the chat state to wake word detected
    protocol_->SendWakeWordDetected(wake_word);
    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                          : kListeningModeRealtime);
#else
    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                          : kListeningModeRealtime);
    // Play the pop up sound to indicate the wake word is detected
    audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
  } else if (device_state_ == kDeviceStateSpeaking) {
    AbortSpeaking(kAbortReasonWakeWordDetected);
  } else if (device_state_ == kDeviceStateActivating) {
    SetDeviceState(kDeviceStateIdle);
  }
}

void Application::AbortSpeaking(AbortReason reason) {
  ESP_LOGI(TAG, "Abort speaking");
  aborted_ = true;
  if (protocol_) {
    protocol_->SendAbortSpeaking(reason);
  }
}

void Application::SetListeningMode(ListeningMode mode) {
  listening_mode_ = mode;
  SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state) {
  if (device_state_ == state) {
    return;
  }

  clock_ticks_ = 0;
  auto previous_state = device_state_;
  device_state_ = state;
  ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

  // Send the state change event
  DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state,
                                                              state);

  auto &board = Board::GetInstance();
  auto display = board.GetDisplay();

#ifdef CONFIG_WEATHER_IDLE_DISPLAY_ENABLE
  // --- [ADD HIDDEN/SHOWABLE LOGIC FOR IDLE SCREEN] ---
  if (state != kDeviceStateIdle) {
    // If not Idle, hide the idle screen
    ESP_LOGI(TAG, "Hiding idle screen due to state change: %s -> %s",
             STATE_STRINGS[previous_state], STATE_STRINGS[state]);
    display->HideIdleCard();
  }
  // -----------------------------
#endif

  auto led = board.GetLed();
  led->OnStateChanged();
  // Stop music playback when transitioning from idle state to any other state
  if (previous_state == kDeviceStateIdle && state != kDeviceStateIdle) {
    if (music_) {
      ESP_LOGI(TAG, "Stopping music streaming due to state change: %s -> %s",
               STATE_STRINGS[previous_state], STATE_STRINGS[state]);
      music_->StopStreaming();
    }
    if (radio_) {
      ESP_LOGI(TAG, "Stopping radio streaming due to state change: %s -> %s",
               STATE_STRINGS[previous_state], STATE_STRINGS[state]);
      radio_->Stop();
    }
    if (sd_music_) {
      ESP_LOGI(TAG, "Stopping SD music due to state change: %s -> %s",
               STATE_STRINGS[previous_state], STATE_STRINGS[state]);
      sd_music_->stop();
    }

    display->ClearQRCode();
  }
  switch (state) {
  case kDeviceStateUnknown:
  case kDeviceStateIdle:
    display->SetStatus(Lang::Strings::STANDBY);
    display->SetEmotion("neutral");
    display->SetChatMessage("system", ""); // Clear any previous messages
    audio_service_.EnableVoiceProcessing(false);
    audio_service_.EnableWakeWordDetection(true);
    break;
  case kDeviceStateConnecting:
    display->SetStatus(Lang::Strings::CONNECTING);
    display->SetEmotion("neutral");
    display->SetChatMessage("system", "");
    break;
  case kDeviceStateListening:
    display->SetStatus(Lang::Strings::LISTENING);
    display->SetEmotion("neutral");

    // Make sure the audio processor is running
    if (!audio_service_.IsAudioProcessorRunning()) {
      // Send the start listening command
      protocol_->SendStartListening(listening_mode_);
      audio_service_.EnableVoiceProcessing(true);
      audio_service_.EnableWakeWordDetection(false);
    }
    break;
  case kDeviceStateSpeaking:
    display->SetStatus(Lang::Strings::SPEAKING);

    // With AEC enabled, keep voice processing active for barge-in detection
    if (listening_mode_ != kListeningModeRealtime) {
      audio_service_.EnableVoiceProcessing(false);
    }
    // Always enable wake word detection in speaking mode for barge-in
    // AEC in wake word detector allows detecting wake word even while speaker is playing
    ESP_LOGI(TAG, "Speaking mode: enabling wake word detection for barge-in");
    audio_service_.EnableWakeWordDetection(true);
    audio_service_.ResetDecoder();
    break;
  default:
    // Do nothing
    break;
  }
}

void Application::Reboot() {
  ESP_LOGI(TAG, "Rebooting...");
  // Disconnect the audio channel
  if (protocol_ && protocol_->IsAudioChannelOpened()) {
    protocol_->CloseAudioChannel();
  }
  protocol_.reset();
  audio_service_.Stop();

  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
}

bool Application::UpgradeFirmware(Ota &ota, const std::string &url) {
  auto &board = Board::GetInstance();
  auto display = board.GetDisplay();

  // Use provided URL or get from OTA object
  std::string upgrade_url = url.empty() ? ota.GetFirmwareUrl() : url;
  std::string version_info =
      url.empty() ? ota.GetFirmwareVersion() : "(Manual upgrade)";

  // Close audio channel if it's open
  if (protocol_ && protocol_->IsAudioChannelOpened()) {
    ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
    protocol_->CloseAudioChannel();
  }
  ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());

  Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download",
        Lang::Sounds::OGG_UPGRADE);
  vTaskDelay(pdMS_TO_TICKS(3000));

  SetDeviceState(kDeviceStateUpgrading);

  std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
  display->SetChatMessage("system", message.c_str());

  board.SetPowerSaveMode(false);
  audio_service_.Stop();
  vTaskDelay(pdMS_TO_TICKS(1000));

  bool upgrade_success = ota.StartUpgradeFromUrl(
      upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
          char buffer[32];
          snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress,
                   speed / 1024);
          display->SetChatMessage("system", buffer);
        }).detach();
      });

  if (!upgrade_success) {
    // Upgrade failed, restart audio service and continue running
    ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and "
                  "continuing operation...");
    audio_service_.Start();       // Restart audio service
    board.SetPowerSaveMode(true); // Restore power save mode
    Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark",
          Lang::Sounds::OGG_EXCLAMATION);
    vTaskDelay(pdMS_TO_TICKS(3000));
    return false;
  } else {
    // Upgrade success, reboot immediately
    ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
    display->SetChatMessage("system", "Upgrade successful, rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
    Reboot();
    return true;
  }
}

void Application::WakeWordInvoke(const std::string &wake_word) {
  if (!protocol_) {
    return;
  }

  if (device_state_ == kDeviceStateIdle) {
    audio_service_.EncodeWakeWord();

    if (!protocol_->IsAudioChannelOpened()) {
      SetDeviceState(kDeviceStateConnecting);
      if (!protocol_->OpenAudioChannel()) {
        audio_service_.EnableWakeWordDetection(true);
        return;
      }
    }

    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD || CONFIG_USE_CUSTOM_WAKE_WORD
    // Encode and send the wake word data to the server
    while (auto packet = audio_service_.PopWakeWordPacket()) {
      protocol_->SendAudio(std::move(packet));
    }
    // Set the chat state to wake word detected
    protocol_->SendWakeWordDetected(wake_word);
    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                          : kListeningModeRealtime);
#else
    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                          : kListeningModeRealtime);
    // Play the pop up sound to indicate the wake word is detected
    audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
  } else if (device_state_ == kDeviceStateSpeaking) {
    Schedule([this]() { AbortSpeaking(kAbortReasonNone); });
  } else if (device_state_ == kDeviceStateListening) {
    Schedule([this]() {
      if (protocol_) {
        protocol_->CloseAudioChannel();
      }
    });
  }
}

bool Application::CanEnterSleepMode() {
  if (device_state_ != kDeviceStateIdle) {
    return false;
  }

  if (protocol_ && protocol_->IsAudioChannelOpened()) {
    return false;
  }

  if (!audio_service_.IsIdle()) {
    return false;
  }

  // Now it is safe to enter sleep mode
  return true;
}

void Application::SendMcpMessage(const std::string &payload) {
  if (protocol_ == nullptr) {
    return;
  }

  // Make sure you are using main thread to send MCP message
  if (xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_) {
    protocol_->SendMcpMessage(payload);
  } else {
    Schedule([this, payload = std::move(payload)]() {
      protocol_->SendMcpMessage(payload);
    });
  }
}

void Application::SetAecMode(AecMode mode) {
  aec_mode_ = mode;
  Schedule([this]() {
    auto &board = Board::GetInstance();
    auto display = board.GetDisplay();
    switch (aec_mode_) {
    case kAecOff:
      audio_service_.EnableDeviceAec(false);
      display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
      break;
    case kAecOnServerSide:
      audio_service_.EnableDeviceAec(false);
      display->ShowNotification(Lang::Strings::RTC_MODE_ON);
      break;
    case kAecOnDeviceSide:
      audio_service_.EnableDeviceAec(true);
      display->ShowNotification(Lang::Strings::RTC_MODE_ON);
      break;
    }

    // If the AEC mode is changed, close the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
      protocol_->CloseAudioChannel();
    }
  });
}

// New: Receive external audio data (such as music playback)
void Application::AddAudioData(AudioStreamPacket &&packet) {
  auto codec = Board::GetInstance().GetAudioCodec();
  if (device_state_ == kDeviceStateIdle && codec->output_enabled()) {
    // packet.payload contains raw PCM data (int16_t)
    if (packet.payload.size() >= 2) {
      size_t num_samples = packet.payload.size() / sizeof(int16_t);
      std::vector<int16_t> pcm_data(num_samples);
      memcpy(pcm_data.data(), packet.payload.data(), packet.payload.size());

      // Check if sample rate matches, if not, perform simple resampling
      if (packet.sample_rate != codec->output_sample_rate()) {
        // ESP_LOGI(TAG, "Resampling music audio from %d to %d Hz",
        //         packet.sample_rate, codec->output_sample_rate());

        // Validate sample rate parameters
        if (packet.sample_rate <= 0 || codec->output_sample_rate() <= 0) {
          ESP_LOGE(TAG, "Invalid sample rates: %d -> %d", packet.sample_rate,
                   codec->output_sample_rate());
          return;
        }

        std::vector<int16_t> resampled;

        if (packet.sample_rate > codec->output_sample_rate()) {
          ESP_LOGI(TAG,
                   "Music playback: Switching sample rate from %d Hz to %d Hz",
                   codec->output_sample_rate(), packet.sample_rate);

          // Try to dynamically switch sample rate
          if (codec->SetOutputSampleRate(packet.sample_rate)) {
            ESP_LOGI(
                TAG,
                "Successfully switched to music playback sample rate: %d Hz",
                packet.sample_rate);
          } else {
            ESP_LOGW(TAG,
                     "Cannot switch sample rate, continue using current sample "
                     "rate: %d Hz",
                     codec->output_sample_rate());
          }
        } else {
          // Upsampling: linear interpolation
          float upsample_ratio = codec->output_sample_rate() /
                                 static_cast<float>(packet.sample_rate);
          size_t expected_size =
              static_cast<size_t>(pcm_data.size() * upsample_ratio + 0.5f);
          resampled.reserve(expected_size);

          for (size_t i = 0; i < pcm_data.size(); ++i) {
            // Add original sample
            resampled.push_back(pcm_data[i]);

            // Calculate number of samples to interpolate
            int interpolation_count = static_cast<int>(upsample_ratio) - 1;
            if (interpolation_count > 0 && i + 1 < pcm_data.size()) {
              int16_t current = pcm_data[i];
              int16_t next = pcm_data[i + 1];
              for (int j = 1; j <= interpolation_count; ++j) {
                float t = static_cast<float>(j) / (interpolation_count + 1);
                int16_t interpolated =
                    static_cast<int16_t>(current + (next - current) * t);
                resampled.push_back(interpolated);
              }
            } else if (interpolation_count > 0) {
              // For the last sample, simply repeat it
              for (int j = 1; j <= interpolation_count; ++j) {
                resampled.push_back(pcm_data[i]);
              }
            }
          }

          ESP_LOGI(TAG, "Upsampled %d -> %d samples (ratio: %.2f)",
                   pcm_data.size(), resampled.size(), upsample_ratio);
        }

        pcm_data = std::move(resampled);
      }

      // Ensure audio output is enabled
      if (!codec->output_enabled()) {
        codec->EnableOutput(true);
      }

      // Send PCM data to audio codec
      codec->OutputData(pcm_data);

      audio_service_.UpdateOutputTimestamp();
    }
  }
}

void Application::PlaySound(const std::string_view &sound) {
  audio_service_.PlaySound(sound);
}

// --- [DienBien Mod]- WEATHER SCREEN UPDATE----
#ifdef CONFIG_WEATHER_IDLE_DISPLAY_ENABLE
void Application::UpdateIdleDisplay() {
  auto &weather_service = WeatherService::GetInstance();
  const WeatherInfo &weather_info = weather_service.GetWeatherInfo();

  IdleCardInfo card;

  // 1. THỜI GIAN
  time_t now = time(nullptr);
  struct tm tm_buf;
  if (localtime_r(&now, &tm_buf) != nullptr) {
    char buffer[35];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm_buf);
    card.time_text = buffer;

    strftime(buffer, sizeof(buffer), "%d/%m/%Y", &tm_buf);
    card.date_text = buffer;

    const char *wdays[] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư",
                           "Thứ Năm",  "Thứ Sáu", "Thứ Bảy"};
    card.day_text = wdays[tm_buf.tm_wday];
  }

  // 2. THÔNG TIN HỆ THỐNG
  auto &board = Board::GetInstance();
  card.network_icon = board.GetNetworkStateIcon();

  int battery_level;
  bool charging, discharging;
  const char *icon = nullptr;
  if (board.GetBatteryLevel(battery_level, charging, discharging)) {
    if (charging) {
      icon = FONT_AWESOME_BATTERY_BOLT;
    } else {
      const char *const levels[] = {
          FONT_AWESOME_BATTERY_EMPTY,          // 0-19%
          FONT_AWESOME_BATTERY_QUARTER,        // 20-39%
          FONT_AWESOME_BATTERY_HALF,           // 40-59%
          FONT_AWESOME_BATTERY_THREE_QUARTERS, // 60-79%
          FONT_AWESOME_BATTERY_FULL,           // 80-99%
          FONT_AWESOME_BATTERY_FULL            // 100%
      };
      icon = levels[battery_level / 20];
    }
    card.battery_level = battery_level;
    card.battery_icon = icon;
    card.is_charging = charging;
  } else {
    card.battery_icon = FONT_AWESOME_BATTERY_BOLT;
    card.battery_level = -1; // Không biết mức pin
    card.is_charging = false;
  }

  // 3. THÔNG TIN THỜI TIẾT
  if (weather_info.valid) {
    card.city = weather_info.city;

    char temp_buf[16];
    snprintf(temp_buf, sizeof(temp_buf), "%d°C", (int)round(weather_info.temp));
    card.temperature_text = temp_buf;

    card.description_text = weather_info.description;
    card.humidity_text = std::to_string(weather_info.humidity) + "%";

    char extra_buf[32];
    snprintf(extra_buf, sizeof(extra_buf), "%.1f m/s", weather_info.wind_speed);
    card.wind_text = extra_buf;

    // Cập nhật Forecast vào Card
    card.forecast = weather_info.forecast; // COPY DỮ LIỆU DỰ BÁO SANG UI

    card.icon = WeatherUI::GetWeatherIcon(weather_info.icon_code);
  } else {
    card.city = "Dang cap nhat...";
    card.temperature_text = "--";
    card.icon = "\uf128";
  }

  auto display = Board::GetInstance().GetDisplay();
  display->ShowIdleCard(card);
}
#endif
// --- [DienBien Mod]- END WEATHER SCREEN UPDATE----

#ifdef CONFIG_ENABLE_MQTT_NOTIFICATIONS
/**
 * @brief Initialize MQTT push notifications client
 *
 * Reads configuration from NVS (saved by OTA) and starts the MQTT client
 * to receive push notifications from server.
 */
void Application::InitializeMqttNotifications() {
  // Read MQTT config from NVS (saved by OTA ParseMqttConfig)
  Settings mqtt_settings("mqtt_common", false);
  std::string endpoint = mqtt_settings.GetString("endpoint");
  std::string username = mqtt_settings.GetString("username");
  std::string password = mqtt_settings.GetString("password");

  if (endpoint.empty()) {
    ESP_LOGW(TAG, "MQTT endpoint not configured, push notifications disabled");
    return;
  }

  // Get device MAC for topic subscription
  std::string mac = SystemInfo::GetMacAddress();

  // Set up notification callback
  auto &mqtt = MqttNotification::GetInstance();

  mqtt.SetOnNotification([this](const MqttNotificationData &n) {
    ESP_LOGI(TAG, "Received push notification: type=%s, title=%s",
             n.type.c_str(), n.title.c_str());

    // Capture a copy for the scheduled callback
    MqttNotificationData notification_copy = n;

    // Schedule on main thread
    Schedule(
        [this, notification_copy]() { OnMqttNotification(notification_copy); });
  });

  mqtt.SetOnAssetsUpdate([](const std::string &version, const std::string &hash,
                            const std::string &url) {
    ESP_LOGI(TAG, "Received assets update: version=%s, hash=%s",
             version.c_str(), hash.c_str());
    // TODO: Handle assets update (trigger OTA assets download)
  });

  // Set up intercom (Walkie-Talkie) callback
  mqtt.SetOnIntercom([this](const IntercomData &intercom) {
    ESP_LOGI(TAG, "Received intercom: from=%s, is_reply=%d",
             intercom.from_device_name.c_str(), intercom.is_reply);

    // Capture a copy for the scheduled callback
    IntercomData intercom_copy = intercom;

    // Schedule on main thread
    Schedule([this, intercom_copy]() { OnIntercom(intercom_copy); });
  });

  // Start MQTT client
  mqtt.Start(endpoint, mac, username, password);

  ESP_LOGI(TAG, "MQTT push notifications initialized, endpoint: %s",
           endpoint.c_str());
}

/**
 * @brief Handle received MQTT notification
 * @param notification The notification payload
 *
 * When useLLM/useTTS is true and device is idle, we need to:
 * 1. Display notification
 * 2. Open audio channel (send hello → server creates session)
 * 3. Send notification_speak request with content
 * 4. Server processes and returns TTS audio
 *
 * When useLLM/useTTS is false, we just play a notification sound.
 */
void Application::OnMqttNotification(const MqttNotificationData &notification) {
  auto display = Board::GetInstance().GetDisplay();

  // Display like active conversation:
  // - Title goes to status/notification bar (type: cảnh báo, thông báo, ...)
  // - Content displayed in center as chat message

  // Show title in notification bar if not empty
  if (!notification.title.empty()) {
    display->ShowNotification(notification.title.c_str());
  }

  // Show content in center as assistant message (like active conversation)
  if (!notification.content.empty()) {
    display->SetChatMessage("assistant", notification.content.c_str());
  }

  if (notification.useLLM) {
    // Need to open channel and send notification_speak request
    // Backend waits for device hello before sending TTS audio
    ESP_LOGI(TAG, "TTS notification received, title=%s, content=%s",
             notification.title.c_str(), notification.content.c_str());

    // Store content for TTS request
    std::string tts_content = notification.content;
    if (tts_content.empty()) {
      tts_content = notification.title;
    }

    // Schedule channel open and TTS request on main thread
    Schedule([this, tts_content]() {
      // Check if audio channel is already open (active conversation)
      if (protocol_->IsAudioChannelOpened()) {
        // Channel already open - just send notification_speak
        ESP_LOGI(TAG, "Channel open, sending notification_speak directly");

        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "type", "notification_speak");
        cJSON_AddStringToObject(msg, "content", tts_content.c_str());
        char *json = cJSON_PrintUnformatted(msg);

        ESP_LOGI(TAG, "Sending notification_speak: %s", json);
        protocol_->SendMcpMessage(json);

        cJSON_free(json);
        cJSON_Delete(msg);
        return;
      }

      // Device is idle, need to open channel first
      if (device_state_ != kDeviceStateIdle) {
        ESP_LOGW(TAG, "Device busy (%d) and channel not open, skipping TTS",
                 device_state_);
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
        return;
      }

      // Set state to connecting first
      SetDeviceState(kDeviceStateConnecting);

      // Open audio channel (send hello → server creates session)
      ESP_LOGI(TAG, "Opening audio channel for TTS notification...");
      if (!protocol_->OpenAudioChannel()) {
        ESP_LOGE(TAG, "Failed to open audio channel for TTS notification");
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
        SetDeviceState(kDeviceStateIdle);
        return;
      }

      // Build and send notification_speak request
      cJSON *msg = cJSON_CreateObject();
      cJSON_AddStringToObject(msg, "type", "notification_speak");
      cJSON_AddStringToObject(msg, "content", tts_content.c_str());
      char *json = cJSON_PrintUnformatted(msg);

      ESP_LOGI(TAG, "Sending notification_speak: %s", json);
      protocol_->SendMcpMessage(json);

      cJSON_free(json);
      cJSON_Delete(msg);

      // Set listening mode to manual stop so TTS stop returns to idle
      listening_mode_ = kListeningModeManualStop;

      // Don't set Speaking here - let tts start handler do it
      // This avoids getting stuck if backend doesn't respond
      ESP_LOGI(TAG, "Waiting for server TTS response...");

      // Note: Backend MUST send tts start message for device to transition to
      // Speaking If no response, device stays in Connecting. User can press
      // button to cancel.
    });
  } else {
    // No TTS, play notification sound
    audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    ESP_LOGI(TAG, "Notification displayed with sound: title=%s, content=%s",
             notification.title.c_str(), notification.content.c_str());
  }
}

/**
 * @brief Handle push notification that requires server-side AI processing
 * @param title Notification title
 * @param content Notification content to be processed by AI
 *
 * Note: This is a legacy function, main logic is now in OnMqttNotification.
 */
void Application::OnPushNotification(const std::string &title,
                                     const std::string &content) {
  // Show notification on display
  auto display = Board::GetInstance().GetDisplay();
  std::string message = title;
  if (!content.empty()) {
    if (!message.empty()) {
      message += ": ";
    }
    message += content;
  }
  display->ShowNotification(message.c_str());

  ESP_LOGI(TAG, "Push notification displayed: %s", message.c_str());
}

/**
 * @brief Handle incoming intercom message (Walkie-Talkie)
 * @param intercom The intercom payload
 */
void Application::OnIntercom(const IntercomData &intercom) {
  ESP_LOGI(TAG, "[INTERCOM] OnIntercom: type=%s, from=%s, msg=%s",
           intercom.type.c_str(), intercom.from_device_name.c_str(),
           intercom.message.c_str());

  if (intercom.is_reply) {
    HandleIntercomReply(intercom);
  } else {
    HandleIntercomMessage(intercom);
  }
}

/**
 * @brief Handle incoming intercom message (someone calling this device)
 */
void Application::HandleIntercomMessage(const IntercomData &intercom) {
  auto display = Board::GetInstance().GetDisplay();

  // Store conversation context for reply
  intercom_context_.active = true;
  intercom_context_.conversation_id = intercom.conversation_id;
  intercom_context_.reply_to_mac = intercom.reply_to_mac;
  intercom_context_.from_name = intercom.from_device_name;
  intercom_context_.last_activity_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

  // Build TTS message: "Tin nhắn từ [device]: [message]"
  std::string tts_content =
      "Tin nhắn từ " + intercom.from_device_name + ": " + intercom.message;

  // Display on screen
  display->ShowNotification(("📞 " + intercom.from_device_name).c_str());
  display->SetChatMessage("assistant", tts_content.c_str());

  ESP_LOGI(TAG, "[INTERCOM] Message from %s: %s",
           intercom.from_device_name.c_str(), intercom.message.c_str());

  // Play TTS and then auto-listen for reply
  Schedule([this, tts_content]() {
    // Check if audio channel is open
    if (protocol_->IsAudioChannelOpened()) {
      // Channel already open - send notification_speak
      cJSON *msg = cJSON_CreateObject();
      cJSON_AddStringToObject(msg, "type", "notification_speak");
      cJSON_AddStringToObject(msg, "content", tts_content.c_str());
      cJSON_AddBoolToObject(msg, "intercom", true); // Mark as intercom
      char *json = cJSON_PrintUnformatted(msg);

      ESP_LOGI(TAG, "[INTERCOM] Sending TTS: %s", json);
      protocol_->SendMcpMessage(json);

      cJSON_free(json);
      cJSON_Delete(msg);
      return;
    }

    // Need to open channel first
    if (device_state_ != kDeviceStateIdle) {
      ESP_LOGW(TAG, "[INTERCOM] Device busy, playing notification sound");
      audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
      return;
    }

    SetDeviceState(kDeviceStateConnecting);

    ESP_LOGI(TAG, "[INTERCOM] Opening audio channel for TTS...");
    if (!protocol_->OpenAudioChannel()) {
      ESP_LOGE(TAG, "[INTERCOM] Failed to open audio channel");
      audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
      SetDeviceState(kDeviceStateIdle);
      return;
    }

    // Send notification_speak with intercom flag
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "notification_speak");
    cJSON_AddStringToObject(msg, "content", tts_content.c_str());
    cJSON_AddBoolToObject(msg, "intercom", true);
    cJSON_AddStringToObject(msg, "conversation_id",
                            intercom_context_.conversation_id.c_str());
    char *json = cJSON_PrintUnformatted(msg);

    ESP_LOGI(TAG, "[INTERCOM] Sending TTS: %s", json);
    protocol_->SendMcpMessage(json);

    cJSON_free(json);
    cJSON_Delete(msg);

    // Set listening mode - will auto-listen after TTS finishes
    listening_mode_ = kListeningModeManualStop;
    ESP_LOGI(TAG, "[INTERCOM] Waiting for TTS, will auto-listen after");
  });
}

/**
 * @brief Handle incoming intercom reply (response from another device)
 */
void Application::HandleIntercomReply(const IntercomData &intercom) {
  auto display = Board::GetInstance().GetDisplay();

  // Update context activity
  intercom_context_.last_activity_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

  // Build TTS message: "Phản hồi từ [device]: [message]"
  std::string tts_content =
      "Phản hồi từ " + intercom.from_device_name + ": " + intercom.message;

  // Display on screen
  display->SetChatMessage("assistant", tts_content.c_str());

  ESP_LOGI(TAG, "[INTERCOM] Reply from %s: %s",
           intercom.from_device_name.c_str(), intercom.message.c_str());

  // Play TTS (similar flow to HandleIntercomMessage)
  Schedule([this, tts_content]() {
    if (protocol_->IsAudioChannelOpened()) {
      cJSON *msg = cJSON_CreateObject();
      cJSON_AddStringToObject(msg, "type", "notification_speak");
      cJSON_AddStringToObject(msg, "content", tts_content.c_str());
      cJSON_AddBoolToObject(msg, "intercom", true);
      char *json = cJSON_PrintUnformatted(msg);

      protocol_->SendMcpMessage(json);

      cJSON_free(json);
      cJSON_Delete(msg);
      return;
    }

    if (device_state_ != kDeviceStateIdle) {
      audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
      return;
    }

    SetDeviceState(kDeviceStateConnecting);

    if (!protocol_->OpenAudioChannel()) {
      audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
      SetDeviceState(kDeviceStateIdle);
      return;
    }

    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "notification_speak");
    cJSON_AddStringToObject(msg, "content", tts_content.c_str());
    cJSON_AddBoolToObject(msg, "intercom", true);
    char *json = cJSON_PrintUnformatted(msg);

    protocol_->SendMcpMessage(json);

    cJSON_free(json);
    cJSON_Delete(msg);

    listening_mode_ = kListeningModeManualStop;
  });
}

/**
 * @brief Start listening for intercom reply (called after TTS finishes)
 */
void Application::StartIntercomListen() {
  if (!intercom_context_.active) {
    ESP_LOGW(TAG,
             "[INTERCOM] StartIntercomListen called but no active context");
    return;
  }

  ESP_LOGI(TAG, "[INTERCOM] Auto-listen started, waiting for user reply...");

  // Start listening mode
  Schedule([this]() {
    if (device_state_ == kDeviceStateSpeaking) {
      // Already handling TTS, listening will start after
      listening_mode_ = kListeningModeAutoStop;
    } else if (device_state_ == kDeviceStateIdle) {
      // Start listening
      StartListening();
    }
  });
}

/**
 * @brief End intercom session
 */
void Application::EndIntercomSession(const std::string &reason) {
  ESP_LOGI(TAG, "[INTERCOM] Session ended: %s", reason.c_str());

  intercom_context_.active = false;
  intercom_context_.conversation_id.clear();
  intercom_context_.reply_to_mac.clear();
  intercom_context_.from_name.clear();
  intercom_context_.last_activity_ms = 0;
}
#endif