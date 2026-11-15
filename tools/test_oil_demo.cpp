// Host test for the "Run demo" waveforms and request parsing (oil_demo.h).
//   g++ -std=c++17 -O1 -I.. test_oil_demo.cpp -o /tmp/test_oil_demo && /tmp/test_oil_demo
#include <stdio.h>
#include <math.h>
#include "oil_demo.h"
#include "oil_math.h"

OilSettings g_set;
static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main() {
  og_defaults(g_set);
  // every reading valid, inside the dial ranges, never NaN
  float omin = 1e9, omax = -1e9, bmin = 1e9, bmax = -1e9, amin = 1e9, amax = -1e9, maxjump_o = 0, maxjump_b = 0;
  ChanReading prev[OG_NCH]; bool have = false;
  bool saw_warn = false, saw_crit = false, saw_ok = false, saw_high_boost = false, saw_lean = false;
  bool armed = false;
  for (uint32_t t = 0; t <= OG_DEMO_MS; t += 100) {
    ChanReading r[OG_NCH]; og_demo_readings(t, r);
    for (int c = 0; c < OG_NCH; c++) CHECK(r[c].fault == OIL_FAULT_NONE && isfinite(r[c].val), "t=%u channel %d is not a good number", (unsigned)t, c);
    omin = fminf(omin, r[OG_CH_OIL].val);   omax = fmaxf(omax, r[OG_CH_OIL].val);
    bmin = fminf(bmin, r[OG_CH_BOOST].val); bmax = fmaxf(bmax, r[OG_CH_BOOST].val);
    amin = fminf(amin, r[OG_CH_AFR].val);   amax = fmaxf(amax, r[OG_CH_AFR].val);
    if (have) { maxjump_o = fmaxf(maxjump_o, fabsf(r[OG_CH_OIL].val - prev[OG_CH_OIL].val)); maxjump_b = fmaxf(maxjump_b, fabsf(r[OG_CH_BOOST].val - prev[OG_CH_BOOST].val)); }
    for (int c = 0; c < OG_NCH; c++) prev[c] = r[c]; have = true;
    // what the screens would make of it (the same level rules the gauge uses)
    if (r[OG_CH_OIL].val >= g_set.arm_psi) armed = true;
    const OgLevel lo = og_oil_level(true, armed, r[OG_CH_OIL].val);
    if (lo == OG_LV_CRITICAL) saw_crit = true; else if (lo == OG_LV_WARNING) saw_warn = true; else if (armed) saw_ok = true;
    if (og_boost_level(true, r[OG_CH_BOOST].val) >= OG_LV_WARNING) saw_high_boost = true;
    if (og_afr_level(true, r[OG_CH_AFR].val, true, r[OG_CH_BOOST].val) == OG_LV_CRITICAL) saw_lean = true;
  }
  printf("oil %.1f..%.1f psi (max step %.2f per 100 ms), boost %.1f..%.1f psi (max step %.2f), AFR %.2f..%.2f\n",
         (double)omin, (double)omax, (double)maxjump_o, (double)bmin, (double)bmax, (double)maxjump_b, (double)amin, (double)amax);
  CHECK(omin >= 0 && omax <= 100, "oil stays inside its dial (0-100 psi)");
  CHECK(bmin >= -15 && bmax <= 30, "boost stays inside its dial (-15..30 psi)");
  CHECK(amin >= 10 && amax <= 20, "AFR stays inside its dial (10-20)");
  CHECK(omin <= 1.0f && omax >= 90.0f, "oil sweeps nearly its whole range");
  CHECK(bmin < -8.0f && bmax > 20.0f, "boost goes from vacuum to high boost");
  CHECK(maxjump_o < 2.5f && maxjump_b < 2.5f, "readings glide, they do not jump");
  CHECK(saw_ok, "the oil alert arms and the demo shows a healthy reading");
  CHECK(saw_warn, "the demo shows the low-pressure warning level");
  CHECK(saw_crit, "the demo shows the critical level");
  CHECK(saw_high_boost, "the demo reaches a boost warning level");
  CHECK(saw_lean, "the demo reaches a lean AFR warning");

  // starts at the quiet end, repeats exactly every cycle
  ChanReading a[OG_NCH], b[OG_NCH];
  og_demo_readings(0, a); CHECK(a[OG_CH_OIL].val == 0.0f, "oil starts at 0 (engine off)");
  og_demo_readings(36000u * 3, b); CHECK(b[OG_CH_OIL].val == a[OG_CH_OIL].val, "oil repeats every 36 s");

  // time left
  CHECK(og_demo_left_s(false, 0) == 0, "not running = 0 s left");
  CHECK(og_demo_left_s(true, 0) == (int)(OG_DEMO_MS / 1000), "just started = full time");
  CHECK(og_demo_left_s(true, 1) == (int)(OG_DEMO_MS / 1000), "rounded up");
  CHECK(og_demo_left_s(true, OG_DEMO_MS - 1) == 1, "1 ms left shows 1 s");
  CHECK(og_demo_left_s(true, OG_DEMO_MS) == 0 && og_demo_left_s(true, OG_DEMO_MS + 5000) == 0, "time up = 0");

  // request parsing
  CHECK(og_demo_parse("1") == 1 && og_demo_parse("0") == 0, "1 starts, 0 stops");
  CHECK(og_demo_parse(nullptr) == -1 && og_demo_parse("") == -1 && og_demo_parse("2") == -1 && og_demo_parse("11") == -1 &&
        og_demo_parse("on") == -1 && og_demo_parse("-1") == -1, "anything else is refused");

  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
