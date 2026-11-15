#pragma once
// ---------------------------------------------------------------------------
// board_panel - the 480x480 round RGB panel (ST7701 controller) and its backlight.
//
//   board_panel_begin()      resets the panel, sends its set-up commands over the
//                            serial command port, then starts the RGB output
//   board_panel_draw(...)    copy a rectangle of 16-bit pixels into the frame buffer
//   board_backlight_begin()  / board_backlight(percent)   0..100
// ---------------------------------------------------------------------------
#include <Arduino.h>

bool board_panel_begin();
void board_panel_draw(int x1, int y1, int x2, int y2, const void* pixels);   // x2,y2 inclusive

void board_backlight_begin(uint8_t percent);
void board_backlight(uint8_t percent);
