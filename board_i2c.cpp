#include "board_i2c.h"
#include "board_pins.h"
#include <Wire.h>

bool board_i2c_begin() {
  return Wire.begin(board::kI2cSda, board::kI2cScl, board::kI2cHz);
}

bool board_i2c_write(uint8_t addr, uint8_t reg, const uint8_t* data, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (n) Wire.write(data, n);
  return Wire.endTransmission(true) == 0;
}

bool board_i2c_read(uint8_t addr, uint8_t reg, uint8_t* out, size_t n) {
  if (n == 0) return true;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom((int)addr, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; i++) out[i] = (uint8_t)Wire.read();
  return true;
}
