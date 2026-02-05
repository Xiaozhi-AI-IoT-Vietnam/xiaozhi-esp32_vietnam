#include "intercom_contacts_ui.h"
#include "display.h"  // For HAVE_LVGL definition
#include "board.h"
#include <esp_log.h>

static const char* TAG = "IntercomUI";

IntercomContactsUI::IntercomContactsUI() {
}

IntercomContactsUI::~IntercomContactsUI() {
    Hide();
}

void IntercomContactsUI::SetContacts(const std::vector<IntercomContact>& contacts) {
    contacts_ = contacts;
    selected_index_ = 0;
#ifdef HAVE_LVGL
    if (is_visible_) {
        UpdateUI();
    }
#endif
}

void IntercomContactsUI::MoveUp() {
    if (contacts_.empty()) return;
    
    selected_index_--;
    if (selected_index_ < 0) {
        selected_index_ = contacts_.size() - 1;
    }
    ESP_LOGI(TAG, "Move up: selected=%d/%d", selected_index_, (int)contacts_.size());
    
#ifdef HAVE_LVGL
    auto display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);
    UpdateSelection();
    if (list_) {
        lv_obj_invalidate(list_);
    }
#endif
}

void IntercomContactsUI::MoveDown() {
    if (contacts_.empty()) return;
    
    selected_index_++;
    if (selected_index_ >= (int)contacts_.size()) {
        selected_index_ = 0;
    }
    ESP_LOGI(TAG, "Move down: selected=%d/%d", selected_index_, (int)contacts_.size());
    
#ifdef HAVE_LVGL
    auto display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);
    UpdateSelection();
    if (list_) {
        lv_obj_invalidate(list_);
    }
#endif
}

void IntercomContactsUI::Select() {
    ESP_LOGI(TAG, "Select() called: contacts=%d, selected=%d, visible=%d", 
             (int)contacts_.size(), selected_index_, is_visible_);
    
    if (contacts_.empty() || selected_index_ < 0 || selected_index_ >= (int)contacts_.size()) {
        ESP_LOGW(TAG, "Select() aborted: invalid state");
        return;
    }
    
    ESP_LOGI(TAG, "Selected contact: %s", contacts_[selected_index_].name.c_str());
    
    if (on_selected_) {
        on_selected_(contacts_[selected_index_]);
    } else {
        ESP_LOGW(TAG, "on_selected_ callback is null!");
    }
}

void IntercomContactsUI::Cancel() {
    ESP_LOGI(TAG, "Cancel");
    Hide();
    if (on_cancel_) {
        on_cancel_();
    }
}

void IntercomContactsUI::Show() {
#ifdef HAVE_LVGL
    ESP_LOGI(TAG, "Show Intercom UI");
    if (!is_visible_) {
        is_visible_ = true;
        CreateUI();
    }
#endif
}

void IntercomContactsUI::Hide() {
#ifdef HAVE_LVGL
    ESP_LOGI(TAG, "Hide Intercom UI");
    is_visible_ = false;
    if (container_) {
        // Must use DisplayLockGuard for LVGL thread safety
        auto display = Board::GetInstance().GetDisplay();
        if (display) {
            DisplayLockGuard lock(display);
            lv_obj_del(container_);
        } else {
            lv_obj_del(container_);
        }
        container_ = nullptr;
        title_label_ = nullptr;
        list_ = nullptr;
        list_items_.clear();
    }
#endif
}

#ifdef HAVE_LVGL

