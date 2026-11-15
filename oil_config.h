#pragma once
// ---------------------------------------------------------------------------
// oil_config.h - the FACTORY DEFAULTS for the standalone gauge (oil pressure,
// plus optional boost and AFR screens on the other two INA3221 channels).
// Most of these can be changed from the phone settings page (swipe up on the
// dial to switch the gauge's Wi-Fi on); what you save there is kept in the
// screen's flash and overrides the value here. "Reset settings to defaults" on
// the page comes back to this file. Values marked >>> CHECK <<< should be
// confirmed for your car before you rely on the gauge.
//
// Where a value comes from your existing system it carries the same number and
// name logic as Brain370Z/config.h and Gauge_Config.h, so the standalone gauge
// reads the same as the brain + screens did.
// ---------------------------------------------------------------------------

#define OG_VERSION "v1.5.0"

// ===========================================================================
// WHICH SENSOR BOARD IS CONNECTED
// ===========================================================================
// I2C is the Waveshare board's own bus: SDA = GPIO15, SCL = GPIO7 (set in
// board_pins.h). The touch chip, IO expander, IMU and RTC share this bus; none
// of them use 0x40-0x43 or 0x49, so there is no clash.
//
//   0 = AUTO   look for either board at start-up (and re-look every 2 s if
//              neither is found, so a late-connected board is picked up)
//   1 = M5 Meter ISOLATED (ADS1115 @ 0x49) only
//   2 = Adafruit INA3221 only
//   3 = a plain ADS1115 breakout (A0..A3 single-ended)     - chosen on the phone page, never auto-detected
//   4 = M5 PaHUB with an M5 voltmeter on each of its ports - chosen on the phone page, never auto-detected
//   5 = M5 PbHUB: its six ports as analog inputs           - chosen on the phone page, never auto-detected
#define OG_SENSOR_MODE      0

// If AUTO finds BOTH boards, prefer this one (1 = M5, 2 = INA3221).
#define OG_SENSOR_PREFER    1

// Bus speed. 100 kHz is the safe choice with a hand-made lead to the sensor
// board. The touch chip and everything else on the bus work fine at 100 kHz.
#define OG_I2C_HZ           100000

// ---- M5 Meter ISOLATED (ADS1115) -- same as Brain370Z/config.h ------------
#define OG_M5_ADDR          0x49
// Input scaling of the M5 unit's front end. Reading is
//   volts = raw * (full_scale / 32768) * OG_M5_INPUT_GAIN
#define OG_M5_INPUT_GAIN    62.8181818f
// >>> CHECK <<< your trim from ~/.m5_vmeter_cal.json on the Pi 4 (scale, offset).
// 1.0 / 0.0 = uncalibrated.
#define OG_M5_CAL_SCALE     1.0f
#define OG_M5_CAL_OFFSET    0.0f

// ---- Adafruit INA3221 -----------------------------------------------------
// Address by the A0 solder pad / pin: GND = 0x40 (Adafruit default), VS = 0x41,
// SDA = 0x42, SCL = 0x43. Auto-detect tries all four.
#define OG_INA_ADDR         0x40
// Which of the three channels the sender is wired to (1, 2 or 3).
// The sender signal goes to that channel's Vin- terminal (see README).
#define OG_INA_CHANNEL      1
// Trim for the INA3221 reading itself (1.0 / 0.0 = none). Check against a
// multimeter on the sender wire and adjust if it is out.
#define OG_INA_CAL_SCALE    1.0f
#define OG_INA_CAL_OFFSET   0.0f

// ---- ADS1115 breakout / M5 PaHUB / M5 PbHUB (modes 3, 4, 5) ----------------
// Address of the hub or ADC. 0 = the usual one: ADS1115 0x48 (0x48-0x4B), PaHUB 0x70 (0x70-0x77), PbHUB 0x61 (0x61-0x68).
#define OG_HUB_ADDR         0
// What the PbHUB's analog value means: 0 = 10-bit, 0-5 V (M5's documentation), 1 = 12-bit, 0-3.3 V, 2 = 12-bit, 0-5 V.
#define OG_PB_ADC           0
// Voltage divider in front of an ADS1115 / PbHUB input: sender volts = measured volts * this. 1.0 = none.
// (An ADS1115 on 3.3 V can only measure up to 3.3 V, so a 0.5-4.5 V sender needs a divider, e.g. 2:1 = 2.0.)
#define OG_IN_DIV           1.0f
// Trim for the ADS1115 / PbHUB reading (1.0 / 0.0 = none).
#define OG_ADC_CAL_SCALE    1.0f
#define OG_ADC_CAL_OFFSET   0.0f

