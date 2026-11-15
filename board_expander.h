#pragma once
// ---------------------------------------------------------------------------
// board_expander - the TCA9554 8-line IO expander. It drives the panel and touch
// resets, the panel's chip select and the buzzer. Lines are numbered 1..8 (see
// board_pins.h for which is which).
// ---------------------------------------------------------------------------
#include <Arduino.h>

// Makes every line an output. The buzzer line starts LOW (silent) and every
// other line keeps the level it already had, so nothing blips at start-up.
bool board_expander_begin();

// Drive one line high or low without disturbing the others. False if the chip
// did not acknowledge (the level is then not assumed to have changed).
bool board_expander_set(uint8_t line, bool high);

// The piezo buzzer: true = sounding.
bool board_buzzer(bool on);
