#include "board_touch.h"
#include "board_i2c.h"
#include "board_pins.h"
#include "board_expander.h"

namespace {
constexpr uint8_t kRegTouchData   = 0x01;   // 6 bytes: gesture, count, X hi/lo, Y hi/lo
constexpr uint8_t kRegVersion     = 0x15;
constexpr uint8_t kRegChipId      = 0xA7;   // chip id, project id, firmware version
constexpr uint8_t kRegNoAutoSleep = 0xFE;   // write 0xFF: never go to sleep on its own

uint32_t s_read_errors = 0;
}

bool board_touch_begin() {
  pinMode(board::kTouchIntPin, INPUT_PULLUP);

  board_expander_set(board::kLineTouchReset, false);
  vTaskDelay(pdMS_TO_TICKS(10));
  board_expander_set(board::kLineTouchReset, true);
  vTaskDelay(pdMS_TO_TICKS(50));

  // A sleeping controller stops answering until it is touched hard, which looks
  // like a dead screen. Keep it awake.
  const bool awake = board_i2c_write1(board::kTouchAddr, kRegNoAutoSleep, 0xFF);

  uint8_t ver = 0, id[3] = {0, 0, 0};
  const bool seen = board_i2c_read(board::kTouchAddr, kRegVersion, &ver, 1) &&
                    board_i2c_read(board::kTouchAddr, kRegChipId, id, 3);
  if (seen) Serial.printf("[touch] CST820 version 0x%02X, chip 0x%02X, project 0x%02X, firmware 0x%02X\n",
                          ver, id[0], id[1], id[2]);
  else      Serial.println("[touch] controller did not answer");
  return awake && seen;
}

bool board_touch_read(uint16_t& x, uint16_t& y) {
  uint8_t b[6];
  if (!board_i2c_read(board::kTouchAddr, kRegTouchData, b, sizeof b)) {
    if ((++s_read_errors & 0x3FF) == 1) Serial.println("[touch] read failed");   // not every time
    return false;
  }
  if (b[1] == 0) return false;                       // no finger down
  x = (uint16_t)(((b[2] & 0x0F) << 8) | b[3]);
  y = (uint16_t)(((b[4] & 0x0F) << 8) | b[5]);
  return true;
}
