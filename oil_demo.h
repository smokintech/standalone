#pragma once
// ---------------------------------------------------------------------------
// oil_demo.h - the "Run demo" waveforms. No Arduino, no LVGL: the host tests use it too.
//
// The demo feeds invented readings to the screens so every gauge, zone, light and alert can be
// seen working with no engine running. It is deliberately hard to leave on by accident:
//   * it only starts when asked (phone page), and ends by itself after OG_DEMO_MS;
//   * the screen shows DEMO the whole time;
//   * a REAL low-oil alarm (engine has really run, real pressure really low) ends it at once;
//   * what the demo "armed" never carries over - the real engine-has-run latch is put back.
// ---------------------------------------------------------------------------
#include <stdint.h>
#include <math.h>
#include "oil_types.h"
#include "oil_settings.h"

#define OG_DEMO_MS 120000u            // two minutes, then it stops by itself

// A smooth to-and-fro between lo and hi, one full cycle per period_ms, starting at lo.
static inline float og_demo_wave(uint32_t t_ms, uint32_t period_ms, float lo, float hi, uint32_t offset_ms = 0) {
  const float ph = (float)((t_ms + offset_ms) % period_ms) / (float)period_ms;        // 0..1
  const float c  = 0.5f - 0.5f * cosf(ph * 6.2831853f);                                // 0 -> 1 -> 0, smooth at both ends
  return lo + (hi - lo) * c;
}

// Readings at t ms after the demo started. Three different periods, so over a couple of minutes every
// combination turns up: oil goes from nothing, through the low-pressure warning, to full pressure
// and back; boost from vacuum to high boost; AFR from rich to lean.
static inline void og_demo_readings(uint32_t t_ms, ChanReading r[OG_NCH]) {
  r[OG_CH_OIL]   = { OIL_FAULT_NONE, floorf(og_demo_wave(t_ms, 36000,   0.0f, 95.0f) * 10.0f + 0.5f) / 10.0f, NAN };
  r[OG_CH_BOOST] = { OIL_FAULT_NONE, floorf(og_demo_wave(t_ms, 24000, -12.0f, 28.0f, 6000) * 10.0f + 0.5f) / 10.0f, NAN };
  r[OG_CH_AFR]   = { OIL_FAULT_NONE, floorf(og_demo_wave(t_ms, 30000,  10.5f, 18.5f, 9000) * 100.0f + 0.5f) / 100.0f, NAN };
}

// Seconds left (rounded up), 0 when not running.
static inline int og_demo_left_s(bool on, uint32_t elapsed_ms) {
  if (!on || elapsed_ms >= OG_DEMO_MS) return 0;
  return (int)((OG_DEMO_MS - elapsed_ms + 999u) / 1000u);
}

// The "on" field of the demo request: "1" starts, "0" stops, anything else is refused (-1).
static inline int og_demo_parse(const char* v) {
  if (v && v[0] == '1' && !v[1]) return 1;
  if (v && v[0] == '0' && !v[1]) return 0;
  return -1;
}
