# Backlog

Things noted during v1.0.0 bring-up that didn't make it in. None are
blockers; the device is fully functional per `PAGER_SPEC.md` §9.

## Polish

- **Font sizes for the small panel.** Body text at `lv_font_montserrat_14`
  is legible but tight on a 2" 320×240 IPS at typical desk distance.
  Enable `LV_FONT_MONTSERRAT_16` (or 18) in `include/lv_conf.h` and
  switch the disconnected banner / token-bar caption / entry rows over.
- **A nicer chime.** Currently two queued sine tones (A5 + C#6,
  90 ms each). PAGER_SPEC.md §10 left the door open to a short WAV on
  flash — would be a friendlier sound than raw oscillator pips.
- **Idle-dim transition.** `IDLE_DIM_MS` is a hard 30 s; backlight
  jumps from 220 → 40 instantly. Smoother fade and a tunable threshold
  (NVS-backed) would be nicer.
- **Approval timing.** Spec §6.2 mentions a "fast approvals" counter
  (under 5 s); we track `appr`/`deny` counters but don't surface the
  velocity stat anywhere. Could go on Settings.

## Honest fields

- **`bat.pct` is still fabricated.** We send `pct:100` while on USB, but
  there's no cell. A more honest representation would treat the bat
  object as advisory and let pct flag "no battery" somehow — needs
  Claude Desktop validator behaviour to be more permissive than it is
  today (it rejected omitted bat fields entirely; see `protocol.cpp`).

## Validation gaps

- **§9 #7 — 24 h stability soak.** Not run yet. Things to watch for:
  heap fragmentation in `pager::store::stats` writes, NimBLE bond store
  growth on repeated unpair/repair, LVGL animation timer drift over a
  long interval.
- **§9 #8 — `cmd:unpair` from desktop.** Code path exists
  (`onCmdUnpair` → ack → `forgetEverythingAndReboot`) but not exercised
  end-to-end. Testing requires Claude Desktop to actually send the
  command, which the current Hardware Buddy panel doesn't have a UI
  for; would need a custom test client or wait for the feature to land.

## Build & infra

- **Pre-build script mutates `.pio/libdeps/`.** `scripts/fix_lvgl_xtensa.py`
  removes LVGL's ARM-only `.S` files on every build. Works in practice
  but it's a "fixed at the wrong layer" patch — a cleaner solution
  would be a `library.json` override or a `lib_compat_mode` setting
  that filters by extension. Open to upstreaming the fix to LVGL too:
  the C preprocessor includes inside the `.S` files should sit behind
  an `__ASSEMBLER__` guard so non-ARM targets don't trip on `<stdint.h>`.
- **Hardcoded library pin pair.** `M5Unified 0.2.13` + `M5GFX 0.2.19`
  is the last combination that compiles against any released LVGL 9.x
  (the 2026-04-21 release pair broke this). Worth periodically
  re-checking the latest pair to see if upstream re-aligned.
- **Upload speed at 460800.** CoreS3 SE USB-CDC sometimes hangs the
  baud-change at 1.5 Mbit/s; we dropped to a slower-but-reliable rate.
  Could revisit once esptool / the on-chip USB stack handles it.

## Known behaviour to investigate

- **Claude Desktop's `prompt:` heartbeat timing.** During bring-up we
  watched many Claude heartbeats come through with `running:0,
  waiting:0` even while the user was actively approving Claude Code
  permission prompts. We later did catch a real `prompt:`-bearing
  heartbeat (the `pwsh` one in the README) and confirmed end-to-end
  works. But the bridge appears to drop in-flight prompts that resolve
  inside the 10 s heartbeat cycle. Worth filing upstream against
  `anthropics/claude-desktop-buddy` once we can reproduce reliably.

## Stretch

- **Cowork support.** Spec mentions Claude Cowork sessions alongside
  Claude Code; we haven't tested that path.
- **Windows desktop.** Anthropic's REFERENCE.md says the BLE bridge
  works on Windows too. Untested with this firmware.
- **LV_LARGE_FONT.** If we add CJK or larger glyph sets, may need
  `LV_FONT_FMT_TXT_LARGE 1` in `lv_conf.h`.
