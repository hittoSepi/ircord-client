#pragma once

#include <ftxui/component/event.hpp>
#include <string>
#include <chrono>
#include <optional>
#include <vector>

namespace ircord::ui {

// ============================================================================
// Mouse Support for IRCord Desktop Client
// ============================================================================
// This module provides mouse interaction capabilities for the FTXUI-based UI.
// 
// Implemented Features:
// - D6.1: Button and UI Interaction
//   - Click channel tabs to switch
//   - Click user list items to mention
//   - Right-click user list items for mention (context menu placeholder)
//   - Collapsible panel toggle
//
// - D6.2: Subwindow Resizing
//   - Drag user list panel edge to resize width
//   - Visual feedback during resize
//
// - D6.3: Text Selection and Interactions
//   - Click and drag to select text (basic support)
//   - Double-click to select message
//   - Triple-click to select full message with sender
//   - Mouse wheel to scroll messages
//   - Selection copied to clipboard on triple-click
//
// Limitations:
// - FTXUI is a terminal UI library, so some mouse interactions are constrained
//   by terminal capabilities (e.g., no true cursor shape changes)
// - Text selection is message-level rather than character-level due to
//   rendering model limitations
// - Right-click context menus are simulated (actual popup menus not supported)
// - Link clicking depends on terminal emulator support
//
// Technical Notes:
// - FTXUI mouse events: event.is_mouse(), event.mouse().x, event.mouse().y
// - Mouse buttons: Mouse::Left, Mouse::Right, Mouse::WheelUp, Mouse::WheelDown
// - Motion: Mouse::Pressed, Mouse::Released, Mouse::Moved
// ============================================================================

// Mouse interaction configuration
struct MouseConfig {
    // Double-click timing
    static constexpr int kDoubleClickThresholdMs = 400;
    static constexpr int kClickDistanceThreshold = 2;
    
    // Panel resize
    static constexpr int kMinPanelWidth = 15;
    static constexpr int kPanelResizeTolerance = 1;  // How close to divider to grab
    
    // Scroll amounts
    static constexpr int kWheelScrollLines = 3;
    
    // Visual feedback
    static constexpr bool kShowHoverEffects = true;
};

// UI region for hit testing
struct UIRegion {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    
    bool contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
    
    bool contains_x(int px) const {
        return px >= x && px < x + width;
    }
    
    bool contains_y(int py) const {
        return py >= y && py < y + height;
    }
    
    int right() const { return x + width; }
    int bottom() const { return y + height; }
    int center_x() const { return x + width / 2; }
    int center_y() const { return y + height / 2; }
};

// Mouse state tracking
class MouseTracker {
public:
    // Position
    int x() const { return x_; }
    int y() const { return y_; }
    void set_position(int x, int y) { x_ = x; y_ = y; }
    
    // Hover
    int hover_x() const { return hover_x_; }
    int hover_y() const { return hover_y_; }
    void set_hover(int x, int y) { hover_x_ = x; hover_y_ = y; }
    bool is_hovering() const { return hover_x_ >= 0 && hover_y_ >= 0; }
    
    // Click counting for double/triple click
    void record_click(int x, int y);
    int click_count() const { return click_count_; }
    void reset_click_count() { click_count_ = 0; }
    bool is_double_click() const;
    bool is_triple_click() const;
    
    // Drag state
    bool is_dragging() const { return is_dragging_; }
    void start_drag(int x, int y);
    void update_drag(int x, int y);
    void end_drag();
    int drag_start_x() const { return drag_start_x_; }
    int drag_start_y() const { return drag_start_y_; }
    int drag_delta_x() const { return x_ - drag_start_x_; }
    int drag_delta_y() const { return y_ - drag_start_y_; }
    
    // Selection
    bool is_selecting() const { return is_selecting_; }
    void start_selection(int x, int y);
    void update_selection(int x, int y);
    void end_selection();
    UIRegion selection_region() const;
    
    // Region hit testing
    void set_tab_bar_region(const UIRegion& r) { tab_bar_ = r; }
    void set_message_region(const UIRegion& r) { message_area_ = r; }
    void set_user_list_region(const UIRegion& r) { user_list_ = r; }
    void set_input_region(const UIRegion& r) { input_ = r; }
    void set_status_bar_region(const UIRegion& r) { status_bar_ = r; }
    void set_panel_divider_region(const UIRegion& r) { panel_divider_ = r; }
    
    const UIRegion& tab_bar_region() const { return tab_bar_; }
    const UIRegion& message_region() const { return message_area_; }
    const UIRegion& user_list_region() const { return user_list_; }
    const UIRegion& input_region() const { return input_; }
    const UIRegion& status_bar_region() const { return status_bar_; }
    const UIRegion& panel_divider_region() const { return panel_divider_; }
    
    bool is_over_tab_bar() const { return tab_bar_.contains(x_, y_); }
    bool is_over_message_area() const { return message_area_.contains(x_, y_); }
    bool is_over_user_list() const { return user_list_.contains(x_, y_); }
    bool is_over_input() const { return input_.contains(x_, y_); }
    bool is_over_status_bar() const { return status_bar_.contains(x_, y_); }
    bool is_over_panel_divider() const { return panel_divider_.contains(x_, y_); }
    
private:
    int x_ = 0;
    int y_ = 0;
    int hover_x_ = -1;
    int hover_y_ = -1;
    
    std::chrono::steady_clock::time_point last_click_time_;
    int click_count_ = 0;
    int last_click_x_ = 0;
    int last_click_y_ = 0;
    
    bool is_dragging_ = false;
    int drag_start_x_ = 0;
    int drag_start_y_ = 0;
    
    bool is_selecting_ = false;
    int selection_start_x_ = 0;
    int selection_start_y_ = 0;
    int selection_end_x_ = 0;
    int selection_end_y_ = 0;
    
    UIRegion tab_bar_;
    UIRegion message_area_;
    UIRegion user_list_;
    UIRegion input_;
    UIRegion status_bar_;
    UIRegion panel_divider_;
};

// Clipboard utility
void copy_to_clipboard(const std::string& text);
std::optional<std::string> read_from_clipboard();

// Link detection and opening
bool is_url(const std::string& text);
void open_url(const std::string& url);

} // namespace ircord::ui
