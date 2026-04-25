"""
LVGL 9.2 ships ARM-specific SIMD blend implementations as .S files (Helium,
NEON). The runtime guards inside those files sit below an unconditional
`#include` that pulls in `<stdint.h>`. On the xtensa-esp32 toolchain the C
preprocessor still emits `typedef`s into the assembler input, which then
fails. PlatformIO's library builder compiles every .c/.S in lib root, so we
have no per-file ifdef escape. Drop the .S files on this target — their .h
counterparts stay (the .c blenders include them but only call the asm under
`__ARM_FEATURE_MVE` / `__ARM_NEON`, which xtensa never defines).
"""

import os

Import("env")  # noqa: F821  (PlatformIO injects this)

libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR")  # noqa: F821
piodir = env.subst("$PIOENV")  # noqa: F821
lvgl_root = os.path.join(libdeps_dir, piodir, "lvgl")

if os.path.isdir(lvgl_root):
    for dirpath, _dirnames, filenames in os.walk(lvgl_root):
        for fn in filenames:
            if fn.endswith(".S"):
                target = os.path.join(dirpath, fn)
                os.remove(target)
                print("[fix_lvgl_xtensa] removed", target)
