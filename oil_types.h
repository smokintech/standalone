#pragma once
// Plain types shared by the sensor reader, the web API and the host tests.
// No Arduino, no LVGL.
#include <stdint.h>
#include <math.h>

// The board the readings come from. M5 and INA3221 can be found by Auto-detect; the others are chosen on the phone page.
enum OilKind  { OIL_KIND_NONE = 0, OIL_KIND_M5 = 1, OIL_KIND_INA3221 = 2,
                OIL_KIND_ADS1115 = 3,   // a plain ADS1115 breakout, single-ended inputs A0..A3
                OIL_KIND_PAHUB = 4,     // M5 PaHUB (PCA9548A I2C multiplexer) with an M5 voltmeter on each port
                OIL_KIND_PBHUB = 5 };   // M5 PbHUB (STM32F030): its six ports read as analog inputs
enum OilFault {
  OIL_FAULT_NONE = 0,       // reading is good
  OIL_FAULT_NO_SENSOR,      // no sensor board answering on the I2C bus
  OIL_FAULT_WIRING_LOW,     // signal too low  (unplugged / shorted to ground)
  OIL_FAULT_WIRING_HIGH,    // signal too high (shorted to a supply)
  OIL_FAULT_NO_DATA,        // board present but no fresh reading yet / reading went stale
  OIL_FAULT_UNSUPPORTED,    // this channel needs the INA3221 (the M5 Meter has one input)
  OIL_FAULT_OFF             // this channel is not fitted (switched off in the settings)
};

// One channel's verdict. 'val' is in the channel's own unit and valid only when
// fault == OIL_FAULT_NONE:  oil = psi,  boost = psi (negative = vacuum),  AFR = ratio.
struct ChanReading {
  OilFault fault;
  float    val;
  float    volts;           // signal volts (NAN if unknown) - for the serial log and calibration
};
