#pragma once
// ---------------------------------------------------------------------------
// oil_math.h - the pure arithmetic of the gauge (oil, boost, AFR): no Arduino, no I2C, no
// LVGL. Kept separate so it can be compiled and tested on a PC (see
// tools/test_oil_math.cpp), and so the maths that decides what a driver is
// told about their oil pressure is in one small readable place.
//
// Ported from Brain370Z/sensors_task.cpp (oil_read_volts / oil_volts_to_psi),
// which in turn is oil_publisher.py.
// ---------------------------------------------------------------------------
#include <math.h>
#include <stdint.h>
#include "oil_config.h"
#include "oil_settings.h"      // g_set: the live (phone-editable) settings

enum OgConv { OG_CONV_OK = 0, OG_CONV_LOW = 1, OG_CONV_HIGH = 2 };

// ADS1115 raw count -> volts at the sender (undoes the M5 unit's input divider).
static inline float og_ads1115_volts(int16_t raw, float full_scale_v) {
  return raw * (full_scale_v / 32768.0f) * OG_M5_INPUT_GAIN * g_set.m5_scale + g_set.m5_offset;
}

// ADS1115 breakout, single-ended input, +/-6.144 V range (0.1875 mV per count) -> volts at the sender
// (undoes the divider in front of the input, then the trim).
static inline float og_ads_single_volts(int16_t raw) {
  return raw * (6.144f / 32768.0f) * g_set.in_div * g_set.adc_scale + g_set.adc_offset;
}

// PbHUB analog value -> volts at the sender. g_set.pb_adc says what the value means: 0 = 10-bit over 0-5 V
// (M5's documentation), 1 = 12-bit over 0-3.3 V, 2 = 12-bit over 0-5 V.
static inline float og_pbhub_volts(uint16_t raw) {
  const float v = g_set.pb_adc == 0 ? raw * (5.0f / 1024.0f) : g_set.pb_adc == 1 ? raw * (3.3f / 4096.0f) : raw * (5.0f / 4096.0f);
  return v * g_set.in_div * g_set.adc_scale + g_set.adc_offset;
}
// A 10-bit setting cannot give more than 1023: a bigger number means the hub really reads 12-bit.
static inline bool og_pbhub_too_big_for_10bit(uint16_t raw) { return g_set.pb_adc == 0 && raw > 1100; }

// INA3221 bus-voltage register -> volts. The 16-bit register holds a 13-bit
// value in bits 15..3, 8 mV per count. Arithmetic shift keeps the sign.
static inline float og_ina3221_volts(uint16_t reg) {
  int16_t s = (int16_t)reg;
  s = (int16_t)(s >> 3);
  return s * 0.008f * g_set.ina_scale + g_set.ina_offset;
}

// Sender volts -> psi. Returns OG_CONV_LOW / OG_CONV_HIGH (and leaves psi
// untouched) when the voltage is outside what a healthy sender can produce:
// that is a wiring fault, not a pressure.
static inline OgConv og_volts_to_psi(float v, float& psi) {
  if (v < OG_FAULT_BELOW_V) return OG_CONV_LOW;
  if (v > g_set.v_full + OG_FAULT_ABOVE_MARGIN_V) return OG_CONV_HIGH;
  float bar = fmaxf(0.0f, (v - g_set.v_zero) * g_set.bar_full / (g_set.v_full - g_set.v_zero));
  psi = roundf((bar * OG_PSI_PER_BAR * g_set.sender_scale + g_set.sender_offset) * 10.0f) / 10.0f;
  return OG_CONV_OK;
}

// Median of n values (n >= 1). Sorts the array in place.
static inline float og_median(float* v, int n) {
  for (int i = 1; i < n; i++) {                 // insertion sort, n is tiny
    float k = v[i]; int j = i - 1;
    while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; }
    v[j + 1] = k;
  }
  return v[n / 2];
}

// ---------------------------------------------------------------------------
// Boost (MAP sensor) and AFR (wideband controller). Both are a straight line
// through two (volts, value) points, set on the phone page - the same two-point
// calibration volt_publisher.py uses on the Pi.
// ---------------------------------------------------------------------------

