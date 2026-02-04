#ifndef INTERCOM_CONTACTS_UI_H
#define INTERCOM_CONTACTS_UI_H

#include <string>
#include <vector>
#include <functional>
#include "display.h"  // For HAVE_LVGL definition

#ifdef HAVE_LVGL
#include <lvgl.h>
#endif

// Contact data structure
struct IntercomContact {
    std::string id;
    std::string name;
    std::string mac;
    std::string owner;
    bool is_online = false;
    bool is_own_device = false;
};

// Callback type when contact is selected
using IntercomContactSelectedCallback = std::function<void(const IntercomContact& contact)>;
using IntercomContactsCancelCallback = std::function<void()>;

class IntercomContactsUI {
public:
    IntercomContactsUI();
    ~IntercomContactsUI();

    // Show the contacts UI
    void Show();
    
    // Hide the contacts UI
    void Hide();
    
    // Check if UI is visible
    bool IsVisible() const { return is_visible_; }
    
    // Set contacts list
    void SetContacts(const std::vector<IntercomContact>& contacts);
    
    // Navigation
    void MoveUp();
    void MoveDown();
    void Select();  // Select current item
    void Cancel();  // Cancel and close UI
    
    // Callbacks
    void OnContactSelected(IntercomContactSelectedCallback callback) { on_selected_ = callback; }
    void OnCancel(IntercomContactsCancelCallback callback) { on_cancel_ = callback; }
    
    // Get current selected index
    int GetSelectedIndex() const { return selected_index_; }
    
    // Get contacts count
    size_t GetContactsCount() const { return contacts_.size(); }

private:
    bool is_visible_ = false;
    int selected_index_ = 0;
    std::vector<IntercomContact> contacts_;
    
    IntercomContactSelectedCallback on_selected_;
    IntercomContactsCancelCallback on_cancel_;
    
#ifdef HAVE_LVGL
    lv_obj_t* container_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* list_ = nullptr;
    std::vector<lv_obj_t*> list_items_;
    
    void CreateUI();
    void UpdateUI();
    void UpdateSelection();
    static void OnItemClicked(lv_event_t* e);
#endif
};

#endif // INTERCOM_CONTACTS_UI_H
