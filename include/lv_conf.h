// Pager LVGL 9.x configuration. Trimmed to what we actually use:
// RGB565, partial buffers in SRAM (DMA friendly), big allocations from PSRAM,
// stock dark theme, Montserrat 14/24 fonts. Anything not listed here falls
// back to the LVGL default in lv_conf_internal.h.

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH        16
// LVGL emits RGB565 in native (little-endian) byte order; M5GFX's
// pushImage(uint16_t*) sends those bytes as-is over SPI, but the ILI9342C
// expects RGB565 big-endian on the wire. Without this swap, R↔B end up
// transposed (dark red shows as blue, green as yellow) and text edges get
// chromatic fringing because adjacent sub-pixels are mis-coloured.
#define LV_COLOR_16_SWAP      1

// Built-in LVGL allocator with a 64 KB pool. Predictable, debuggable, and
// avoids depending on ESP-IDF's heap caps. The pool itself sits in BSS and
// is small enough that internal SRAM can absorb it; PSRAM stays free for
// the turn-event ring.
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_BUILTIN
#define LV_MEM_SIZE           (64U * 1024U)
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_TICK_CUSTOM            1
#define LV_TICK_CUSTOM_INCLUDE    "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DPI_DEF                130
#define LV_USE_LOG                0

// Layouts + standard widgets we depend on.
#define LV_USE_FLEX               1
#define LV_USE_GRID               1

#define LV_USE_LABEL              1
#define LV_LABEL_TEXT_SELECTION   0
#define LV_LABEL_LONG_TXT_HINT    1

#define LV_USE_BTN                1
#define LV_USE_BUTTON             1
#define LV_USE_BAR                1
#define LV_USE_SWITCH             1
#define LV_USE_LIST               1
#define LV_USE_LINE               1
#define LV_USE_TILEVIEW           1
#define LV_USE_KEYBOARD           1
#define LV_USE_TEXTAREA           1
#define LV_USE_MSGBOX             1

// Themes.
#define LV_USE_THEME_DEFAULT      1
#define LV_THEME_DEFAULT_DARK     1
#define LV_THEME_DEFAULT_GROW     1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

// Fonts. The 24px font carries the big counters / passkey; 14 is body.
#define LV_FONT_MONTSERRAT_14     1
#define LV_FONT_MONTSERRAT_24     1
#define LV_FONT_DEFAULT           &lv_font_montserrat_14

#define LV_USE_PERF_MONITOR       0
#define LV_USE_MEM_MONITOR        0

#endif // LV_CONF_H