// Pressure on the sensor's own scale (kPa, absolute or gauge as the sensor is).
static inline float og_boost_sensor_kpa(float v) {
  return g_set.boost_kpa1 + (v - g_set.boost_v1) * (g_set.boost_kpa2 - g_set.boost_kpa1) / (g_set.boost_v2 - g_set.boost_v1);
}

// Sensor volts -> boost in psi (negative = vacuum). A powered MAP sensor never sits
// near 0 V, and never above its 5 V supply: outside that it is a wiring fault.
static inline OgConv og_volts_to_boost(float v, float& psi) {
  if (v < OG_BOOST_FAULT_BELOW_V) return OG_CONV_LOW;
  if (v > OG_AUX_FAULT_ABOVE_V)   return OG_CONV_HIGH;
  float kpa = og_boost_sensor_kpa(v);
  if (g_set.boost_abs) kpa -= g_set.boost_atm_kpa;
  psi = roundf(kpa * OG_KPA_TO_PSI * 10.0f) / 10.0f;
  return OG_CONV_OK;
}

// "Zero boost now": an ABSOLUTE sensor with the engine off reads atmospheric pressure.
// Accept it only if it looks like atmosphere (a wrong preset or a running engine would
// not): returns false and leaves kpa_out alone otherwise.
static inline bool og_boost_ambient(float v, float& kpa_out) {
  if (!g_set.boost_abs) return false;
  if (v < OG_BOOST_FAULT_BELOW_V || v > OG_AUX_FAULT_ABOVE_V) return false;
  float kpa = og_boost_sensor_kpa(v);
  if (!(kpa >= 80.0f && kpa <= 110.0f)) return false;
  kpa_out = roundf(kpa * 10.0f) / 10.0f;
  return true;
}

static inline OgConv og_volts_to_afr(float v, float& afr) {
  if (v < OG_AFR_FAULT_BELOW_V) return OG_CONV_LOW;
  if (v > OG_AUX_FAULT_ABOVE_V) return OG_CONV_HIGH;
  float a = g_set.afr_afr1 + (v - g_set.afr_v1) * (g_set.afr_afr2 - g_set.afr_afr1) / (g_set.afr_v2 - g_set.afr_v1);
  afr = roundf(a * 100.0f) / 100.0f;
  return OG_CONV_OK;
}

// ---------------------------------------------------------------------------
// Levels (colour / alert) for each gauge. Pure, so the rules are tested.
// ---------------------------------------------------------------------------
enum OgLevel { OG_LV_NEUTRAL = 0, OG_LV_OK, OG_LV_INFO, OG_LV_WARNING, OG_LV_CRITICAL, OG_LV_FAULT };

// Oil: neutral until the engine has demonstrably run (armed), then below crit / warn.
static inline OgLevel og_oil_level(bool good, bool armed, float psi) {
  if (!good)              return OG_LV_FAULT;
  if (!armed)             return OG_LV_NEUTRAL;
  if (psi < g_set.crit_psi) return OG_LV_CRITICAL;
  if (psi < g_set.warn_psi) return OG_LV_WARNING;
  return OG_LV_OK;
}

static inline OgLevel og_boost_level(bool good, float psi) {
  if (!good)                       return OG_LV_FAULT;
  if (psi >= g_set.boost_crit_psi) return OG_LV_CRITICAL;
  if (psi >= g_set.boost_warn_psi) return OG_LV_WARNING;
  return OG_LV_OK;
}

// Lean is only dangerous under load. There is no throttle input, so "under load" is
// manifold pressure at or above afr_gate_psi (-15 = always). If the gate needs the boost
// sensor and it is missing / faulted, the lean limits stay off rather than guess.
static inline bool og_afr_under_load(bool boost_good, float boost_psi) {
  if (g_set.afr_gate_psi <= -14.95f) return true;
  return boost_good && boost_psi >= g_set.afr_gate_psi;
}
static inline OgLevel og_afr_level(bool good, float afr, bool boost_good, float boost_psi) {
  if (!good) return OG_LV_FAULT;
  if (og_afr_under_load(boost_good, boost_psi)) {
    if (afr > g_set.afr_lean_crit) return OG_LV_CRITICAL;
    if (afr > g_set.afr_lean_warn) return OG_LV_WARNING;
  }
  if (afr < g_set.afr_rich_info) return OG_LV_INFO;
  return OG_LV_OK;
}
