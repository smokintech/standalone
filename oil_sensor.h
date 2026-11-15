#pragma once
#include <Arduino.h>
#include "oil_settings.h"      // OgChan
#include "oil_types.h"         // OilKind, OilFault, ChanReading

void         OilSensor_Begin();            // after board_i2c_begin(); finds the board
void         OilSensor_Poll();             // call from loop() on every pass; never blocks
ChanReading  OilSensor_Get(OgChan ch);     // the latest verdict for one channel
OilKind      OilSensor_Kind();
void         OilSensor_Reconfigure();      // after settings change: re-detect + restart smoothing
void         OilSensor_RestartSmoothing(); // only forget the smoothed values (e.g. after "zero boost"); the board is kept
const char*  OilSensor_Name();             // e.g. "M5 METER 0x49" / "INA3221 0x40 CH1" / "NONE"
