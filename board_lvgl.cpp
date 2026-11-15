#include "board_lvgl.h"
#include "board_pins.h"
#include "board_panel.h"
#include "board_touch.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

namespace {

// LVGL draws into small buffers in INTERNAL RAM and hands the panel only the
// rectangles that changed ("partial" refresh). The panel's frame buffer lives in
// PSRAM and is read continuously by the display hardware; drawing straight into it
// would fight that traffic for the same memory bus. Each buffer holds a few lines;
// a larger one means fewer, bigger hand-offs but costs internal RAM.
constexpr int kDrawLines  = 8;
constexpr int kTickMs     = 2;

lv_disp_draw_buf_t s_draw_buf;
lv_disp_drv_t      s_disp_drv;
lv_indev_drv_t     s_touch_drv;

void on_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
  board_panel_draw(area->x1, area->y1, area->x2, area->y2, px);
  lv_disp_flush_ready(drv);        // the panel driver has already copied the pixels
}

void on_touch_read(lv_indev_drv_t*, lv_indev_data_t* data) {
  uint16_t x, y;
  if (board_touch_read(x, y)) {
    data->point.x = x;
    data->point.y = y;
    data->state   = LV_INDEV_STATE_PR;
  } else {
    data->state   = LV_INDEV_STATE_REL;
  }
}

void on_tick(void*) { lv_tick_inc(kTickMs); }

}  // namespace

void board_lvgl_begin() {
  lv_init();

  const size_t px = (size_t)board::kPanelW * kDrawLines;
  void* a = heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  void* b = heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!a) {
    Serial.println("[lvgl] no internal RAM for a draw buffer - display will stay blank");
    return;
  }
  if (!b) Serial.println("[lvgl] internal RAM is tight - using a single draw buffer");
  Serial.printf("[lvgl] draw buffer %u bytes x%d in internal RAM\n",
                (unsigned)(px * sizeof(lv_color_t)), b ? 2 : 1);

  lv_disp_draw_buf_init(&s_draw_buf, a, b, px);

  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.hor_res      = board::kPanelW;
  s_disp_drv.ver_res      = board::kPanelH;
  s_disp_drv.flush_cb     = on_flush;
  s_disp_drv.full_refresh = 0;                 // redraw only what changed
  s_disp_drv.draw_buf     = &s_draw_buf;
  lv_disp_drv_register(&s_disp_drv);

  lv_indev_drv_init(&s_touch_drv);
  s_touch_drv.type    = LV_INDEV_TYPE_POINTER;
  s_touch_drv.read_cb = on_touch_read;
  lv_indev_drv_register(&s_touch_drv);

  esp_timer_create_args_t args = {};
  args.callback = on_tick;
  args.name     = "lvgl_tick";
  esp_timer_handle_t timer = nullptr;
  esp_timer_create(&args, &timer);
  esp_timer_start_periodic(timer, kTickMs * 1000);
}

void board_lvgl_poll() {
  lv_timer_handler();
}
