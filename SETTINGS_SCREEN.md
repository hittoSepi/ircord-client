# Settings Screen Implementation

## Overview
This document describes the settings screen implementation for the IRCord desktop client.

## Files Created/Modified

### New Files
1. `src/ui/settings_screen.hpp` - Settings screen header with class definition
2. `src/ui/settings_screen.cpp` - Settings screen implementation
3. `config/client.toml.example` - Example configuration file with all settings

### Modified Files
1. `src/config.hpp` - Added new config structures:
   - `ConnectionConfig` - Auto-reconnect, timeout settings
   - `NotificationConfig` - Desktop notifications, sound alerts, mention keywords
   - Extended `UiConfig` with font_scale, show_timestamps, show_user_colors

2. `src/config.cpp` - Updated to load/save all new config sections

3. `src/app.hpp` - Added settings integration methods:
   - `open_settings()` - Opens the settings screen
   - `get_public_key_hex()` - Gets public key for display
   - `on_theme_changed()` - Applies theme changes immediately
   - `save_current_config()` - Saves config to disk

4. `src/app.cpp` - Integrated settings screen:
   - `/settings` command to open settings
   - F12 key shortcut for settings access
   - Theme change callback
   - Logout handling

5. `src/ui/ui_manager.hpp` - Added `OpenSettingsFn` callback type

6. `src/ui/ui_manager.cpp` - Added F12 key handler

7. `src/input/command_parser.cpp` - Added `/settings` to known commands

8. `CMakeLists.txt` - Added `settings_screen.cpp` to build

## Settings Categories

### Appearance
- Theme selection (tokyo-night, dark, light, dracula, nord, solarized-dark, solarized-light)
- Font scale (percentage)
- Show timestamps toggle
- Show user colors toggle
- Timestamp format
- Max messages to display

### Connection
- Auto-reconnect toggle
- Reconnect delay (seconds)
- Connection timeout (seconds)
- TLS certificate verification toggle
- Certificate PIN for custom certs

### Notifications
- Desktop notifications toggle
- Sound alerts toggle
- Notify on mentions toggle
- Notify on direct messages toggle
- Custom mention keywords (comma-separated)

### Account
- Change nickname
- View public key (read-only display)
- Import/Export settings
- Logout button

## Navigation

### Accessing Settings
1. Type `/settings` in the chat input
2. Press F12 key from anywhere in the app

### Within Settings
- **F1**: Jump to Appearance tab
- **F2**: Jump to Connection tab
- **F3**: Jump to Notifications tab
- **F4**: Jump to Account tab
- **Esc** or **Q**: Cancel and return to chat

### Sidebar
Category buttons on the left allow clicking to switch between settings categories.

## Settings Persistence

Settings are stored in TOML format at:
- Windows: `%APPDATA%\ircord\config.toml`
- Linux: `~/.config/ircord/config.toml`

### Save Behavior
- Clicking **SAVE** saves all settings and returns to chat
- Clicking **CANCEL** discards changes and returns to chat
- Clicking **LOGOUT** saves settings and exits the application
- Theme changes can be applied immediately (via callback)

### Import/Export
Settings can be exported to `settings_backup.toml` in the config directory and imported later. This is useful for:
- Backing up preferences
- Migrating settings between devices
- Sharing configurations

## Technical Details

### UI Framework
- Built with FTXUI (same as login screen)
- Uses FTXUI's `Container::Vertical`, `Renderer`, `CatchEvent` patterns
- Sidebar navigation with category buttons
- Form elements: Input, Checkbox, Button

### Config Structure
The config is stored as a TOML file with the following sections:
```toml
[server]
[identity]
[ui]
[voice]
[preview]
[tls]
[connection]
[notifications]
```

### Theme System
- Themes are applied immediately when changed
- Default theme is "tokyo-night"
- Color scheme is defined in `color_scheme.hpp`

## Future Enhancements

Potential improvements for future versions:
1. Theme preview before applying
2. Live font scaling
3. Sound preview for notification sounds
4. Network proxy settings
5. Keyboard shortcut customization
6. Plugin/extension settings
7. Message filtering rules
8. Auto-away settings