// ===========================================================================
// OIL PRESSURE SENDER  -- same as Brain370Z/config.h
// ===========================================================================
// Ratiometric 0.5 V .. 4.5 V sender, 0 .. 10 bar.
#define OG_SENDER_V_ZERO    0.5f
#define OG_SENDER_V_FULL    4.5f
#define OG_SENDER_BAR_FULL  10.0f
// >>> CHECK <<< your trim from ~/.oil_sender_cal.json on the Pi 4.
// psi_final = psi * SCALE + OFFSET   (1.0 / 0.0 = uncalibrated)
#define OG_SENDER_CAL_SCALE  1.0f
#define OG_SENDER_CAL_OFFSET 0.0f

// Below this the sender is treated as unplugged / shorted to ground. Shown as
// CHECK WIRING - never as a fake 0 psi.
#define OG_FAULT_BELOW_V    0.30f
// Above (sender full-scale volts + this margin) the signal is shorted to the
// sender's 5 V feed (a healthy 0.5-4.5 V sender never exceeds 4.5 V, so the
// limit is 4.9 V). Also shown as CHECK WIRING.
#define OG_FAULT_ABOVE_MARGIN_V 0.40f

// Sampling: OG_SAMPLES readings per cycle, median, then smoothed.
#define OG_SAMPLES          9
#define OG_SMOOTH           0.4f     // 1.0 = no smoothing, lower = smoother
#define OG_INTERVAL_MS      250      // one published reading per cycle
// No fresh reading for this long -> the gauge shows NO DATA, not the old value.
#define OG_STALE_MS         1500
// This many consecutive failed I2C transfers -> sensor treated as lost.
#define OG_LOST_AFTER_ERRS  4

// ===========================================================================
// DISPLAY / ALERTS -- same numbers as Gauge_Config.h on the three screens
// ===========================================================================
#define OG_DIAL_MAX_PSI     120.0f   // full scale, psi
#define OG_DIAL_MAX_BAR     8.0f     // full scale, bar (8 bar = 116 psi)
#define OG_PSI_PER_BAR      14.5038f
#define OG_UNITS_BAR_DEFAULT 0       // 0 = psi, 1 = bar (tap to change, saved)

#define OG_WARN_PSI         20.0f    // amber below this
#define OG_CRIT_PSI         10.0f    // red + alert below this

// There is no RPM here, so the gauge cannot tell "engine off" from "oil
// pressure lost" by RPM like the brain does. Instead the low-pressure alarm is
// ARMED once pressure has been seen at or above this - i.e. the engine has
// demonstrably run since power-up. Before that the dial stays neutral, so a
// key-on / engine-off car does not alarm. It stays armed until power is removed.
#define OG_ARM_PSI          15.0f
// Must stay below the critical level this long before the alert fires, so one
// bad reading cannot trigger it.
#define OG_ALERT_DEBOUNCE_MS 1500

// Piezo buzzer on the board (IO-expander pin 8). Beeps while the CRITICAL
// alert is active; a tap on the screen silences it.  0 = never beep.
#define OG_BUZZER_ON_CRIT   1

// Backlight, 5..100 %. Set from the phone page (it is no longer a swipe: swiping
// left / right now changes screen).
#define OG_BACKLIGHT_DEFAULT 70

// Touch: a press that moves less than this many pixels is a tap.
#define OG_TAP_MAX_PX       25
// A press that moves at least this far in one direction is a swipe.
#define OG_SWIPE_MIN_PX     60

