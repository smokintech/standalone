#pragma once
// ---------------------------------------------------------------------------
// board_pins.h - what is wired to what on the Waveshare ESP32-S3-Touch-LCD-2.1,
// plus the panel's pixel geometry and timing. Everything board-specific that is
// a plain number lives here, so the rest of the board code reads as logic.
// ---------------------------------------------------------------------------
#include <stdint.h>

namespace board {

// ---- shared I2C bus: touch controller, IO expander, and the sensor board -----
constexpr int      kI2cSda   = 15;
constexpr int      kI2cScl   = 7;
constexpr uint32_t kI2cHz    = 100000;

// ---- TCA9554 IO expander (I2C 0x20). Line numbers are 1..8 ---------------------
constexpr uint8_t  kExpanderAddr   = 0x20;
constexpr uint8_t  kLineLcdReset   = 1;   // panel reset, active low
constexpr uint8_t  kLineTouchReset = 2;   // touch controller reset, active low
constexpr uint8_t  kLineLcdCs      = 3;   // panel's serial-command chip select, active low
constexpr uint8_t  kLineBuzzer     = 8;   // piezo, high = sounding

// ---- touch controller (CST820, I2C 0x15) ---------------------------------------
constexpr uint8_t  kTouchAddr = 0x15;
constexpr int      kTouchIntPin = 16;

// ---- panel's serial command port (write-only, 9-bit frames) --------------------
constexpr int      kLcdSpiMosi = 1;
constexpr int      kLcdSpiClk  = 2;
constexpr uint32_t kLcdSpiHz   = 40000000;

// ---- backlight (PWM) -----------------------------------------------------------
constexpr int      kBacklightPin  = 6;
constexpr uint32_t kBacklightHz   = 20000;
constexpr uint8_t  kBacklightBits = 10;

// ---- the 480x480 RGB panel -----------------------------------------------------
constexpr int      kPanelW = 480;
constexpr int      kPanelH = 480;

constexpr int      kPinHsync = 38;
constexpr int      kPinVsync = 39;
constexpr int      kPinDe    = 40;
constexpr int      kPinPclk  = 41;
constexpr int      kPinDisp  = -1;   // not connected

// Data lines D0..D15, in order.
constexpr int kPinData[16] = { 5, 45, 48, 47, 21, 14, 13, 12, 11, 10, 9, 46, 3, 8, 18, 17 };

// Pixel clock and blanking. 12 MHz is deliberately gentle: the panel streams its
// frame from PSRAM, and a slower clock leaves bus time for Wi-Fi and the CPU.
constexpr uint32_t kPclkHz = 12000000;
constexpr int      kHsyncPulse = 8,  kHsyncBack = 10, kHsyncFront = 50;
constexpr int      kVsyncPulse = 3,  kVsyncBack = 8,  kVsyncFront = 8;

// The panel driver refills a small bounce buffer in internal RAM from the PSRAM
// frame buffer; this many lines at a time. Keeps the picture steady.
constexpr int      kBounceLines = 20;

}  // namespace board
