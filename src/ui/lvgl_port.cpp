// LVGL 9 ↔ M5GFX glue. Two SRAM-resident partial buffers (DMA-friendly),
// one pointer-style input device backed by M5.Touch.

#include "ui/lvgl_port.h"

#include <M5Unified.h>
#include <Arduino.h>  // for millis() in the lv_tick_set_cb lambda
#include <lvgl.h>

namespace pager::ui::lvgl_port {

namespace {

// Max panel width we support — sized for the CoreS3 SE landscape (320).
// The Dial (240×240) just uses fewer columns; the unused tail is wasted
// RAM but keeps the buffer allocation static. Two 40-row partial buffers:
// RGB565 → 320 * 40 * 2 = 25 600 bytes each, render-while-DMA-pushes.
constexpr int kMaxHRes  = 320;
constexpr int kBufRows  = 40;
constexpr size_t kBufPx = kMaxHRes * kBufRows;

uint16_t g_buf1[kBufPx];
uint16_t g_buf2[kBufPx];

lv_display_t* g_disp  = nullptr;
lv_indev_t*   g_touch = nullptr;

void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;

  // Synchronous push — pushImageDMA would let us overlap render and transfer
  // but requires deferring lv_display_flush_ready until DMA actually drains.
  // For 40-row partial buffers the synchronous path is well under one frame.
  M5.Display.startWrite();
  M5.Display.pushImage(area->x1, area->y1, w, h,
                       reinterpret_cast<uint16_t*>(px_map));
  M5.Display.endWrite();

  lv_display_flush_ready(disp);
}

void touchReadCb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
  // M5Unified samples touch on M5.update(); the main loop calls that, so by
  // the time LVGL polls us we have a fresh sample.
  auto t = M5.Touch.getDetail(0);
  if (t.isPressed()) {
    data->state   = LV_INDEV_STATE_PRESSED;
    data->point.x = t.x;
    data->point.y = t.y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

} // namespace

void begin() {
  // M5.begin() in main has already initialised display + touch. Configure
  // landscape, then bring LVGL up against the active panel.
  M5.Display.setRotation(1);
  M5.Display.fillScreen(0);
  M5.Display.setBrightness(192);

  lv_init();

  // LVGL 9 dropped the LV_TICK_CUSTOM_SYS_TIME_EXPR mechanism we set in
  // include/lv_conf.h. Without a registered tick callback `lv_tick_get()`
  // returns 0, which silently breaks any code that does
  // `lv_tick_get() - some_past_ms` — including the glance view's
  // disconnected-banner staleness check (it reads as "very stale forever").
  lv_tick_set_cb([]() -> uint32_t { return millis(); });

  // Read panel dimensions from M5GFX after rotation — CoreS3 SE reports
  // 320×240, Dial 240×240. Telling LVGL the right size keeps widgets
  // aligned to the actual visible area; hardcoding 320 painted off-screen
  // on the 240-wide Dial panel.
  const int32_t hres = M5.Display.width();
  const int32_t vres = M5.Display.height();
  g_disp = lv_display_create(hres, vres);
  lv_display_set_flush_cb(g_disp, flushCb);
  lv_display_set_buffers(
      g_disp, g_buf1, g_buf2,
      sizeof(g_buf1),
      LV_DISPLAY_RENDER_MODE_PARTIAL);

  g_touch = lv_indev_create();
  lv_indev_set_type(g_touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(g_touch, touchReadCb);
}

unsigned int tick() {
  // LV_TICK_CUSTOM=1 means LVGL polls millis() itself; we just service timers.
  return lv_timer_handler();
}

void setBacklight(uint8_t level) {
  M5.Display.setBrightness(level);
}

} // namespace pager::ui::lvgl_port
