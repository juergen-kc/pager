# Pager

A physical Claude Code remote for your desk.

Pager is a small touchscreen companion that gives you ambient awareness of
Claude Code and Claude Cowork sessions, and lets you approve or deny
tool-use prompts without context-switching away from your main work. It
talks to the Claude desktop app over Bluetooth LE using the documented
[Hardware Buddy protocol](https://github.com/anthropics/claude-desktop-buddy/blob/main/REFERENCE.md).

**→ See [`PAGER_SPEC.md`](./PAGER_SPEC.md) for the v1 specification.**

## Hardware

- [M5Stack CoreS3 SE](https://shop.m5stack.com/products/m5stack-cores3-se-iot-controller-w-o-battery-bottom) (SKU K128-SE)
- USB-C cable

## Build & flash

1. Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/)
   or the VS Code extension.
2. Plug the CoreS3 SE into your machine over USB-C.
3. From the repo root:

   ```sh
   pio run -t upload
   pio device monitor
   ```

The first build pulls M5Unified, NimBLE-Arduino, and ArduinoJson (~30 MB).

### CoreS3 SE download-mode quirk

The CoreS3 SE doesn't have a dedicated BOOT button. If `pio run -t upload`
hangs on `Connecting......` for 10+ seconds, put the board into download
mode manually: **long-press the RESET button for ~3 seconds until the green
LED lights up**, release, then retry the upload. PlatformIO usually
triggers this automatically, but the manual ritual works when it doesn't.

## Pair with Claude

1. In Claude for macOS or Windows, enable **Help → Troubleshooting → Enable
   Developer Mode**.
2. Open **Developer → Open Hardware Buddy…** and click **Connect**.
3. Pick `Claude-Pager-XXXX` from the scanner.
4. The Pager displays a 6-digit passkey. Type it into the OS prompt.
5. Verify `sec: true` in the Hardware Buddy window's stats panel — that
   means the link is encrypted.

If pairing misbehaves after a reflash (old bond data on one side, fresh on
the other), click **Forget this device** in Claude, then power-cycle the
Pager and try again.

## Project layout

```
pager/
├── include/
│   ├── config.h          BLE UUIDs, firmware version, capacities, NVS keys
│   └── lv_conf.h         LVGL 9 configuration (RGB565, dark theme, fonts)
├── src/
│   ├── main.cpp          composition root, BLE↔queue↔protocol↔UI wiring
│   ├── audio/chime.*     generated double-chirp on approval arrival
│   ├── ble/nus.*         NimBLE NUS peripheral + LE SC bonding
│   ├── persistence/      NVS-backed settings + counters
│   ├── proto/protocol.*  Hardware Buddy JSON parse/build/dispatch
│   ├── state/session.*   in-memory snapshot model + PSRAM ring buffer
│   ├── system/clock.*    RTC + epoch/TZ formatting helpers
│   └── ui/               LVGL port, router, view widgets
├── PAGER_SPEC.md         v1 specification
├── CLAUDE.md             notes for Claude Code instances
├── platformio.ini
├── LICENSE
└── README.md
```

## Current status

**v1.0.0 — ship.** All four UI modes (Glance / Approval / Recent / Settings),
full snapshot + turn-event handling, permission round-trip, time sync, and
the `status` / `name` / `owner` / `unpair` commands. Folder push is refused
with `ack:false` per spec.

### Milestones

| Version  | Scope                                                              |
|----------|--------------------------------------------------------------------|
| v0.1.0   | Hello-world baseline                                               |
| v0.2.0   | Heartbeat snapshot parsing + Glance view render                    |
| v0.3.0   | Approval mode + permission round-trip                              |
| v0.4.0   | Recent view (ring buffer) + Settings                               |
| v0.5.0   | Status command ack, time sync, owner + device-name persistence     |
| v1.0.0   | Ship (this release)                                                |

## Protocol source

This firmware is a fresh implementation against Anthropic's
[Hardware Buddy REFERENCE.md](https://github.com/anthropics/claude-desktop-buddy/blob/main/REFERENCE.md).
No code is copied from `anthropics/claude-desktop-buddy`; only the
documented wire protocol is used.

## License

MIT. See [LICENSE](./LICENSE).
