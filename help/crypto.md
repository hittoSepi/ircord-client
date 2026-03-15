# Encryption & Trust

IRCord uses **end-to-end encryption** for all messages. The server never sees plaintext.

## How It Works
- Each user generates an **Ed25519 identity key** on first launch
- Direct messages use the **Signal Protocol** (double ratchet)
- Channel messages use **Sender Keys** for group encryption

## Trusting Users
Use `/trust <user>` to mark a user's identity key as trusted. This verifies you are communicating with the right person.

## Key Management
Your identity key is stored locally. Use `--clear-creds` to reset it. Log back in with the same username and passphrase to re-sync your server-side keys, then re-establish sessions with your contacts.
