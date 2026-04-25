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
