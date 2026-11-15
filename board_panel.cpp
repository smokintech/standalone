#include "board_panel.h"
#include "board_pins.h"
#include "board_expander.h"

#include "driver/spi_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

namespace {

esp_lcd_panel_handle_t s_panel = nullptr;
spi_device_handle_t    s_cmd_port = nullptr;

// ---------------------------------------------------------------------------
// The panel's set-up commands.
//
// The ST7701 is configured through a write-only serial port. Each entry below is
//     command, number-of-data-bytes, data bytes...
// A count of kDelayMarker means "no data; wait for the next two bytes (high, low)
// milliseconds after sending". These values describe THIS glass (voltages, gamma,
// timing); they are fixed by the panel and must be written exactly as listed.
// ---------------------------------------------------------------------------
constexpr uint8_t kDelayMarker = 0xFF;

const uint8_t kPanelSetup[] = {

  // ---- select register bank 0x10 ----
  0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x10,
  0xC0, 2, 0x3B, 0x00,
  0xC1, 2, 0x0B, 0x02,
  0xC2, 2, 0x07, 0x02,
  0xCC, 1, 0x10,
  0xCD, 1, 0x08,
  0xB0, 16, 0x00, 0x11, 0x16, 0x0E, 0x11, 0x06, 0x05, 0x09, 0x08, 0x21, 0x06, 0x13, 0x10, 0x29, 0x31, 0x18,
  0xB1, 16, 0x00, 0x11, 0x16, 0x0E, 0x11, 0x07, 0x05, 0x09, 0x09, 0x21, 0x05, 0x13, 0x11, 0x2A, 0x31, 0x18,

  // ---- select register bank 0x11 ----
  0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x11,
  0xB0, 1, 0x6D,
  0xB1, 1, 0x37,
  0xB2, 1, 0x81,
  0xB3, 1, 0x80,
  0xB5, 1, 0x43,
  0xB7, 1, 0x85,
  0xB8, 1, 0x20,
  0xC1, 1, 0x78,
  0xC2, 1, 0x78,
  0xD0, 1, 0x88,
  0xE0, 3, 0x00, 0x00, 0x02,
  0xE1, 11, 0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x20, 0x20,
  0xE2, 13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xE3, 4, 0x00, 0x00, 0x11, 0x00,
  0xE4, 2, 0x22, 0x00,
  0xE5, 16, 0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xE6, 4, 0x00, 0x00, 0x11, 0x00,
  0xE7, 2, 0x22, 0x00,
  0xE8, 16, 0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xEB, 7, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00,
  0xED, 16, 0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF, 0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF,
  0xEF, 6, 0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F,

  // ---- select register bank 0x13 ----
  0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
  0xEF, 1, 0x08,

  // ---- select register bank 0x00 ----
  0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
  0x36, 1, 0x00,
  0x3A, 1, 0x66,
  0x11, kDelayMarker, 0x01, 0xE0,        // wait 480 ms
  0x20, kDelayMarker, 0x00, 0x78,        // wait 120 ms
  0x29, 0,
};

// ---- the serial command port: 9-bit frames, first bit = 0 for a command, 1 for data
void send_frame(bool is_data, uint8_t value) {
  spi_transaction_t t;
  memset(&t, 0, sizeof t);
  t.cmd  = is_data ? 1 : 0;      // the 1-bit "command" field carries the D/C flag
  t.addr = value;                // the 8-bit "address" field carries the byte
  spi_device_transmit(s_cmd_port, &t);
}

bool open_command_port() {
  spi_bus_config_t bus;
  memset(&bus, 0, sizeof bus);
  bus.mosi_io_num   = board::kLcdSpiMosi;
  bus.miso_io_num   = -1;
  bus.sclk_io_num   = board::kLcdSpiClk;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 64;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  spi_device_interface_config_t dev;
  memset(&dev, 0, sizeof dev);
  dev.command_bits   = 1;
  dev.address_bits   = 8;
  dev.mode           = 0;
  dev.clock_speed_hz = board::kLcdSpiHz;
  dev.spics_io_num   = -1;       // chip select is an expander line, handled by hand
  dev.queue_size     = 1;
  return spi_bus_add_device(SPI2_HOST, &dev, &s_cmd_port) == ESP_OK;
}

void pulse_reset() {
  board_expander_set(board::kLineLcdReset, false);
  vTaskDelay(pdMS_TO_TICKS(10));
  board_expander_set(board::kLineLcdReset, true);
  vTaskDelay(pdMS_TO_TICKS(50));
}

void send_setup_table() {
  board_expander_set(board::kLineLcdCs, false);
  vTaskDelay(pdMS_TO_TICKS(10));

  const uint8_t* p   = kPanelSetup;
  const uint8_t* end = kPanelSetup + sizeof kPanelSetup;
  while (p < end) {
    const uint8_t cmd = *p++;
    const uint8_t n   = *p++;
    send_frame(false, cmd);
    if (n == kDelayMarker) {
      const uint32_t ms = ((uint32_t)p[0] << 8) | p[1];
      p += 2;
      vTaskDelay(pdMS_TO_TICKS(ms));
    } else {
      for (uint8_t i = 0; i < n; i++) send_frame(true, *p++);
    }
  }

  board_expander_set(board::kLineLcdCs, true);
  vTaskDelay(pdMS_TO_TICKS(10));
}

bool start_rgb_output() {
  esp_lcd_rgb_panel_config_t cfg;
  memset(&cfg, 0, sizeof cfg);

  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz            = board::kPclkHz;
  cfg.timings.h_res              = board::kPanelW;
  cfg.timings.v_res              = board::kPanelH;
  cfg.timings.hsync_pulse_width  = board::kHsyncPulse;
  cfg.timings.hsync_back_porch   = board::kHsyncBack;
  cfg.timings.hsync_front_porch  = board::kHsyncFront;
  cfg.timings.vsync_pulse_width  = board::kVsyncPulse;
  cfg.timings.vsync_back_porch   = board::kVsyncBack;
  cfg.timings.vsync_front_porch  = board::kVsyncFront;
  // all four polarity flags stay 0: normal sync polarity, data on the rising edge

  cfg.data_width            = 16;
  cfg.bits_per_pixel        = 16;
  cfg.num_fbs               = 1;                                    // one frame buffer, in PSRAM
  cfg.bounce_buffer_size_px = board::kBounceLines * board::kPanelW; // pixels, not bytes
  cfg.psram_trans_align     = 64;

  cfg.hsync_gpio_num = board::kPinHsync;
  cfg.vsync_gpio_num = board::kPinVsync;
  cfg.de_gpio_num    = board::kPinDe;
  cfg.pclk_gpio_num  = board::kPinPclk;
  cfg.disp_gpio_num  = board::kPinDisp;
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = board::kPinData[i];

  cfg.flags.fb_in_psram = true;                                     // everything else off

  const esp_err_t e = esp_lcd_new_rgb_panel(&cfg, &s_panel);
  if (e != ESP_OK || s_panel == nullptr) {
    Serial.printf("[panel] could not create the RGB panel: %s\n", esp_err_to_name(e));
    s_panel = nullptr;
    return false;
  }
  esp_lcd_panel_reset(s_panel);
  esp_lcd_panel_init(s_panel);
  return true;
}

}  // namespace

bool board_panel_begin() {
  pulse_reset();
  if (!open_command_port()) {
    Serial.println("[panel] could not open the command port");
    return false;
  }
  send_setup_table();
  return start_rgb_output();
}

void board_panel_draw(int x1, int y1, int x2, int y2, const void* pixels) {
  if (!s_panel) return;
  // The driver wants an exclusive end; ours is inclusive. Never run past the glass.
  int ex = x2 + 1, ey = y2 + 1;
  if (ex > board::kPanelW) ex = board::kPanelW;
  if (ey > board::kPanelH) ey = board::kPanelH;
  esp_lcd_panel_draw_bitmap(s_panel, x1, y1, ex, ey, pixels);
}

// ---- backlight ---------------------------------------------------------------
void board_backlight(uint8_t percent) {
  if (percent > 100) percent = 100;
  const uint32_t full = 1u << board::kBacklightBits;               // 1024 = fully on
  const uint32_t duty = (percent == 100) ? full : (uint32_t)percent * 10u;
  ledcWrite(board::kBacklightPin, duty);
}

void board_backlight_begin(uint8_t percent) {
  ledcAttach(board::kBacklightPin, board::kBacklightHz, board::kBacklightBits);
  board_backlight(percent);
}
