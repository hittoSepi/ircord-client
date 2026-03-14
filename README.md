# IRCord Client

End-to-end encrypted chat and voice client for friend groups. Terminal UI with irssi-style aesthetics, mouse support, and full Signal Protocol encryption.

## Features

- 🔒 **End-to-end encryption** via Signal Protocol (X3DH + Double Ratchet)
- 👥 **Group chats** with Sender Keys for efficient multi-party encryption
- 🖱️ **Mouse support** — click channels, select text, resize panels
- 🎙️ **Voice rooms** — WebRTC P2P voice calls
- 📋 **Clipboard integration** — `Ctrl+V` paste support
- 🔗 **Link previews** — automatic URL fetching
- 🎨 **Themes** — Tokyo Night and other color schemes
- ⌨️ **IRC-style commands** — `/join`, `/part`, `/msg`, `/call`
- 🔐 **Certificate pinning** — trust-on-first-use (TOFU)

## Requirements

- Windows 10+ or Linux (x64)
- A server address and port from whoever runs your IRCord server

## First Run

1. Copy `client.toml.example` to `client.toml` (in the same folder as the executable, or see [Config Location](#config-location))
2. Edit `client.toml` and set the server address:
   ```toml
   [server]
   host = "your.server.address"
   port = 6697
   ```
3. Run `ircord-client.exe` (Windows) or `./ircord-client` (Linux)
4. If no username is set in the config, you will be prompted to enter one — it is saved automatically for future runs
5. Enter your passphrase when prompted (new users: choose a passphrase; returning users: enter the same one you used before)

> **Keep your passphrase safe.** It encrypts your identity key. Losing it means losing access to your account.

## Quick Connect

Launch directly with an `ircord://` quick-connect URL:
```bash
ircord-client ircord://chat.example.com:6697
```

Or from a web browser when clicking an IRCord server link on the landing page.

## Config Location

If no `--config` flag is passed, the client looks for config in the platform default directory:

| Platform | Path |
|----------|------|
| Windows  | `%APPDATA%\ircord\client.toml` |
| Linux    | `~/.config/ircord/client.toml` |

You can also pass a custom path:
```bash
ircord-client --config /path/to/client.toml
```

## client.toml Reference

```toml
[server]
host = "your.server.address"   # Server hostname or IP
port = 6697                    # Server port
# cert_pin = ""                # SHA-256 certificate fingerprint (set automatically on first connect)

[identity]
user_id = "Alice"              # Your username (set automatically on first run if left blank)

[ui]
theme = "tokyo-night"          # UI color theme
timestamp_format = "%H:%M"     # Message timestamp format
max_messages = 1000            # Messages kept in memory per channel
show_user_list = true          # Show right-side user list panel
user_list_width = 20           # Width of user list in characters
user_list_collapsed = false    # Whether user list is collapsed

[voice]
input_device  = ""             # Microphone (empty = system default)
output_device = ""             # Speaker (empty = system default)
opus_bitrate  = 64000          # Voice quality in bits/s
frame_ms      = 20             # Audio frame size in milliseconds
ice_servers   = [              # Empty = built-in Google STUN fallback
  "stun:turn.example.com:3478",
  "turn:turn.example.com:3478?transport=udp",
  "turns:turn.example.com:5349?transport=tcp",
]
turn_username = "ircord"       # Optional TURN credential
turn_password = "secret"       # Optional TURN credential

[preview]
enabled       = true           # Enable link previews
fetch_timeout = 5              # Fetch timeout in seconds
max_cache     = 200            # Number of previews to cache

[tls]
verify_peer = true             # Verify server TLS certificate (keep true in production)
```

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Enter` | Send message |
| `Ctrl+V` | Paste clipboard text into the input line |
| `Tab` | Autocomplete username / channel |
| `PgUp` / `PgDn` | Scroll message history |
| `F1` | Toggle voice mode (`PTT` / `VOX`) |
| `F2` | Toggle the right-side user list |
| `F12` | Open settings |
| `Alt+1..9` | Switch to channel by number |
| `Alt+Left` / `Alt+Right` | Cycle channels |
| `/join #channel` | Join a channel |
| `/part` | Leave current channel |
| `/msg <user> <text>` | Send a private message |
| `/call <user>` | Start a voice call |
| `/hangup` | End current call |
| `/quit` | Exit the client |

## Mouse Support

| Action | Description |
|--------|-------------|
| Click channel tab | Switch active channel |
| Click user list item | Mention user in input |
| Right-click user | Context menu (if available) |
| Drag user list border | Resize panel width |
| Click + drag message | Select text |
| Double-click message | Select entire message |
| Triple-click message | Select message with sender, auto-copies to clipboard |
| Mouse wheel | Scroll message history |

## Command Line Options

```
ircord-client [OPTIONS] [ircord://host:port]

Options:
  --config <path>   Path to client.toml (default: platform config dir)
  --user   <id>     Override username from config
  --clear-creds     Clear remembered credentials and local encrypted identity
  --help            Show this help
```

## Security

- **End-to-end encrypted** — the server never sees message content
- **Signal Protocol** — X3DH key agreement + Double Ratchet for forward secrecy
- **Identity key** — stored locally, encrypted at rest with your passphrase
- **Voice** — WebRTC P2P (audio does not pass through the server)
- **Certificate pinning** — automatic trust-on-first-use for server certificates

## Building from Source

## Voice / ICE Setup

If `[voice].ice_servers` is empty, the client falls back to public Google STUN servers. That is acceptable for quick testing, but it is not reliable enough for real-world NAT traversal.

For a server you run yourself, configure one STUN entry and at least one TURN entry:

```toml
[voice]
ice_servers = [
  "stun:turn.example.com:3478",
  "turn:turn.example.com:3478?transport=udp",
  "turns:turn.example.com:5349?transport=tcp",
]
turn_username = "ircord"
turn_password = "replace-with-your-turn-password"
```

When `ice_servers` is defined, the client uses only that list and does not append the built-in Google STUN fallback. If `turn_username` is set, the same TURN credentials are applied to all configured ICE servers.

### Prerequisites

- CMake 3.20+
- C++20 compiler (MSVC 2022+, GCC 11+, Clang 13+)
- vcpkg

### Build

```bash
cd ircord-client
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

## Troubleshooting

**"Crypto init failed (wrong passphrase?)"**
You entered the wrong passphrase. Try again — there is no reset; the key is derived from your passphrase.

**Connection refused / timeout**
Check that `host` and `port` in `client.toml` match the server, and that the server is running.

**TLS errors with `verify_peer = true`**
If the server uses a self-signed certificate, set `verify_peer = false` in `[tls]` (only do this on a trusted network).

**Voice not working**
Make sure `input_device` and `output_device` are empty (system default) or set to a valid device name. Check that the server is reachable for signaling.

## Related Projects

- [ircord-server](../ircord-server) — C++ relay server
- [ircord-android](../ircord-android) — Android mobile client
- [ircord-plugin](../ircord-plugin) — Plugin system

## License

MIT License — see LICENSE file for details.
