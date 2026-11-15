#pragma once
// ---------------------------------------------------------------------------
// board_lvgl - connects LVGL to the panel and the touch controller.
//
//   board_lvgl_begin()   call once, after the panel and touch are up
//   board_lvgl_poll()    call every pass of loop(): runs LVGL's timers and reads touch
// ---------------------------------------------------------------------------
#include <lvgl.h>
#include "lv_conf.h"

void board_lvgl_begin();
void board_lvgl_poll();