void IntercomContactsUI::CreateUI() {
    auto display = Board::GetInstance().GetDisplay();
    if (!display) {
        ESP_LOGE(TAG, "Display is null!");
        return;
    }
    
    ESP_LOGI(TAG, "Creating Intercom UI...");
    
    // Create fullscreen container
    container_ = lv_obj_create(lv_scr_act());
    if (!container_) {
        ESP_LOGE(TAG, "Failed to create container!");
        return;
    }
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 8, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    title_label_ = lv_label_create(container_);
    lv_label_set_text(title_label_, "Intercom");
    lv_obj_set_style_text_color(title_label_, lv_color_hex(0xffffff), 0);
    lv_obj_align(title_label_, LV_ALIGN_TOP_MID, 0, 5);
    
    // Create list
    list_ = lv_obj_create(container_);
    if (!list_) {
        ESP_LOGE(TAG, "Failed to create list!");
        return;
    }
    lv_obj_set_size(list_, LV_PCT(100), LV_PCT(100) - 40);
    lv_obj_align(list_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(list_, lv_color_hex(0x2d3436), 0);
    lv_obj_set_style_bg_opa(list_, LV_OPA_50, 0);
    lv_obj_set_style_border_width(list_, 0, 0);
    lv_obj_set_style_pad_all(list_, 4, 0);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list_, 6, 0);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    
    // Add debug touch handler to list
    lv_obj_add_event_cb(list_, [](lv_event_t* e) {
        lv_event_code_t code = lv_event_get_code(e);
        ESP_LOGI("TouchDebug", "List event: %d", (int)code);
    }, LV_EVENT_ALL, nullptr);
    
    ESP_LOGI(TAG, "Created Intercom UI successfully");
    
    UpdateUI();
}

void IntercomContactsUI::UpdateUI() {
    if (!list_) {
        ESP_LOGE(TAG, "List is null in UpdateUI!");
        return;
    }
    
    ESP_LOGI(TAG, "UpdateUI with %d contacts", (int)contacts_.size());
    
    // Clear existing items
    lv_obj_clean(list_);
    list_items_.clear();
    
    if (contacts_.empty()) {
        // Show empty message
        lv_obj_t* empty_label = lv_label_create(list_);
        lv_label_set_text(empty_label, "No contacts");
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0x888888), 0);
        return;
    }
    
    // Create simple items for each contact
    for (size_t i = 0; i < contacts_.size(); i++) {
        auto& contact = contacts_[i];
        
        // Simple container for each item
        lv_obj_t* item = lv_obj_create(list_);
        lv_obj_set_size(item, LV_PCT(95), 44);
        lv_obj_set_style_radius(item, 8, 0);
        lv_obj_set_style_pad_all(item, 8, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        
        // Background color
        bool is_selected = (i == (size_t)selected_index_);
        lv_obj_set_style_bg_color(item, 
            is_selected ? lv_color_hex(0x4a69bd) : lv_color_hex(0x3d4f5f), 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x5a79cd), LV_STATE_PRESSED);
        
        // Set user data and click handler
        lv_obj_set_user_data(item, (void*)(intptr_t)i);
        lv_obj_add_event_cb(item, OnItemClicked, LV_EVENT_CLICKED, this);
        
        // Label inside item
        lv_obj_t* name_label = lv_label_create(item);
        std::string display_text;
        if (contact.is_online) {
            display_text = "[ON] " + contact.name;
        } else {
            display_text = "[--] " + contact.name;
        }
        lv_label_set_text(name_label, display_text.c_str());
        lv_obj_set_style_text_color(name_label, lv_color_hex(0xffffff), 0);
        lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 0, 0);
        
        // Store item reference
        list_items_.push_back(item);
        
        ESP_LOGI(TAG, "Created item %d: %s", (int)i, contact.name.c_str());
    }
}

void IntercomContactsUI::UpdateSelection() {
    if (!list_ || list_items_.empty()) return;
    
    for (size_t i = 0; i < list_items_.size(); i++) {
        bool is_selected = (i == (size_t)selected_index_);
        lv_obj_set_style_bg_color(list_items_[i], 
            is_selected ? lv_color_hex(0x4a69bd) : lv_color_hex(0x3d4f5f), 0);
    }
    
    // Scroll to make selected item visible
    if (selected_index_ >= 0 && selected_index_ < (int)list_items_.size()) {
        lv_obj_scroll_to_view(list_items_[selected_index_], LV_ANIM_ON);
    }
}

void IntercomContactsUI::OnItemClicked(lv_event_t* e) {
    IntercomContactsUI* ui = (IntercomContactsUI*)lv_event_get_user_data(e);
    lv_obj_t* item = (lv_obj_t*)lv_event_get_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(item);
    
    ESP_LOGI(TAG, "Item clicked: %d", index);
    
    if (ui && index >= 0 && index < (int)ui->contacts_.size()) {
        ui->selected_index_ = index;
        ui->Select();
    }
}

#endif // HAVE_LVGL
