---
name: LVGL Display Development
description: Skill for LVGL (Light and Versatile Graphics Library) UI development on ESP32. Covers widgets, styles, layouts, animations, and display drivers. Use when working with LCD displays, UI components, or graphics.
triggers:
  - lvgl
  - display
  - lcd
  - ui
  - widget
  - lv_
  - SetChatMessage
  - ShowNotification
  - SetStatus
  - SetEmotion
---

# LVGL Display Development Skill

## Overview
LVGL is a free and open-source graphics library for embedded systems with easy-to-use graphical elements.

## Version
LVGL 9.x (ESP Component Registry: lvgl/lvgl ^9.4.0)

## Installation (ESP-IDF)
```cmake
# CMakeLists.txt
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl esp_lcd
)
```

## Core Concepts

### 1. Display Initialization
```cpp
#include "lvgl.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"

// Initialize LVGL
lv_init();

// Create display driver
static lv_display_t *disp = lv_display_create(width, height);
lv_display_set_flush_cb(disp, my_flush_cb);

// Flush callback
static void my_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    // Send to panel
    esp_lcd_panel_draw_bitmap(panel, x1, y1, x2+1, y2+1, px_map);
    lv_display_flush_ready(disp);
}
```

### 2. Basic Widgets
```cpp
// Label
lv_obj_t *label = lv_label_create(lv_screen_active());
lv_label_set_text(label, "Hello World");
lv_obj_center(label);

// Button
lv_obj_t *btn = lv_button_create(lv_screen_active());
lv_obj_set_size(btn, 100, 40);
lv_obj_center(btn);

// Image
lv_obj_t *img = lv_image_create(lv_screen_active());
lv_image_set_src(img, &my_image);
```

### 3. Styling (v9 syntax)
```cpp
static lv_style_t style;
lv_style_init(&style);
lv_style_set_bg_color(&style, lv_color_hex(0x2196F3));
lv_style_set_radius(&style, 10);
lv_style_set_text_color(&style, lv_color_white());
lv_style_set_text_font(&style, &lv_font_montserrat_16);

lv_obj_add_style(obj, &style, 0);
```

### 4. Layouts
```cpp
// Flex layout
lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

// Grid layout
static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
lv_obj_set_grid_dsc_array(parent, col_dsc, row_dsc);
```

### 5. Events
```cpp
static void btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Button clicked!");
    }
}

lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
```

### 6. Animations
```cpp
lv_anim_t a;
lv_anim_init(&a);
lv_anim_set_var(&a, obj);
lv_anim_set_values(&a, 0, 100);
lv_anim_set_duration(&a, 500);
lv_anim_set_exec_cb(&a, [](void *var, int32_t v) {
    lv_obj_set_x((lv_obj_t*)var, v);
});
lv_anim_start(&a);
```

## Xiaozhi Display Pattern

### Display Interface
```cpp
class Display {
public:
    virtual void SetStatus(const char* status) = 0;
    virtual void SetChatMessage(const char* role, const char* message) = 0;
    virtual void ShowNotification(const char* text) = 0;
    virtual void SetEmotion(const char* emotion) = 0;
    virtual void ClearQRCode() = 0;
};
```

### Usage Example
```cpp
auto display = Board::GetInstance().GetDisplay();
display->SetStatus(Lang::Strings::STANDBY);
display->SetChatMessage("assistant", "Hello!");
display->SetEmotion("happy");
display->ShowNotification("Connected to WiFi");
```

## Supported Display Drivers (ESP32)
- ILI9341 (2.8" TFT)
- ST7789 (1.3", 1.54", 2.0")
- SSD1306 (OLED)
- GC9A01 (Round display)

## Memory Optimization
```cpp
// Use PSRAM for frame buffer
LV_ATTRIBUTE_MEM_ALIGN static uint8_t buf1[DISP_BUF_SIZE];
lv_display_set_buffers(disp, buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
```

## Resources
- LVGL Docs: https://docs.lvgl.io/9.4/
- ESP32 LVGL Port: https://components.espressif.com/components/lvgl/lvgl
- LVGL GitHub: https://github.com/lvgl/lvgl
