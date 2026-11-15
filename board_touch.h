#pragma once
// ---------------------------------------------------------------------------
// board_touch - the CST820 capacitive touch controller (single touch point).
// ---------------------------------------------------------------------------
#include <Arduino.h>

bool board_touch_begin();                          // reset, keep it awake, log its ID

// True while a finger is down, with its position in panel pixels (0..479).
// False when nothing is touching, or the controller did not answer.
bool board_touch_read(uint16_t& x, uint16_t& y);
