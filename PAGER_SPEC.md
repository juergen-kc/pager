# Pager — v1 Spec

**A physical Claude Code remote for your desk.**

A small touchscreen companion that gives you ambient awareness of Claude Code and Claude Cowork sessions, and lets you approve or deny tool-use prompts without context-switching away from your main work.

---

## 1. What it is, in one paragraph

Pager sits next to your keyboard. It shows you at a glance how many Claude sessions are running, which are waiting on you, today's token usage, and the last few things Claude did. When a permission prompt blocks a session, the screen takes over with the full tool call, and you approve or deny with a tap. It talks to the Claude desktop app over BLE using the documented Hardware Buddy protocol (Nordic UART Service + newline-delimited JSON). It is not a pet; it is a tool.

---

## 2. Scope

### In scope (v1)
- BLE NUS peripheral advertising as `Claude-Pager-XXXX` (last four bytes of MAC).
- LE Secure Connections bonding with DisplayOnly IO capability; passkey rendered on-screen.
- Newline-delimited JSON parser with MTU-boundary reassembly.
- Heartbeat snapshot rendering (counters, msg, entries, tokens).
- Permission prompt takeover + approve/deny round-trip.
- Turn event capture into an in-RAM ring buffer for a Recent view.
- Supported commands: `status`, `name`, `owner`, `unpair`.
- Time sync (set RTC from desktop-supplied epoch + TZ offset).
- Four UI modes: Glance, Approval, Recent, Settings.
- Audio chime on approval arrival (configurable; off by default).
- Persisted settings in NVS (device name, owner, chime on/off, bond data).

### Deferred (v2+)
- Folder push transport (`char_begin`/`file`/`chunk`/`file_end`/`char_end`) — respond with `ack:false` so the desktop gives up cleanly.
- GIF character packs and pet animations.
- Battery UI (CoreS3 SE has no battery).
- IMU-driven interactions — tilt-to-wake, shake, face-down-to-nap (CoreS3 SE has no IMU).
- Gamification — level-up, approval velocity stats beyond raw counters.
- Hardware keyboard input for future device→desktop commands.
- Multi-device or multi-user operation.

### Explicit non-goals
- **No on-device allowlist auto-approval.** `prompt.hint` is model output; pattern-matching rules on the device are a security regression. Allowlists belong in desktop settings, not here.
- **No voice approval.** Mic is present but voice control in a shared workspace is a footgun.
- **No session-attributed views.** The protocol doesn't distinguish which session a transcript entry came from, so we don't fake it.
- **Pager is not a pet.** Approval and ambient awareness are the product. Personality animations are not.

---

## 3. Hardware

**Target:** M5Stack CoreS3 SE (SKU K128-SE).

| Component            | Detail                                                  |
|----------------------|---------------------------------------------------------|
| MCU                  | ESP32-S3, dual-core Xtensa LX7 @ 240 MHz                |
| RAM / PSRAM / Flash  | 512 KB SRAM / 8 MB PSRAM / 16 MB Flash                  |
| Display              | 2.0" IPS capacitive touch, 320×240 (ILI9342C + GT6336U) |
| Audio                | 1W speaker via AW88298, dual mics via ES7210            |
| Storage              | microSD slot                                            |
| RTC                  | BM8563                                                  |
| PMU                  | AXP2101 (no battery on SE; USB-C powered)               |
| Expansion            | Grove port + M-Bus bottom header                        |

**Power:** USB-C tethered. No battery operation in v1; if an IMU module is added later via Grove, battery behavior is not a v1 concern.

---

## 4. Software stack