// ===========================================================================
// WI-FI SETTINGS PAGE
// ===========================================================================
// The gauge is its own hotspot (no home network needed). It is OFF at power-up:
// swipe UP on the dial to switch it on, swipe DOWN to switch it off. The name
// and address are shown on the dial while it is on; the PASSWORD IS NEVER SHOWN
// (so nobody looking at the car can read it). The name is OilGauge-XXXX
// (XXXX is different on each gauge) and the password is the one below; change
// both from the page after first connecting.
// Hold a finger on the dial for 5 s to reset the Wi-Fi name/password only.
//
// Wi-Fi switches itself off after this many minutes with no use of the page
// (0 = never). Keeping it off while driving also keeps the display's memory bus
// free of Wi-Fi traffic.
#define OG_WIFI_DEFAULT_PASS  "JLbFc1h4"      // factory password, 8 to 63 characters
#define OG_WIFI_AUTO_OFF_MIN  15
// Refuse to switch Wi-Fi on if free internal RAM is below this (bytes) - better
// a visible "LOW MEMORY" than a crash on a screen you are driving with.
#define OG_WIFI_MIN_HEAP      45000
// Hold this long (ms) without moving to get the "reset Wi-Fi settings?" prompt.
#define OG_LONGPRESS_MS       5000

// ===========================================================================
// BOOST AND AFR SCREENS  (INA3221 only - the M5 Meter has a single input)
// ===========================================================================
// Which INA3221 channel each sensor is wired to: 0 = not fitted, 1..3 = channel.
// Oil is on OG_INA_CHANNEL above; the three must all be different. Each signal
// goes to that channel's Vin- terminal with a COMMON GROUND, exactly like oil.
#define OG_BOOST_CHANNEL    0
#define OG_AFR_CHANNEL      0

// Starting curves are preset 0 of each list in oil_settings.h, which are the
// same numbers as the Pi's volt_publisher.py: a Bosch-style 3-bar MAP sensor
// (11 kPa @ 0.25 V ... 307 kPa @ 4.75 V) and a linear 0-5 V wideband mapped to
// AFR 10-20. >>> CHECK <<< both against YOUR sensor and controller: pick the
// matching preset (or Custom) on the phone page.
#define OG_KPA_TO_PSI       0.145038f
#define OG_BOOST_ATM_KPA    101.3f     // sea-level atmosphere, used to turn an ABSOLUTE MAP reading into boost.
                                       // "Zero boost now" on the phone page replaces it with the real value.

// Wiring-fault limits, as volt_publisher.py: below these a powered sensor never
// sits (an unplugged input floats near 0 V), above OG_AUX_FAULT_ABOVE_V the
// signal is shorted to something higher than a 5 V sensor can produce.
#define OG_BOOST_FAULT_BELOW_V 0.10f
#define OG_AFR_FAULT_BELOW_V   0.15f
#define OG_AUX_FAULT_ABOVE_V   5.25f

// Boost: same dial and limits as Gauge_Config.h on the three screens.
#define OG_BOOST_DIAL_MIN_PSI  -15.0f
#define OG_BOOST_DIAL_MAX_PSI   30.0f
#define OG_BOOST_DIAL_MIN_BAR   -1.0f
#define OG_BOOST_DIAL_MAX_BAR    2.0f
#define OG_BOOST_WARN_PSI       18.0f    // amber at or above
#define OG_BOOST_CRIT_PSI       22.0f    // red at or above - set to YOUR safe limit

// AFR: same dial and limits as Gauge_Config.h.
#define OG_AFR_DIAL_MIN        10.0f
#define OG_AFR_DIAL_MAX        20.0f
#define OG_AFR_STOICH          14.7f
#define OG_AFR_LEAN_WARN       13.4f     // above this UNDER LOAD: amber
#define OG_AFR_LEAN_CRIT       14.0f     // above this UNDER LOAD: red
#define OG_AFR_RICH_INFO       11.0f     // below this: shown in blue, not an alarm
// The full app only applies the lean limits above 55 % throttle. This gauge has
// no throttle input, so it uses manifold pressure instead: the limits apply
// while the boost sensor reads at or above this (psi, vacuum is negative).
// -2 psi is roughly 87 kPa absolute - well above any cruise, and it works on a
// naturally aspirated engine as well as a boosted one. -15 = no gate (always
// apply). With the boost sensor missing or faulted the lean limits stay off.
#define OG_AFR_GATE_PSI        -2.0f
