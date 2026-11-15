// Host-side check of oil_math.h.   g++ -std=c++11 -I.. test_oil_math.cpp -o t && ./t
#include <stdio.h>
#include "oil_math.h"
OilSettings g_set;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static bool near(float a, float b, float t) { return fabsf(a - b) <= t; }

// Reference: the formula exactly as Brain370Z/sensors_task.cpp has it.
static bool ref_psi(float v, float& psi) {
  if (v < 0.30f) return false;
  float bar = fmaxf(0.0f, (v - 0.5f) * 10.0f / (4.5f - 0.5f));
  psi = roundf((bar * 14.5038f * 1.0f + 0.0f) * 10.0f) / 10.0f;
  return true;
}

int main() {
  og_defaults(g_set);
  // sender curve against the brain's formula, across the whole range
  for (float v = 0.30f; v <= 4.89f; v += 0.01f) {
    float a = 0, b = 0;
    OgConv c = og_volts_to_psi(v, a);
    bool r = ref_psi(v, b);
    CHECK(c == OG_CONV_OK && r && a == b, "v=%.2f gauge=%.1f brain=%.1f", v, a, b);
  }
  float p;
  CHECK(og_volts_to_psi(0.5f, p) == OG_CONV_OK && p == 0.0f, "0.5V -> %.1f", p);
  CHECK(og_volts_to_psi(2.5f, p) == OG_CONV_OK && near(p, 72.5f, 0.05f), "2.5V -> %.1f", p);
  CHECK(og_volts_to_psi(4.5f, p) == OG_CONV_OK && near(p, 145.0f, 0.05f), "4.5V -> %.1f", p);
  CHECK(og_volts_to_psi(0.2f, p) == OG_CONV_LOW,  "0.2V must be a LOW fault");
  CHECK(og_volts_to_psi(0.0f, p) == OG_CONV_LOW,  "0V must be a LOW fault");
  CHECK(og_volts_to_psi(4.95f, p) == OG_CONV_HIGH, "4.95V must be a HIGH fault");
  CHECK(og_volts_to_psi(0.35f, p) == OG_CONV_OK && p == 0.0f, "0.35V is OK and clamps to 0, got %.1f", p);

  // ADS1115 path: raw count -> volts at the sender
  CHECK(near(og_ads1115_volts(0, 0.256f), 0.0f, 1e-6f), "ads zero");
  // 2.5 V at the sender = 0.039797 V at the ADC (gain 62.818) -> 0.256 FS range
  int16_t raw = (int16_t)lroundf(2.5f / 62.8181818f / 0.256f * 32768.0f);
  CHECK(near(og_ads1115_volts(raw, 0.256f), 2.5f, 0.01f), "ads 2.5V -> %.3f (raw %d)", og_ads1115_volts(raw, 0.256f), raw);
  CHECK(near(og_ads1115_volts(32767, 0.256f), 0.256f * 62.8181818f * 32767.0f / 32768.0f, 0.01f), "ads full scale");
  CHECK(near(og_ads1115_volts(-100, 4.096f), -100 * 4.096f / 32768.0f * 62.8181818f, 1e-4f), "ads negative");

  // INA3221: 13-bit value in bits 15..3, 8 mV per count
  CHECK(near(og_ina3221_volts(0x09C0), 2.496f, 1e-4f), "ina 0x09C0 -> %.4f", og_ina3221_volts(0x09C0));
  CHECK(near(og_ina3221_volts(0x0008), 0.008f, 1e-5f), "ina one LSB");
  CHECK(near(og_ina3221_volts(0x0007), 0.0f, 1e-6f), "ina low 3 bits are ignored");
  CHECK(near(og_ina3221_volts(0xFFF8), -0.008f, 1e-5f), "ina negative");
  CHECK(near(og_ina3221_volts(0x2710 << 0), 10000/8*0.008f*0 + (0x2710 >> 3) * 0.008f, 1e-3f), "ina big");
  // 4.5 V = 562.5 counts -> 562 -> 4.496 V -> 144.8 psi.  Quantisation, not an error:
  { uint16_t reg = (uint16_t)(562 << 3); float v = og_ina3221_volts(reg);
    og_volts_to_psi(v, p); CHECK(near(p, 144.9f, 0.3f), "ina 4.5V -> %.1f psi", p); }
  // one INA LSB in psi (resolution users should expect)
  { float a, b; og_volts_to_psi(2.000f, a); og_volts_to_psi(2.008f, b);
    printf("INFO  INA3221 step = 8 mV = %.2f psi\n", b - a); }

  // median
  { float v[5] = { 5, 1, 4, 2, 3 }; CHECK(og_median(v, 5) == 3, "median odd"); }
  { float v[1] = { 7 };            CHECK(og_median(v, 1) == 7, "median one"); }
  { float v[9] = { 40, 41, 40, 99, 40, 41, 0, 40, 41 }; CHECK(og_median(v, 9) == 40, "median rejects spikes"); }


  // ======================= boost (MAP) =======================
  // Reference: volt_publisher.py read_map_kpa() + the brain's boost = (map - atmo) * 0.145038
  auto ref_boost = [](float v, float v1, float p1, float v2, float p2, float atm) {
    float kpa = p1 + (v - v1) * (p2 - p1) / (v2 - v1);
    return roundf((kpa - atm) * 0.145038f * 10.0f) / 10.0f;
  };
  og_defaults(g_set);                                   // preset 0 = Bosch-style 3 bar, absolute
  for (float v = 0.10f; v <= 5.25f; v += 0.01f) {
    float a = 0; OgConv c = og_volts_to_boost(v, a);
    CHECK(c == OG_CONV_OK, "boost v=%.2f conv %d", v, c);
    float r = ref_boost(v, 0.25f, 11.0f, 4.75f, 307.0f, 101.3f);
    CHECK(near(a, r, 0.0501f), "boost v=%.2f gauge=%.1f pi=%.1f", v, a, r);
  }
  CHECK(og_volts_to_boost(0.25f, p) == OG_CONV_OK && near(p, -13.1f, 0.06f), "Bosch 0.25 V -> %.2f psi (11 kPa abs = -13.1)", p);
  CHECK(og_volts_to_boost(4.75f, p) == OG_CONV_OK && near(p, 29.8f, 0.06f), "Bosch 4.75 V -> %.2f psi (307 kPa abs = 29.8)", p);
  CHECK(og_volts_to_boost(0.05f, p) == OG_CONV_LOW,  "boost 0.05 V is a LOW fault");
  CHECK(og_volts_to_boost(0.0f, p) == OG_CONV_LOW,   "boost 0 V is a LOW fault");
  CHECK(og_volts_to_boost(5.3f, p) == OG_CONV_HIGH,  "boost 5.3 V is a HIGH fault");
  CHECK(og_volts_to_boost(5.25f, p) == OG_CONV_OK,   "boost 5.25 V still OK (limit is above it)");
  { float ambient_v = 0.25f + (101.3f - 11.0f) / ((307.0f - 11.0f) / 4.5f);
    CHECK(og_volts_to_boost(ambient_v, p) == OG_CONV_OK && near(p, 0.0f, 0.06f), "ambient volts %.4f -> %.2f psi", ambient_v, p); }

  // every preset: sane at its own end points and the right type
  for (int i = 0; i < OG_NBOOST_PRESETS; i++) {
    OilSettings s; og_defaults(s); og_load_boost_preset(s, i); g_set = s;
    char err[96]; CHECK(og_validate(s, err, sizeof err), "boost preset %d '%s': %s", i, OG_BOOST_PRESETS[i].name, err);
    // volts at the preset's second point must give that kPa on the sensor's own scale
    CHECK(near(og_boost_sensor_kpa(OG_BOOST_PRESETS[i].v2), OG_BOOST_PRESETS[i].kpa2, 0.01f), "preset %d kpa2", i);
    CHECK(near(og_boost_sensor_kpa(OG_BOOST_PRESETS[i].v1), OG_BOOST_PRESETS[i].kpa1, 0.01f), "preset %d kpa1", i);
  }
  { OilSettings s; og_defaults(s); og_load_boost_preset(s, 1); g_set = s;      // GM 3 bar: 1.1 kPa @ 0 V, 315.5 @ 5 V
    CHECK(og_volts_to_boost(1.0f, p) == OG_CONV_OK && near(p, (1.1f + 62.88f - 101.3f) * 0.145038f, 0.1f), "GM 1.0 V -> %.2f", p);
    CHECK(og_volts_to_boost(5.0f, p) == OG_CONV_OK && near(p, (315.5f - 101.3f) * 0.145038f, 0.06f), "GM 5.0 V -> %.2f (31 psi)", p); }
  { OilSettings s; og_defaults(s); og_load_boost_preset(s, 5); g_set = s;      // gauge sensor 0.5-4.5 V = 0-30 psi gauge
    CHECK(og_volts_to_boost(0.5f, p) == OG_CONV_OK && near(p, 0.0f, 0.06f), "gauge sensor 0.5 V -> %.2f", p);
    CHECK(og_volts_to_boost(4.5f, p) == OG_CONV_OK && near(p, 30.0f, 0.06f), "gauge sensor 4.5 V -> %.2f", p);
    CHECK(og_volts_to_boost(2.5f, p) == OG_CONV_OK && near(p, 15.0f, 0.06f), "gauge sensor 2.5 V -> %.2f", p);
    float k; CHECK(!og_boost_ambient(1.0f, k), "a gauge sensor has no atmospheric reference to learn"); }
  { OilSettings s; og_defaults(s); s.boost_atm_kpa = 96.0f; g_set = s;          // a hilltop: atmosphere is 96 kPa
    float ambient_v = 0.25f + (96.0f - 11.0f) / ((307.0f - 11.0f) / 4.5f);
    CHECK(og_volts_to_boost(ambient_v, p) == OG_CONV_OK && near(p, 0.0f, 0.06f), "learned atmosphere makes ambient read 0: %.2f", p); }

  // "Zero boost now" only accepts something that looks like atmosphere
  og_defaults(g_set);
  { float k = 0;
    float v_amb = 0.25f + (101.3f - 11.0f) / ((307.0f - 11.0f) / 4.5f);
    CHECK(og_boost_ambient(v_amb, k) && near(k, 101.3f, 0.06f), "ambient accepted: %.2f", k);
    CHECK(!og_boost_ambient(0.6f, k),  "34 kPa (idle vacuum / wrong preset) must be refused");
    CHECK(!og_boost_ambient(3.0f, k),  "192 kPa (on boost) must be refused");
    CHECK(!og_boost_ambient(0.02f, k), "unplugged must be refused");
    CHECK(!og_boost_ambient(5.4f, k),  "over-range must be refused"); }

  // ======================= AFR =======================
  auto ref_afr = [](float v, float vmin, float vmax, float a0, float a1) {     // volt_publisher.py read_afr()
    float f = (v - vmin) / (vmax - vmin); return roundf((a0 + f * (a1 - a0)) * 100.0f) / 100.0f;
  };
  og_defaults(g_set);
  for (float v = 0.15f; v <= 5.25f; v += 0.01f) {
    float a = 0; OgConv c = og_volts_to_afr(v, a);
    CHECK(c == OG_CONV_OK && near(a, ref_afr(v, 0, 5, 10, 20), 0.0101f), "afr v=%.2f gauge=%.2f pi=%.2f", v, a, ref_afr(v, 0, 5, 10, 20));
  }
  CHECK(og_volts_to_afr(2.35f, p) == OG_CONV_OK && near(p, 14.7f, 0.006f), "2.35 V -> %.2f", p);
  CHECK(og_volts_to_afr(0.10f, p) == OG_CONV_LOW,  "AFR 0.10 V is a LOW fault (unplugged)");
  CHECK(og_volts_to_afr(5.3f, p)  == OG_CONV_HIGH, "AFR 5.3 V is a HIGH fault");
  { OilSettings s; og_defaults(s); og_load_afr_preset(s, 1); g_set = s;          // Innovate: 7.35 @ 0 V, 22.39 @ 5 V
    CHECK(og_volts_to_afr(2.5f, p) == OG_CONV_OK && near(p, 7.35f + 2.5f * (22.39f - 7.35f) / 5.0f, 0.006f), "LC-2 2.5 V -> %.2f", p);
    CHECK(og_volts_to_afr(5.0f, p) == OG_CONV_OK && near(p, 22.39f, 0.006f), "LC-2 5 V -> %.2f", p);
    CHECK(og_volts_to_afr(1.0f, p) == OG_CONV_OK && near(p, 10.36f, 0.006f), "LC-2 1 V -> %.2f", p); }
  { OilSettings s; og_defaults(s); og_load_afr_preset(s, 2); g_set = s;          // AEM X-Series: 8.5 @ 0 V, 18.0 @ 5 V
    CHECK(og_volts_to_afr(2.5f, p) == OG_CONV_OK && near(p, 13.25f, 0.006f), "AEM 2.5 V -> %.2f", p);
    CHECK(og_volts_to_afr(5.0f, p) == OG_CONV_OK && near(p, 18.0f, 0.006f), "AEM 5 V -> %.2f", p); }
  for (int i = 0; i < OG_NAFR_PRESETS; i++) {
    OilSettings s; og_defaults(s); og_load_afr_preset(s, i); char err[96];
    CHECK(og_validate(s, err, sizeof err), "afr preset %d '%s': %s", i, OG_AFR_PRESETS[i].name, err);
  }
  // INA3221 step in AFR / boost terms (what users should expect)
  { og_defaults(g_set); float a, b;
    og_volts_to_afr(2.000f, a); og_volts_to_afr(2.008f, b);        printf("INFO  INA3221 step on the 10-20 AFR curve = %.3f AFR\n", b - a);
    og_volts_to_boost(2.000f, a); og_volts_to_boost(2.008f, b);    printf("INFO  INA3221 step on the 3 bar MAP curve = %.3f psi\n", b - a); }

  // ======================= levels =======================
  og_defaults(g_set);
  CHECK(og_oil_level(false, true, 50) == OG_LV_FAULT, "oil fault");
  CHECK(og_oil_level(true, false, 2) == OG_LV_NEUTRAL, "oil not armed: neutral even at 2 psi");
  CHECK(og_oil_level(true, true, 9.9f) == OG_LV_CRITICAL, "oil 9.9 critical");
  CHECK(og_oil_level(true, true, 10.0f) == OG_LV_WARNING, "oil 10.0 warning (critical is BELOW 10)");
  CHECK(og_oil_level(true, true, 19.9f) == OG_LV_WARNING, "oil 19.9 warning");
  CHECK(og_oil_level(true, true, 20.0f) == OG_LV_OK, "oil 20 ok");
  CHECK(og_boost_level(false, 5) == OG_LV_FAULT, "boost fault");
  CHECK(og_boost_level(true, -10) == OG_LV_OK && og_boost_level(true, 17.9f) == OG_LV_OK, "boost ok");
  CHECK(og_boost_level(true, 18.0f) == OG_LV_WARNING && og_boost_level(true, 21.9f) == OG_LV_WARNING, "boost warn at 18");
  CHECK(og_boost_level(true, 22.0f) == OG_LV_CRITICAL, "boost critical at 22");
  // AFR: lean only counts under load (boost sensor at/above the gate, -2 psi by default)
  CHECK(og_afr_level(false, 14, true, 5) == OG_LV_FAULT, "afr fault");
  CHECK(og_afr_level(true, 15.5f, true, -8.0f) == OG_LV_OK, "lean at cruise (-8 psi) is not an alarm");
  CHECK(og_afr_level(true, 13.5f, true, -1.0f) == OG_LV_WARNING, "13.5 under load: warning");
  CHECK(og_afr_level(true, 13.4f, true, -1.0f) == OG_LV_OK, "13.4 exactly is not above the warning");
  CHECK(og_afr_level(true, 14.1f, true, 12.0f) == OG_LV_CRITICAL, "14.1 on boost: critical");
  CHECK(og_afr_level(true, 14.0f, true, 12.0f) == OG_LV_WARNING, "14.0 exactly is warning, not critical");
  CHECK(og_afr_level(true, 12.0f, true, 12.0f) == OG_LV_OK, "12.0 on boost: fine");
  CHECK(og_afr_level(true, 10.5f, true, 12.0f) == OG_LV_INFO, "10.5: very rich, info only");
  CHECK(og_afr_level(true, 10.5f, false, 0.0f) == OG_LV_INFO, "rich info needs no boost sensor");
  CHECK(og_afr_level(true, 15.0f, false, 0.0f) == OG_LV_OK, "boost sensor missing: the lean limits stay off (never guess)");
  g_set.afr_gate_psi = -15.0f;
  CHECK(og_afr_level(true, 15.0f, false, 0.0f) == OG_LV_CRITICAL, "gate = -15: always applies, even with no boost sensor");
  CHECK(og_afr_level(true, 14.7f, true, -12.0f) == OG_LV_CRITICAL, "gate = -15: applies at vacuum too");
  g_set.afr_gate_psi = 5.0f;
  CHECK(og_afr_level(true, 14.5f, true, 4.9f) == OG_LV_OK && og_afr_level(true, 14.5f, true, 5.0f) == OG_LV_CRITICAL, "custom gate at +5 psi");

  printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
  return fails != 0;
}
