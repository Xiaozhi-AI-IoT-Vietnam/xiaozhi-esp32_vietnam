# 📋 Hướng Dẫn Cấu Hình Board - Xiaozhi ESP32 Vietnam

> Tài liệu này mô tả chi tiết cách chuyển đổi giữa các board Vietnam và các cấu hình của chúng.

---

## 🎯 Danh Sách Board Vietnam

| Board | Display | Audio | Buttons | Description |
|-------|---------|-------|---------|-------------|
| `xiaozhi-ai-iot-vietnam-1st` | NV3023 1.83" TFT | ES8311 + ES7210 | BOOT, Vol+, Vol- | Board chính với dual mic |
| `xingzhi-cube-1.54tft-wifi` | GC9A01 1.54" TFT | I2S Mic + Speaker | BOOT, Vol+, Vol- | Cube với màn hình tròn |
| `xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8` | ST7789 2.8" Touch | Box Audio Codec | Touch + BOOT | Board màn cảm ứng |

---

## 🔄 Cách Chuyển Board

### Bước 1: Mở menuconfig
```bash
source $HOME/esp/esp-idf/export.sh
idf.py menuconfig
```

### Bước 2: Chọn Board
```
Xiaozhi Assistant  --->
  Board Selection  ---> 
    (X) xiaozhi-ai-iot-vietnam-1st     # Board 1.83" TFT
    ( ) xingzhi-cube-1.54tft-wifi      # Cube 1.54"
    ( ) xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8  # LCD 2.8" Touch
```

