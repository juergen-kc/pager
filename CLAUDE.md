# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for **Pager**, an M5Stack CoreS3 SE (ESP32-S3) touchscreen companion that talks to the Claude desktop app over BLE using Anthropic's documented Hardware Buddy protocol (Nordic UART Service + newline-delimited JSON). Current state is `v1.0.0` — full snapshot/turn parsing, four UI modes (Glance/Approval/Recent/Settings), permission round-trip, time sync, and the `status`/`name`/`owner`/`unpair` commands. The v1 scope, milestones, and explicit non-goals live in `PAGER_SPEC.md`; read it before designing anything user-visible.

## Build, flash, monitor

PlatformIO project, single env `[env:pager]`. From the repo root:

```sh
pio run                  # compile only
pio run -t upload        # compile + flash over USB-C
pio device monitor       # serial monitor at 115200 with esp32 exception decoder
pio run -t clean         # wipe .pio/ build cache
```

There is no host-side test suite; this is bare-metal firmware. Validation is on-device (serial logs, screen, BLE peer behaviour).

**CoreS3 SE upload quirk**: no dedicated BOOT button. If `pio run -t upload` hangs on `Connecting......` for 10+ seconds, long-press RESET for ~3s until the green LED lights, release, then retry. PlatformIO usually triggers download mode automatically but not always.

## Architecture (the parts that span files)

### Composition root in `src/main.cpp`

`main.cpp` is the only place that knows about every module. It wires `pager::ble` → FreeRTOS queue → `pager::proto::handleLine` → `pager::state` → `pager::ui::router`, plus the persistence/clock/audio modules. Modules in `src/ble/`, `src/proto/`, `src/state/`, `src/ui/`, etc., never call each other directly — they all bind through main. Keep it that way; layered direct calls would pull the BLE task into LVGL or vice versa.

### Cross-task discipline (the load-bearing rule)

BLE callbacks (`pager::ble::NusCallbacks` — `onStateChanged`, `onLineReceived`, `onPasskeyDisplay`) fire on the NimBLE task. LVGL is **not** thread-safe and must only be touched from the main loop. The split is enforced by two channels:

- **Inbound JSON lines** cross the boundary via a FreeRTOS queue of `std::string*` (depth `INBOUND_QUEUE_DEPTH`). The BLE callback heap-allocates a copy and `xQueueSend`s the pointer; `loop()` drains and `delete`s.
- **The pairing passkey** crosses via two volatile globals (`g_pendingPasskey`, `g_passkeyPending`). 32-bit aligned writes are atomic on Xtensa, so this is safe without locking. The `g_passkeyPending` flag exists because `0` is a legal (if rare) passkey value.

If you add a new BLE→main signal, follow one of these two patterns. Don't call `lv_*` from a BLE callback.

### MTU-boundary line buffering

`nus.cpp::ingest()` accumulates raw BLE writes into `g_lineBuffer` and emits complete `\n`-delimited lines upward — desktop messages routinely fragment across MTU boundaries. The buffer is capped at `LINE_BUFFER_CAPACITY` (5 KB, defined in `include/config.h`); on overrun it is silently cleared, on the assumption that anything bigger than the protocol's 4 KB turn-event cap is garbage. Don't add per-message size logic upstream — the line is already a complete logical unit by the time `onLineReceived` sees it.

### Encryption enforced at the characteristic, not at runtime

NUS characteristics are created with `NIMBLE_PROPERTY::WRITE_ENC` / `READ_ENC` (`nus.cpp:108,114`). The BLE stack itself rejects unencrypted access, so app code never has to gate on `isSecure()` for protocol traffic. Pairing uses LE Secure Connections with `BLE_HS_IO_DISPLAY_ONLY` IO capability — the device generates a random 6-digit passkey via `esp_random()` and the host prompts the user to type it. Bonds persist in NimBLE's NVS-backed store across reboots.

### Hardware Buddy protocol is upstream-defined

UUIDs in `include/config.h` (NUS service + RX/TX) are fixed by Anthropic's REFERENCE.md — do not change. This firmware is a clean implementation of the documented protocol; no code is copied from `anthropics/claude-desktop-buddy`. When adding protocol handlers, treat REFERENCE.md as the source of truth and `PAGER_SPEC.md` §5 as the v1 subset we actually handle (plus the `ack:false` refusal pattern for deferred commands like `char_begin`).

## v1 design constraints worth knowing

From `PAGER_SPEC.md` §2, these are explicit non-goals — don't propose them as improvements:

- **No on-device allowlist auto-approval.** `prompt.hint` is model output; pattern-matching it on the device is a security regression. Allowlists belong in desktop settings.
- **No session-attributed views.** The protocol doesn't tell us which session a transcript entry came from — don't fake it.
- **No battery UI / IMU features.** CoreS3 SE has neither.
- **Pager is not a pet.** Approval and ambient awareness are the product.

### UI is LVGL 9 over M5GFX

`src/ui/lvgl_port.cpp` brings up LVGL with two 40-row RGB565 partial buffers in SRAM (DMA-friendly, ~25 KB each). The flush callback uses synchronous `M5.Display.pushImage` — the DMA variant would force us to defer `lv_display_flush_ready` until the transfer drains, and the partial buffer size is small enough that synchronous push fits well inside one frame. Touch goes through M5.Touch, sampled fresh on every `M5.update()` so LVGL's pointer driver always sees a current sample.

`src/ui/router.cpp` owns a tileview (Glance | Recent | Settings, horizontal swipe) plus a full-screen Approval overlay and a passkey overlay. State changes flow through `router::onStateChanged()` which refreshes all three tile views — only the visible one actually paints, so it's cheap.

LVGL config lives in `include/lv_conf.h`: built-in 64 KB allocator (predictable, debuggable), dark theme, Montserrat 14/24 fonts, `LV_TICK_CUSTOM` reads `millis()` so we don't need a separate tick timer. The deny-button long-press threshold is set explicitly in `router::begin()` to `DENY_HOLD_MS` (400 ms) so a future LVGL default change can't silently weaken the deny gesture.

### Where state lives

- `pager::state::SessionState` (in `src/state/session.cpp`) is the in-RAM mirror of the most recent heartbeat snapshot — counters, tokens, prompt, last entries. Single producer (proto on main loop), single consumer (UI on main loop), so no locks.
- The turn-event ring buffer is allocated from PSRAM via `heap_caps_malloc(MALLOC_CAP_SPIRAM)` so it doesn't compete with LVGL/BLE for SRAM. ~64 entries, oldest evicted on overflow, cleared on reboot per spec.
- `pager::store` (in `src/persistence/store.cpp`) wraps `Preferences` (NVS namespace `pager`) for owner, device-name override, chime toggle, and approval/deny counters. Counters write through immediately so a crash mid-session doesn't lose stats.

## Stack reference

| Layer        | Library                          |
|--------------|----------------------------------|
| Build        | PlatformIO, `platform=espressif32@6.7.0`, `board=esp32-s3-devkitc-1` (with CoreS3 16 MB / 8 MB PSRAM flags) |
| HAL          | M5Unified `^0.2.0`               |
| BLE          | NimBLE-Arduino `^2.0.0`          |
| JSON         | ArduinoJson `^7.0.4`             |
| UI           | LVGL `^9.2.0` (config in `include/lv_conf.h`) |

Code is C++17 (`-std=gnu++17`), namespaced under `pager::ble`, `pager::proto`, `pager::state`, `pager::store`, `pager::system_clock`, `pager::audio`, `pager::ui`, and `pager::ui::router/lvgl_port`.
