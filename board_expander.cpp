#include "board_expander.h"
#include "board_i2c.h"
#include "board_pins.h"

namespace {
constexpr uint8_t kRegOutput = 0x01;   // level of each output line
constexpr uint8_t kRegConfig = 0x03;   // 1 = input, 0 = output, per line

// What we last wrote to the output register. Keeping our own copy means a line
// change is one write, and a failed read can never scramble the other lines.
uint8_t s_out = 0xFF;                  // power-on state of the chip: all lines high
}

bool board_expander_begin() {
  uint8_t now;
  if (board_i2c_read(board::kExpanderAddr, kRegOutput, &now, 1)) s_out = now;

  // Set the levels first, then turn the lines into outputs, so the buzzer line
  // never goes high on the way.
  s_out &= (uint8_t)~(1u << (board::kLineBuzzer - 1));
  if (!board_i2c_write1(board::kExpanderAddr, kRegOutput, s_out)) return false;
  return board_i2c_write1(board::kExpanderAddr, kRegConfig, 0x00);
}

bool board_expander_set(uint8_t line, bool high) {
  if (line < 1 || line > 8) return false;
  const uint8_t bit  = (uint8_t)(1u << (line - 1));
  const uint8_t next = high ? (uint8_t)(s_out | bit) : (uint8_t)(s_out & ~bit);
  if (!board_i2c_write1(board::kExpanderAddr, kRegOutput, next)) return false;
  s_out = next;
  return true;
}

bool board_buzzer(bool on) {
  return board_expander_set(board::kLineBuzzer, on);
}
