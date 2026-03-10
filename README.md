# IrssiCord Client

End-to-end encrypted chat and voice for friend groups. Terminal UI with irssi-style aesthetics.

## Requirements

- Windows 10+ or Linux (x64)
- A server address and port from whoever runs your IrssiCord server

## First Run

1. Copy `client.toml.example` to `client.toml` (in the same folder as the executable, or see [Config Location](#config-location))
2. Edit `client.toml` and set the server address:
   ```toml
   [server]
   host = "your.server.address"
   port = 6667
   ```
3. Run `ircord-client.exe` (Windows) or `./ircord-client` (Linux)
4. If no username is set in the config, you will be prompted to enter one — it is saved automatically for future runs
5. Enter your passphrase when prompted (new users: choose a passphrase; returning users: enter the same one you used before)

> **Keep your passphrase safe.** It encrypts your identity key. Losing it means losing access to your account.

## Config Location

If no `--config` flag is passed, the client looks for config in the platform default directory:

| Platform | Path |
|----------|------|
| Windows  | `%APPDATA%\ircord\client.toml` |
| Linux    | `~/.config/ircord/client.toml` |

You can also pass a custom path:
```
ircord-client --config /path/to/client.toml
```

## client.toml Reference

```toml
[server]
host = "your.server.address"   # Server hostname or IP
port = 6667                    # Server port
# cert_pin = ""                # SHA-256 certificate fingerprint (set automatically on first connect)

[identity]
user_id = "Alice"              # Your username (set automatically on first run if left blank)

[ui]
theme = "tokyo-night"          # UI color theme
timestamp_format = "%H:%M"     # Message timestamp format
max_messages = 1000            # Messages kept in memory per channel

[voice]
input_device  = ""             # Microphone (empty = system default)
output_device = ""             # Speaker (empty = system default)
opus_bitrate  = 64000          # Voice quality in bits/s
frame_ms      = 20             # Audio frame size in milliseconds

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
| `Tab` | Autocomplete username / channel |
| `PgUp` / `PgDn` | Scroll message history |
| `Alt+1..9` | Switch to channel by number |
| `/join #channel` | Join a channel |
| `/part` | Leave current channel |
| `/msg <user> <text>` | Send a private message |
| `/call <user>` | Start a voice call |
| `/hangup` | End current call |
| `/quit` | Exit the client |

## Security

- **End-to-end encrypted** — the server never sees message content
- **Signal Protocol** — X3DH key agreement + Double Ratchet for forward secrecy
- **Identity key** — stored locally, encrypted at rest with your passphrase
- **Voice** — WebRTC P2P (audio does not pass through the server)

## Command Line Options

```
ircord-client [OPTIONS]

Options:
  --config <path>   Path to client.toml (default: platform config dir)
  --user   <id>     Override username from config
  --help            Show this help
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
