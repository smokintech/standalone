// ---------------------------------------------------------------------------
// OilGauge370Z - standalone oil pressure gauge, with optional boost and AFR screens
//
// Board : Waveshare ESP32-S3-Touch-LCD-2.1 (480x480 round, CST820 touch)
// Sensor: M5 Meter ISOLATED (ADS1115 @ 0x49, oil only)  or  Adafruit INA3221 (oil, boost, AFR)
//         on the board's I2C bus: SDA = GPIO15, SCL = GPIO7
//
// No brain, no Pi: the senders are read directly and shown on the dial(s).
// Factory defaults are in oil_config.h; settings are changed from a phone over
// the gauge's own Wi-Fi (swipe up on the dial). See README.txt.
// ---------------------------------------------------------------------------
#include "board_i2c.h"
#include "board_expander.h"
#include "board_panel.h"
#include "board_touch.h"
#include "board_lvgl.h"
#include "oil_config.h"
#include "oil_settings.h"
#include "oil_sensor.h"
#include "oil_ui.h"
#include "oil_web.h"
#include "oil_layout.h"

void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.println("\n\n########## OIL GAUGE " OG_VERSION "  " __DATE__ " " __TIME__ " ##########");
  Serial.printf("[mem] internal RAM free at boot: %u bytes\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  OilSettings_Load();               // saved phone settings (or the defaults in oil_config.h)

  if (!board_i2c_begin()) Serial.println("[i2c] bus failed to start");   // SDA=GPIO15, SCL=GPIO7
  if (!board_expander_begin()) Serial.println("[board] IO expander did not answer (panel and buzzer need it)");

  OilSensor_Begin();                // find the M5 / INA3221

  board_panel_begin();              // panel reset, set-up commands, RGB output
  board_touch_begin();
  board_backlight_begin(50);        // the UI sets the saved brightness a moment later
  board_lvgl_begin();
  OilLayout_Begin();                // designed views stored on the gauge (if any)
  OilUI_Begin();
  OilWeb_Begin();                   // Wi-Fi stays OFF until you swipe up on the dial
  Serial.printf("[mem] internal RAM free after UI: %u bytes (Wi-Fi needs about %u)\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)OG_WIFI_MIN_HEAP);
  Serial.println(">>> setup done");
}

static uint32_t last_ui = 0, last_log = 0;

void loop()
{
  board_lvgl_poll();                // LVGL timers + touch (same task as the sensor: one user of Wire)
  OilSensor_Poll();                 // non-blocking, one short I2C step per pass
  OilWeb_Poll();                    // phone page requests (only when Wi-Fi is on)

  uint32_t now = millis();
  if (now - last_ui >= 100) {
    last_ui = now;
    OilUI_Update();
  }
  if (now - last_log >= 2000) {     // one line every 2 s: what the gauge is seeing
    last_log = now;
    char line[160]; int n = 0;
    const ChanReading o = OilSensor_Get(OG_CH_OIL), bo = OilSensor_Get(OG_CH_BOOST), af = OilSensor_Get(OG_CH_AFR);
    if (o.fault == OIL_FAULT_NONE) n += snprintf(line + n, sizeof line - n, "oil %.1f psi (%.3f V)", (double)o.val, (double)o.volts);
    else                           n += snprintf(line + n, sizeof line - n, "oil FAULT %d", (int)o.fault);
    if (bo.fault != OIL_FAULT_OFF) {
      if (bo.fault == OIL_FAULT_NONE) n += snprintf(line + n, sizeof line - n, "  | boost %.1f psi (%.3f V)", (double)bo.val, (double)bo.volts);
      else                            n += snprintf(line + n, sizeof line - n, "  | boost FAULT %d", (int)bo.fault);
    }
    if (af.fault != OIL_FAULT_OFF) {
      if (af.fault == OIL_FAULT_NONE) n += snprintf(line + n, sizeof line - n, "  | AFR %.2f (%.3f V)", (double)af.val, (double)af.volts);
      else                            n += snprintf(line + n, sizeof line - n, "  | AFR FAULT %d", (int)af.fault);
    }
    Serial.printf("[gauge] %s   %s\n", line, OilSensor_Name());
  }
  vTaskDelay(pdMS_TO_TICKS(5));
}
