# Handoff

This firmware was written end-to-end against `PAGER_SPEC.md` on a different
machine and **has never been compiled**. It needs a `pio run` pass on real
hardware before anything is trusted.

## Prereqs on this machine

- PlatformIO Core: `brew install platformio` (or `pip install platformio`)
- USB-C cable to the M5Stack CoreS3 SE
- Claude for macOS with Developer Mode enabled
  (Help → Troubleshooting → Enable Developer Mode)

## Paste this to Claude Code in this directory

> I just transferred this firmware project from another machine. It's `v1.0.0`
> per PAGER_SPEC.md but has never been compiled. Please:
>
> 1. Read CLAUDE.md and PAGER_SPEC.md to load context.
> 2. Run `pio run` and report what happens. Don't fix things speculatively —
>    surface the first real compile error and propose the smallest correct fix.
> 3. Once it builds clean, walk me through `pio run -t upload` and the pairing
>    flow from PAGER_SPEC.md §9 (boot → passkey → `sec:true` → snapshot
>    rendering → approval round-trip). I want to verify each success criterion
>    individually.
>
> Don't refactor or "improve" anything until v1 is proven on hardware. The
> non-goals in PAGER_SPEC.md §2 are settled — don't reopen them.

## Likely first-build friction

Areas most likely to need a small fix once `pio run` runs for the first time:

- **LVGL 9.x function aliases** — `lv_btn_create` / `lv_list_add_btn` /
  `lv_obj_clear_flag` are deprecated names. LVGL keeps backwards aliases but
  some 9.2.x builds emit warnings or want the new names
  (`lv_button_create` / `lv_list_add_button` / `lv_obj_remove_flag`).
- **`M5.Display.pushImage` overload** — depending on the M5GFX/LovyanGFX
  release, the `uint16_t*` argument may need to be `const`.
- **Designated initializers** in `src/main.cpp` — work as a GCC C++17
  extension; if the toolchain rejects them, switch to positional init.

## After it boots

PAGER_SPEC.md §9 lists the eight ship criteria. Walk them in order — each
exercises a different layer (BLE, encryption, JSON, LVGL, NVS, audio).

## Diagnostic scripts (`scripts/`)

These earned their keep during bring-up and are checked in for reuse. All
require `pip install bleak pyserial` (or PIO's bundled Python — both
modules are pre-installed there).

- **`fix_lvgl_xtensa.py`** — pre-build hook (wired in `platformio.ini`).
  Removes LVGL's ARM-only `.S` files (Helium, NEON) from libdeps before
  compilation, since the xtensa assembler chokes on the `<stdint.h>`
  typedefs they pull in unconditionally. Runs automatically; no manual
  invocation.

- **`pagermon.py`** — long-running USB-CDC monitor that reconnects across
  drops. The ESP32-S3's CDC port disappears whenever NimBLE crashes,
  upload runs, or the device reboots; `pio device monitor` dies on the
  first drop. This one keeps logging. Run it in another terminal:
  `python3 scripts/pagermon.py`.

- **`blecli.py`** — connects to the Pager as a generic BLE central,
  prints the discovered GATT structure, subscribes to TX, writes a
  `{"cmd":"status"}` to RX, and dumps the reply. Use this whenever
  Claude's panel says "No response" — if `blecli` works the firmware
  is fine and the issue is on Claude's side. The first time we ran it
  it caught the `WRITE_NR` and `setValue+notify` bugs.

- **`blefake.py`** — simulates Claude Desktop. Connects, subscribes to
  TX, sends a heartbeat with a synthetic permission prompt, and waits
  for the device's `{"cmd":"permission",...}` response. Lets you test
  the approval-takeover UI end-to-end without needing a real Claude
  Code permission prompt to fire at a heartbeat-friendly moment.

For all the BLE scripts: Cmd-Q Claude first so the peripheral connection
is free.