| Layer         | Choice                                  | Why                                                                 |
|---------------|-----------------------------------------|---------------------------------------------------------------------|
| Build system  | PlatformIO                              | Matches upstream repo conventions and M5Stack docs                  |
| Board target  | `m5stack-cores3`                        | Official PlatformIO board definition                                |
| HAL           | M5Unified                               | Single API across M5 board family; future-proofs hardware swaps     |
| BLE stack     | NimBLE-Arduino                          | Mature LE Secure Connections bonding on ESP32-S3                    |
| JSON          | ArduinoJson 7                           | Protocol messages fit comfortably in a 2 KB `JsonDocument`          |
| UI            | LVGL 9.x                                | First-class M5Unified integration, production-grade widget library  |
| Persistence   | ESP32 NVS (Preferences API)             | Built in, battle-tested, fits settings + bond data                  |

---

## 5. Protocol surface (what we handle)

### Device → desktop

- Advertise NUS service (`6e400001-...`) with name `Claude-Pager-XXXX`.
- Permission responses:
  `{"cmd":"permission","id":"<echo>","decision":"once|deny"}`
- Acks for every inbound command:
  `{"ack":"<cmd>","ok":true|false,"n":0}`
- Status response includes `sec:true` once bonded, and `sys.up`/`sys.heap` from runtime. `stats` counters are maintained locally in NVS (`appr`, `deny`, `lvl` — omit `vel`/`nap` since they're pet-state derived).

### Desktop → device (handled)

- Heartbeat snapshots (every change + 10s keepalive). Parsed into in-memory `SessionState`.
- Turn events (`{"evt":"turn","role":...,"content":[...]}`). Appended to a ring buffer (capacity ~64 events, backed by PSRAM).
- Time sync (`{"time":[epoch, tz_offset]}`). Applied to BM8563 RTC.
- `{"cmd":"owner","name":...}` — stored in NVS.
- `{"cmd":"name","name":...}` — stored in NVS, updates advertised name on next reconnect.
- `{"cmd":"unpair"}` — wipes NimBLE bond store, clears owner, emits ack, reboots.
- `{"cmd":"status"}` — returns status response (see below).

### Desktop → device (deferred, ack false)

- `char_begin` — respond `{"ack":"char_begin","ok":false,"error":"unsupported"}`.
- `file`, `chunk`, `file_end`, `char_end` — should not arrive after `char_begin` refusal, but handle defensively with `ok:false`.

### Status response shape (v1)

```json
{
  "ack": "status",
  "ok": true,
  "data": {
    "name": "<nvs:name>",
    "sec": true,
    "sys": { "up": <millis/1000>, "heap": <ESP.getFreeHeap()> },
    "stats": { "appr": <n>, "deny": <n>, "lvl": 0 }
  }
}
```

`bat` omitted intentionally — CoreS3 SE has no battery.

---

## 6. UI modes

Navigation: horizontal swipe between Glance ↔ Recent ↔ Settings. Approval takes over regardless of current mode when `prompt` appears in a snapshot; returns to previous mode when prompt clears.

### 6.1 Glance (default)

Primary ambient view. Always-on while connected (dims after 30s idle, any touch wakes).

```
  ┌──────────────────────────────┐
  │  Pager · connected           │
  │                              │
  │     2 running  ·  0 waiting  │
  │                              │
  │  ▓▓▓▓▓▓▓▓▓░░░░  31.2k today  │
  │                              │
  │  10:42  git push             │
  │  10:41  yarn test            │
  └──────────────────────────────┘
```

Elements: device status line, large counters, token bar (today vs. a rolling max), last two transcript entries from `snapshot.entries`.

### 6.2 Approval (modal takeover)

Triggered when `snapshot.prompt` is present. Exits when prompt clears or user decides.

```
  ┌──────────────────────────────┐
  │  ⚠  Approval needed          │
  │                              │
  │  Tool: Bash                  │
  │                              │
  │  rm -rf /tmp/foo             │
  │                              │
  │  [  Deny  ]    [  Approve  ] │
  └──────────────────────────────┘
```

- Full `prompt.hint` wraps if needed; truncate with ellipsis only if it exceeds 3 lines.
- Approve is on the right (thumb-natural), Deny on the left.
- Deny requires a **tap-and-hold for 400ms** to prevent mis-taps on destructive rejections. Approve is a normal tap.
- Optional audio chime on arrival (configurable in Settings, default off).
- Approval time under 5 seconds increments a local "fast approvals" counter (ambient stat, not surfaced unless asked).

### 6.3 Recent

Scrollable list of captured turn events, newest first. Data lives in a ring buffer (~64 entries) in PSRAM; cleared on reboot.

Each row shows a timestamp (from local RTC), role (`assistant`/`user`), and a one-line summary extracted from the first text block of `content`. Tap a row to expand inline.

### 6.4 Settings

Static view. Items:

- Device name (editable via on-screen keyboard if user wants; defaults to `Claude-Pager-XXXX`).
- Owner name (read-only; set by desktop).
- Audio chime toggle.
- Passkey — only shown during an active pairing attempt.
- **Forget bonds** button — wipes local bond store, reboots (equivalent to receiving `unpair` but initiated by user).
- Firmware version string.

---

## 7. Security

- Mark the NUS RX characteristic and TX CCCD as encrypted-only.
- Advertise with DisplayOnly IO capability so macOS prompts the user for the 6-digit passkey the Pager displays.
- Persist bond (LTK) in NimBLE's NVS-backed store; reconnects reuse it without re-prompting.
- Include `sec:true` in status responses once the link is encrypted.
- On `unpair` (desktop-initiated) or Settings → Forget bonds (user-initiated): wipe all bonds, clear owner name from NVS, reboot.
- On `file.path` validation (if folder push is ever enabled): reject any path containing `..` or starting with `/`.

---

## 8. State machine

```
     ┌──────────┐  BLE up       ┌──────────┐
     │  Boot    │──────────────▶│ Advert.  │
     └──────────┘               └────┬─────┘
                                     │ connect
                                     ▼
                                ┌──────────┐
                        ┌───────│ Pairing  │  (first time only)
                        │       └────┬─────┘
                        │            │ bonded
                        │            ▼
                        │       ┌──────────┐
                        └──────▶│ Connected│◀──┐
                                └────┬─────┘   │
                          heartbeat  │         │ disconnect /
                          has prompt │         │ 30s silence
                                     ▼         │
                                ┌──────────┐   │
                                │ Approval │───┘
                                └──────────┘
```

Connected state renders Glance/Recent/Settings based on user navigation. If no snapshot arrives for >30s, show "disconnected" banner in Glance and return to advertising.

---

## 9. Success criteria (v1 ship)

1. Boots from cold to Glance view in under 3 seconds.
2. Pairs with Claude for macOS using the 6-digit passkey flow. `sec:true` visible in Hardware Buddy window's stats panel.
3. Heartbeat changes render on screen within 1 second of receipt.
4. Approval prompt triggers takeover within 500 ms of heartbeat arrival.
5. Approve/Deny round-trip clears the prompt on the desktop side reliably.
6. Survives a full macOS sleep/wake cycle with auto-reconnect.
7. No resets or OOMs over a 24-hour continuous-use session.
8. `unpair` from desktop wipes local bonds; subsequent connect prompts for a fresh passkey.

---

## 10. Open questions

- **Name uniqueness.** Should `Claude-Pager-XXXX` suffix come from the BT MAC or a user-settable slug? Defaulting to MAC for v1; revisit if multi-device ownership becomes real.
- **Chime sound.** Generate in code (simple tone) or ship a short WAV on flash? Defaulting to generated tone, ~200ms soft double-chirp.
- **Token bar scaling.** `tokens_today` has no natural max. Suggest scaling against a rolling 7-day peak from NVS, or just logarithmic. Defaulting to logarithmic (visually forgiving, no state needed).
- **LVGL theme.** Default dark theme fits the always-on desk context. Light/dark toggle deferred to v2.

---

## 11. Non-spec notes

- Upstream `claude-desktop-buddy` is MIT; we're building a fresh implementation against REFERENCE.md, not forking. No code copied; protocol only.
- Project name **Pager** is not a trademark claim, just an internal/repo name. No Anthropic branding in the binary beyond the required `Claude-` advertising prefix.
