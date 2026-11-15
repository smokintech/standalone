#pragma once
// ---------------------------------------------------------------------------
// board_i2c - the board's one I2C bus (touch, IO expander, sensor board).
// Only the main loop task uses it, so there is no locking.
// ---------------------------------------------------------------------------
#include <Arduino.h>

bool board_i2c_begin();                                   // true if the bus started

// Register access for devices with an 8-bit register address.
// Both return true only if every byte was acknowledged / received.
bool board_i2c_write(uint8_t addr, uint8_t reg, const uint8_t* data, size_t n);
bool board_i2c_read (uint8_t addr, uint8_t reg, uint8_t* out, size_t n);

inline bool board_i2c_write1(uint8_t addr, uint8_t reg, uint8_t value) {
  return board_i2c_write(addr, reg, &value, 1);
}