### Bước 3: Clean build và flash
```bash
idf.py fullclean
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

---

## 📌 Cấu Hình Chi Tiết Từng Board

### 1. xiaozhi-ai-iot-vietnam-1st (Board Chính 1.83" TFT)

**GPIO Buttons:**
| Chức năng | GPIO | Chú thích |
|-----------|------|-----------|
| BOOT | GPIO_0 | Wake + Chat toggle |
| Volume UP | GPIO_39 | Tăng âm, Intercom open (2x click) |
| Volume DOWN | GPIO_40 | Giảm âm |

**GPIO Audio (ES8311 + ES7210):**
| Chức năng | GPIO |
|-----------|------|
| I2S_MCLK | GPIO_5 |
| I2S_WS | GPIO_16 |
| I2S_BCLK | GPIO_15 |
| I2S_DIN | GPIO_7 |
| I2S_DOUT | GPIO_6 |
| I2C_SDA | GPIO_12 |
| I2C_SCL | GPIO_11 |
| PA_EN | GPIO_4 |

**GPIO Display (NV3023 1.83" TFT):**
| Chức năng | GPIO |
|-----------|------|
| SDA/MOSI | GPIO_10 |
| SCL/SCK | GPIO_9 |
| DC | GPIO_8 |
| CS | GPIO_14 |
| RST | GPIO_18 |
| Backlight | GPIO_13 |

**Display Config:**
- Resolution: 284x240
- Offset: X=36, Y=0
- Swap XY: true
- Mirror X: false, Mirror Y: true

**Intercom Controls:**
| Nút | Tác dụng |
|-----|----------|
| BOOT long press (2s) | Mở Intercom UI |
| BOOT click | Di chuyển xuống |
| BOOT double click | Chọn contact |
| Volume DOWN long press | Đóng Intercom |

---

### 2. xingzhi-cube-1.54tft-wifi (Cube 1.54" TFT)

**GPIO Buttons:**
| Chức năng | GPIO | Chú thích |
|-----------|------|-----------|
| BOOT | GPIO_0 | Chọn contact / Chat toggle |
| Volume UP | GPIO_40 | Tăng âm, Intercom open (2x click), Di chuyển lên |
| Volume DOWN | GPIO_39 | Giảm âm, Di chuyển xuống |

**GPIO Audio (I2S Mic + Speaker):**
| Chức năng | GPIO |
|-----------|------|
| MIC_WS | GPIO_4 |
| MIC_SCK | GPIO_5 |
| MIC_DIN | GPIO_6 |
| SPK_DOUT | GPIO_7 |
| SPK_BCLK | GPIO_15 |
| SPK_LRCK | GPIO_16 |

**GPIO Display (GC9A01 1.54" TFT Round):**
| Chức năng | GPIO |
|-----------|------|
| SDA/MOSI | GPIO_10 |
| SCL/SCK | GPIO_9 |
| DC | GPIO_8 |
| CS | GPIO_14 |
| RST | GPIO_18 |
| Backlight | GPIO_13 |

**Display Config:**
- Resolution: 240x240
- Offset: X=0, Y=0
- Swap XY: false
- Mirror X: false, Mirror Y: false

**Intercom Controls:**
| Nút | Tác dụng |
|-----|----------|
| Volume UP × 2 (double) | Mở Intercom UI |
| Volume UP | Di chuyển lên ⬆️ |
| Volume DOWN | Di chuyển xuống ⬇️ |
| BOOT | Chọn contact ✅ |
| Volume DOWN (giữ) | Đóng Intercom |

---

### 3. xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8 (LCD 2.8" Touch)

**GPIO Buttons:**
| Chức năng | GPIO |
|-----------|------|
| BOOT | GPIO_0 |

**GPIO Display (ST7789 2.8" Touch TFT):**
| Chức năng | GPIO |
|-----------|------|
| SDA/MOSI | GPIO_10 |
| SCL/SCK | GPIO_11 |
| DC | GPIO_9 |
| CS | GPIO_14 |
| RST | GPIO_21 |
| Backlight | GPIO_8 |
| Touch SDA | GPIO_4 |
| Touch SCL | GPIO_5 |
| Touch INT | GPIO_7 |

**Display Config:**
- Resolution: 320x240
- Touch: Capacitive (FT5x06)

**Intercom Controls (Touch + BOOT):**
| Thao tác | Tác dụng |
|----------|----------|
| **BOOT giữ (2s)** | Mở Intercom UI 📱 |
| **Touch tap item** | Chọn contact đó ✅ |
| **Touch scroll** | Di chuyển lên/xuống (LVGL list tự scroll) |
| **BOOT click** | Di chuyển xuống |
| **BOOT × 2** | Chọn contact hiện tại |
| **BOOT × 5** | Reset WiFi |

> **Lưu ý**: Màn cảm ứng hỗ trợ tap trực tiếp vào contact để gọi!

---

## ⚡ Quick Commands

```bash
# === BOARD 1: xiaozhi-ai-iot-vietnam-1st (1.83" TFT) ===
# Nếu đã set trong menuconfig, chỉ cần:
idf.py build && idf.py -p /dev/cu.usbmodem* flash monitor

# === BOARD 2: xingzhi-cube-1.54tft-wifi (Cube 1.54") ===
idf.py build && idf.py -p /dev/cu.usbmodem* flash monitor

# === BOARD 3: xiaozhi-ai-iot-vietnam-es3n28p-lcd-2.8 (LCD 2.8" Touch) ===
idf.py build && idf.py -p /dev/cu.usbmodem* flash monitor
```

---

## 🔧 Lưu Ý Khi Chuyển Board

1. **Luôn `idf.py fullclean`** khi chuyển board để tránh lỗi linking
2. **Kiểm tra serial port** - có thể thay đổi khi cắm board khác
3. **Backup sdkconfig** nếu có custom config

```bash
# Backup config hiện tại
cp sdkconfig sdkconfig.backup.cube154

# Restore config cũ
cp sdkconfig.backup.vietnam1st sdkconfig
```

---

## 📝 Version History

| Date | Change |
|------|--------|
| 2026-02-04 | Thêm Intercom UI navigation controls cho mỗi board |
| 2026-02-02 | Khởi tạo tài liệu |

---

**Xiaozhi ESP32 Vietnam** - *AI Voice Assistant on the Edge*
