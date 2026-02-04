# ESP32 Firmware Agent Kit

> AI-powered firmware development kit for ESP32/ESP-IDF projects
> Compatible with: Antigravity, GitHub Copilot, Cursor, Windsurf

## 🚀 Quick Install

### Option 1: One-line install (bash)
```bash
curl -sSL https://raw.githubusercontent.com/Xiaozhi-AI-IoT-Vietnam/esp32-agent-kit/main/install.sh | bash
```

### Option 2: Manual install
```bash
# Clone the kit
git clone https://github.com/Xiaozhi-AI-IoT-Vietnam/esp32-agent-kit.git

# Copy to your ESP32 project
cp -r esp32-agent-kit/.agent /path/to/your/esp32-project/
cp esp32-agent-kit/GEMINI.md /path/to/your/esp32-project/
cp esp32-agent-kit/.github/copilot-instructions.md /path/to/your/esp32-project/.github/
```

---

## 📦 What's Included

```
esp32-agent-kit/
├── .agent/
│   ├── ARCHITECTURE.md      # System architecture
│   ├── skills/              # 5 ESP32-specific skills
│   │   ├── esp32-firmware/
│   │   ├── esp32-audio/
│   │   ├── lvgl-display/
│   │   ├── esp32-mqtt-protocol/
│   │   └── xiaozhi-patterns/
│   ├── workflows/           # 7 development workflows
│   │   ├── plan.md
│   │   ├── design.md
│   │   ├── code.md
│   │   ├── test.md
│   │   ├── review.md
│   │   ├── fix.md
│   │   └── git.md
│   ├── roles/               # 4 engineer roles
│   ├── rules/               # 4 coding rules
│   └── agents/              # 6 specialized agents
├── .github/
│   └── copilot-instructions.md  # For GitHub Copilot
├── GEMINI.md                # For Antigravity/Gemini
├── .cursorrules             # For Cursor AI
├── install.sh               # Installer script
└── README.md                # This file
```

---

## 🔧 Compatibility

| Tool | Config File | Status |
|------|-------------|--------|
| **Antigravity** | `GEMINI.md` + `.agent/` | ✅ Full support |
| **GitHub Copilot** | `.github/copilot-instructions.md` | ✅ Full support |
| **Cursor** | `.cursorrules` | ✅ Full support |
| **Windsurf** | `.windsurfrules` | ✅ Full support |
| **Cline/Claude** | `.clinerules` | ✅ Full support |

---

## 📚 Skills Overview

| Skill | Purpose |
|-------|---------|
| `esp32-firmware` | ESP-IDF, FreeRTOS, memory management |
| `esp32-audio` | I2S, Opus codec, wake word detection |
| `lvgl-display` | LVGL 9.x graphics, widgets, styles |
| `esp32-mqtt-protocol` | MQTT client, push notifications |
| `xiaozhi-patterns` | Application state machine, callbacks |

---

## 🔄 Workflows (Slash Commands)

| Command | Description |
|---------|-------------|
| `/plan` | Plan new feature with ESP32 constraints |
| `/design` | Design architecture and state machine |
| `/code` | Implement with ESP32 best practices |
| `/test` | Build, flash, and test on device |
| `/review` | Code review with ESP32 checklist |
| `/fix` | Debug and fix firmware issues |
| `/git` | Commit and push changes |

---

## 🛠️ Usage Examples

### With Antigravity
```
User: /code implement MQTT notification handling
```

### With GitHub Copilot Chat
```
@workspace How do I add a new notification type to the MQTT handler?
```

### With Cursor
```
Cmd+K: Add TTS playback for reminder notifications
```

---

## 📋 Coding Rules

The kit enforces ESP32-specific coding standards:

1. **Memory**: Use PSRAM for buffers >4KB
2. **Thread Safety**: Use `Schedule()` for UI updates
3. **Error Handling**: Always check `esp_err_t`
4. **Logging**: Use `ESP_LOGI/W/E` with TAG

---

## 🔗 References

- [Original Xiaozhi ESP32](https://github.com/78/xiaozhi-esp32)
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/)
- [LVGL 9.x Docs](https://docs.lvgl.io/9.4/)
- [Arduino-ESP32](https://github.com/espressif/arduino-esp32)

---

## 📄 License

MIT License - Use freely in your ESP32 projects!

---

**Made with ❤️ for ESP32 Firmware Developers**
