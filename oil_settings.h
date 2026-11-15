#pragma once
// ---------------------------------------------------------------------------
// oil_settings.h - the settings that can be changed from the phone page.
//
// oil_config.h now only holds the FACTORY DEFAULTS. Anything saved from the web
// page lives in the screen's flash (NVS) and overrides them. "Reset settings to
// defaults" on the page goes back to oil_config.h.
//
// This header is pure C++ (no Arduino, no LVGL) so the validation, form parsing
// and JSON output can be tested on a PC - see tools/test_oil_settings.cpp.
// ---------------------------------------------------------------------------
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "oil_config.h"

#define OG_SETTINGS_VERSION 3
#define OG_MAX_PAGES        4          // screens that can be put in the swipe rotation
#define OG_PRESET_CUSTOM    255

// The three things the gauge can measure.
enum OgChan { OG_CH_OIL = 0, OG_CH_BOOST = 1, OG_CH_AFR = 2, OG_NCH = 3 };

// A screen is one byte: low nibble = the (top) gauge, high nibble = the bottom
// gauge of a split screen (0 = single gauge). Gauges: 1 oil, 2 boost, 3 AFR.
// 0 = unused slot.
#define OG_PG_OIL   1
#define OG_PG_BOOST 2
#define OG_PG_AFR   3
// Codes from OG_PG_CUSTOM_BASE up are DESIGNED views (made in the layout editor):
// code = base + the view's number in the loaded layout. They are not made of
// gauges, so og_page_top() / og_page_bot() mean nothing for them - test
// og_page_is_custom() first.
#define OG_PG_CUSTOM_BASE   0x40
#define OG_MAX_CUSTOM_VIEWS 32
static inline bool    og_page_is_custom(uint8_t code) { return code >= OG_PG_CUSTOM_BASE && code < OG_PG_CUSTOM_BASE + OG_MAX_CUSTOM_VIEWS; }
static inline int     og_page_custom_view(uint8_t code) { return og_page_is_custom(code) ? (int)(code - OG_PG_CUSTOM_BASE) : -1; }
static inline uint8_t og_page_custom_make(int view) { return (uint8_t)(OG_PG_CUSTOM_BASE + view); }
static inline uint8_t og_page_top(uint8_t code) { return code & 0x0F; }
static inline uint8_t og_page_bot(uint8_t code) { return (uint8_t)(code >> 4); }
static inline uint8_t og_page_make(uint8_t top, uint8_t bot) { return (uint8_t)(top | (bot << 4)); }
static inline const char* og_pg_name(uint8_t g) { return g == 1 ? "Oil" : g == 2 ? "Boost" : g == 3 ? "AFR" : "?"; }

struct OilSettings {
  uint16_t version;
  uint16_t size;
  uint8_t  sensor_mode;        // 0 auto, 1 M5, 2 INA3221, 3 ADS1115, 4 PaHUB + M5 voltmeters, 5 PbHUB
  uint8_t  ina_channel;        // the OIL sender's input: INA3221 channel 1..3 (ADS1115 input 1..4, hub port 1..6)
  uint8_t  units_bar;          // 0 psi, 1 bar   (oil and boost)
  uint8_t  backlight;          // 5..100
  uint8_t  buzzer;             // 0 / 1
  uint8_t  wifi_auto_off_min;  // 0 = never
  uint8_t  boost_ch;           // 0 = not fitted, else the input number (as ina_channel)
  uint8_t  afr_ch;             // 0 = not fitted, else the input number
  uint8_t  boost_preset;       // which preset the boost curve came from (255 = custom)
  uint8_t  afr_preset;
  uint8_t  boost_abs;          // 1 = the sensor reads ABSOLUTE pressure (MAP), 0 = gauge pressure
  uint8_t  pad1;
  uint8_t  pages[OG_MAX_PAGES];
  float m5_scale, m5_offset;           // volts at the sender = raw * scale + offset
  float ina_scale, ina_offset;
  float sender_scale, sender_offset;   // psi = psi * scale + offset
  float v_zero, v_full, bar_full;      // sender: volts at 0 and at full scale, bar at full scale
  float warn_psi, crit_psi, arm_psi;
  // boost: two points (volts -> kPa) define the line; boost = kPa - atm when absolute
  float boost_v1, boost_kpa1, boost_v2, boost_kpa2, boost_atm_kpa;
  float boost_warn_psi, boost_crit_psi;
  // AFR: two points (volts -> AFR)
  float afr_v1, afr_afr1, afr_v2, afr_afr2;
  float afr_stoich, afr_lean_warn, afr_lean_crit, afr_rich_info, afr_gate_psi;
  // ---- added in settings version 3 (everything above is the version-2 layout, unchanged) ----
  uint8_t hub_addr;            // 0 = the usual address for the board (modes 3-5), else the address set on the hardware
  uint8_t pb_adc;              // PbHUB reading: 0 = 10-bit 0-5 V, 1 = 12-bit 0-3.3 V, 2 = 12-bit 0-5 V
  uint8_t pad2, pad3;
  float in_div;                // divider ahead of an ADS1115 / PbHUB input (1.0 = none)
  float adc_scale, adc_offset; // volts = measured * in_div * adc_scale + adc_offset   (ADS1115, PbHUB)
};

extern OilSettings g_set;      // the live settings (defined in oil_settings.cpp)

// ---- presets: the curves people most often have ------------------------------
// These are the sensors' published two-point curves, or - for the generic
// 0.5-4.5 V ones - defined by their stated range, not by a part number.
// Always confirm against your own sensor / controller manual; the phone page
// says so too. Preset 0 of each list is what the Pi's volt_publisher.py uses.
struct OgBoostPreset { const char* name; float v1, kpa1, v2, kpa2; uint8_t abs; };
static const OgBoostPreset OG_BOOST_PRESETS[] = {
  { "Bosch-style 3 bar MAP (same as the Pi)",   0.25f,  11.0f, 4.75f, 307.0f, 1 },
  { "GM 3 bar MAP (12223861)",                  0.00f,   1.1f, 5.00f, 315.5f, 1 },
  { "0.5-4.5 V absolute, 2 bar (0-200 kPa)",    0.50f,   0.0f, 4.50f, 200.0f, 1 },
  { "0.5-4.5 V absolute, 3 bar (0-300 kPa)",    0.50f,   0.0f, 4.50f, 300.0f, 1 },
  { "0.5-4.5 V absolute, 4 bar (0-400 kPa)",    0.50f,   0.0f, 4.50f, 400.0f, 1 },
  { "0.5-4.5 V gauge, 0-30 psi (no vacuum)",    0.50f,   0.0f, 4.50f, 206.84f, 0 },
};
#define OG_NBOOST_PRESETS ((int)(sizeof(OG_BOOST_PRESETS) / sizeof(OG_BOOST_PRESETS[0])))

struct OgAfrPreset { const char* name; float v1, afr1, v2, afr2; };
static const OgAfrPreset OG_AFR_PRESETS[] = {
  { "Linear 0-5 V = AFR 10-20 (same as the Pi)",       0.0f, 10.0f, 5.0f, 20.0f },
  { "Innovate LC-1 / LC-2 / LM-2 default (7.35-22.39)", 0.0f,  7.35f, 5.0f, 22.39f },
  { "AEM X-Series gasoline (8.5-18.0)",                0.0f,  8.5f,  5.0f, 18.0f },
};
#define OG_NAFR_PRESETS ((int)(sizeof(OG_AFR_PRESETS) / sizeof(OG_AFR_PRESETS[0])))

static inline void og_load_boost_preset(OilSettings& s, int i) {
  s.boost_preset = (uint8_t)i;
  s.boost_v1 = OG_BOOST_PRESETS[i].v1; s.boost_kpa1 = OG_BOOST_PRESETS[i].kpa1;
  s.boost_v2 = OG_BOOST_PRESETS[i].v2; s.boost_kpa2 = OG_BOOST_PRESETS[i].kpa2;
  s.boost_abs = OG_BOOST_PRESETS[i].abs;
}
static inline void og_load_afr_preset(OilSettings& s, int i) {
  s.afr_preset = (uint8_t)i;
  s.afr_v1 = OG_AFR_PRESETS[i].v1; s.afr_afr1 = OG_AFR_PRESETS[i].afr1;
  s.afr_v2 = OG_AFR_PRESETS[i].v2; s.afr_afr2 = OG_AFR_PRESETS[i].afr2;
}
static inline bool og_close(float a, float b) { return fabsf(a - b) < 0.0005f; }
static inline bool og_boost_matches_preset(const OilSettings& s, int i) {
  return og_close(s.boost_v1, OG_BOOST_PRESETS[i].v1) && og_close(s.boost_kpa1, OG_BOOST_PRESETS[i].kpa1) &&
         og_close(s.boost_v2, OG_BOOST_PRESETS[i].v2) && og_close(s.boost_kpa2, OG_BOOST_PRESETS[i].kpa2) &&
         s.boost_abs == OG_BOOST_PRESETS[i].abs;
}
static inline bool og_afr_matches_preset(const OilSettings& s, int i) {
  return og_close(s.afr_v1, OG_AFR_PRESETS[i].v1) && og_close(s.afr_afr1, OG_AFR_PRESETS[i].afr1) &&
         og_close(s.afr_v2, OG_AFR_PRESETS[i].v2) && og_close(s.afr_afr2, OG_AFR_PRESETS[i].afr2);
}

static inline void og_defaults(OilSettings& s) {
  memset(&s, 0, sizeof s);
  s.version = OG_SETTINGS_VERSION; s.size = (uint16_t)sizeof(OilSettings);
  s.sensor_mode = OG_SENSOR_MODE;
  s.ina_channel = OG_INA_CHANNEL;
  s.units_bar   = OG_UNITS_BAR_DEFAULT;
  s.backlight   = OG_BACKLIGHT_DEFAULT;
  s.buzzer      = OG_BUZZER_ON_CRIT ? 1 : 0;
  s.wifi_auto_off_min = OG_WIFI_AUTO_OFF_MIN;
  s.m5_scale = OG_M5_CAL_SCALE;   s.m5_offset = OG_M5_CAL_OFFSET;
  s.ina_scale = OG_INA_CAL_SCALE; s.ina_offset = OG_INA_CAL_OFFSET;
  s.sender_scale = OG_SENDER_CAL_SCALE; s.sender_offset = OG_SENDER_CAL_OFFSET;
  s.v_zero = OG_SENDER_V_ZERO; s.v_full = OG_SENDER_V_FULL; s.bar_full = OG_SENDER_BAR_FULL;
  s.warn_psi = OG_WARN_PSI; s.crit_psi = OG_CRIT_PSI; s.arm_psi = OG_ARM_PSI;
  s.boost_ch = OG_BOOST_CHANNEL; s.afr_ch = OG_AFR_CHANNEL;
  og_load_boost_preset(s, 0); og_load_afr_preset(s, 0);
  s.boost_atm_kpa = OG_BOOST_ATM_KPA;
  s.boost_warn_psi = OG_BOOST_WARN_PSI; s.boost_crit_psi = OG_BOOST_CRIT_PSI;
  s.afr_stoich = OG_AFR_STOICH; s.afr_lean_warn = OG_AFR_LEAN_WARN; s.afr_lean_crit = OG_AFR_LEAN_CRIT;
  s.afr_rich_info = OG_AFR_RICH_INFO; s.afr_gate_psi = OG_AFR_GATE_PSI;
  s.pages[0] = og_page_make(OG_PG_OIL, 0);
  s.hub_addr = OG_HUB_ADDR; s.pb_adc = OG_PB_ADC; s.in_div = OG_IN_DIV;
  s.adc_scale = OG_ADC_CAL_SCALE; s.adc_offset = OG_ADC_CAL_OFFSET;
}

// Settings version 2 (v1.3.x - v1.4.0): the same struct without the last fields. Kept so an upgrade carries the
// saved settings over. The new fields were added at the END, so the old bytes are the start of the new struct.
struct OilSettingsV2 {
  uint16_t version, size;
  uint8_t  sensor_mode, ina_channel, units_bar, backlight, buzzer, wifi_auto_off_min, boost_ch, afr_ch,
           boost_preset, afr_preset, boost_abs, pad1;
  uint8_t  pages[OG_MAX_PAGES];
  float m5_scale, m5_offset, ina_scale, ina_offset, sender_scale, sender_offset;
  float v_zero, v_full, bar_full, warn_psi, crit_psi, arm_psi;
  float boost_v1, boost_kpa1, boost_v2, boost_kpa2, boost_atm_kpa, boost_warn_psi, boost_crit_psi;
  float afr_v1, afr_afr1, afr_v2, afr_afr2, afr_stoich, afr_lean_warn, afr_lean_crit, afr_rich_info, afr_gate_psi;
};
static_assert(sizeof(OilSettingsV2) == offsetof(OilSettings, hub_addr), "version 3 must only add fields at the end");
static inline void og_migrate_v2(const OilSettingsV2& o, OilSettings& n) {
  og_defaults(n);                                       // the new fields keep their defaults
  memcpy(&n, &o, sizeof o);                             // the old layout is the front of the new one
  n.version = OG_SETTINGS_VERSION; n.size = (uint16_t)sizeof(OilSettings);
}

// v1.1.0 stored this layout (oil only). Kept so an upgrade carries the saved
// calibration, thresholds and units over instead of resetting them.
struct OilSettingsV1 {
  uint16_t version, size;
  uint8_t  sensor_mode, ina_channel, units_bar, backlight, buzzer, wifi_auto_off_min, pad[2];
  float m5_scale, m5_offset, ina_scale, ina_offset, sender_scale, sender_offset;
  float v_zero, v_full, bar_full, warn_psi, crit_psi, arm_psi;
};
static inline void og_migrate_v1(const OilSettingsV1& o, OilSettings& n) {
  og_defaults(n);
  n.sensor_mode = o.sensor_mode; n.ina_channel = o.ina_channel; n.units_bar = o.units_bar;
  n.backlight = o.backlight; n.buzzer = o.buzzer; n.wifi_auto_off_min = o.wifi_auto_off_min;
  n.m5_scale = o.m5_scale; n.m5_offset = o.m5_offset; n.ina_scale = o.ina_scale; n.ina_offset = o.ina_offset;
  n.sender_scale = o.sender_scale; n.sender_offset = o.sender_offset;
  n.v_zero = o.v_zero; n.v_full = o.v_full; n.bar_full = o.bar_full;
  n.warn_psi = o.warn_psi; n.crit_psi = o.crit_psi; n.arm_psi = o.arm_psi;
}

// How many inputs the chosen board has, what to call them, and which addresses it may use.
static inline uint8_t og_max_input(uint8_t mode) { return mode == 3 ? 4 : mode >= 4 ? 6 : 3; }
static inline const char* og_input_word(uint8_t mode) {
  return mode == 3 ? "ADS1115 input" : mode == 4 ? "PaHUB port" : mode == 5 ? "PbHUB port" : "INA3221 channel";
}
static inline uint8_t og_hub_default_addr(uint8_t mode) { return mode == 3 ? 0x48 : mode == 4 ? 0x70 : mode == 5 ? 0x61 : 0; }
static inline uint8_t og_hub_addr(const OilSettings& s) { return s.hub_addr ? s.hub_addr : og_hub_default_addr(s.sensor_mode); }

// Reject, never silently clamp: a typo on the phone must not quietly become a
// different threshold. err gets a short human-readable reason.
static inline bool og_validate(const OilSettings& s, char* err, size_t n) {
#define OG_BAD(...) do { snprintf(err, n, __VA_ARGS__); return false; } while (0)
#define OG_FIN(x) (isfinite(x))
  if (s.sensor_mode > 5)                    OG_BAD("sensor must be auto, M5, INA3221, ADS1115, PaHUB or PbHUB");
  { const uint8_t mx = og_max_input(s.sensor_mode);
    if (s.ina_channel < 1 || s.ina_channel > mx) {
      if (mx == 3) OG_BAD("INA3221 channel must be 1, 2 or 3");
      OG_BAD("%s for oil pressure must be 1 to %u", og_input_word(s.sensor_mode), (unsigned)mx); } }
  if (s.sensor_mode >= 3 && s.hub_addr) {
    const uint8_t lo = (uint8_t)(og_hub_default_addr(s.sensor_mode)), hi = (uint8_t)(lo + (s.sensor_mode == 3 ? 3 : 7));
    if (s.hub_addr < lo || s.hub_addr > hi) OG_BAD("%s address must be 0x%02X to 0x%02X (or 0 for the usual 0x%02X)",
        s.sensor_mode == 3 ? "ADS1115" : s.sensor_mode == 4 ? "PaHUB" : "PbHUB", (unsigned)lo, (unsigned)hi, (unsigned)lo); }
  if (s.pb_adc > 2)                         OG_BAD("PbHUB reading must be 0, 1 or 2");
  if (!OG_FIN(s.in_div) || s.in_div < 1.0f || s.in_div > 20.0f)          OG_BAD("input divider must be 1.0 to 20.0");
  if (!OG_FIN(s.adc_scale) || s.adc_scale < 0.5f || s.adc_scale > 2.0f)  OG_BAD("ADS1115 / PbHUB scale must be 0.5 to 2.0");
  if (!OG_FIN(s.adc_offset) || fabsf(s.adc_offset) > 2.0f)               OG_BAD("ADS1115 / PbHUB offset must be within +/-2 V");
  if (s.units_bar > 1 || s.buzzer > 1)      OG_BAD("on/off values must be 0 or 1");
  if (s.backlight < 5 || s.backlight > 100) OG_BAD("backlight must be 5 to 100 %%");
  if (s.wifi_auto_off_min > 120)            OG_BAD("Wi-Fi auto-off must be 0 to 120 minutes");
  if (!OG_FIN(s.m5_scale) || s.m5_scale < 0.5f || s.m5_scale > 2.0f)     OG_BAD("M5 scale must be 0.5 to 2.0");
  if (!OG_FIN(s.ina_scale) || s.ina_scale < 0.5f || s.ina_scale > 2.0f)  OG_BAD("INA3221 scale must be 0.5 to 2.0");
  if (!OG_FIN(s.sender_scale) || s.sender_scale < 0.5f || s.sender_scale > 2.0f) OG_BAD("sender scale must be 0.5 to 2.0");
  if (!OG_FIN(s.m5_offset) || fabsf(s.m5_offset) > 2.0f)     OG_BAD("M5 offset must be within +/-2 V");
  if (!OG_FIN(s.ina_offset) || fabsf(s.ina_offset) > 2.0f)   OG_BAD("INA3221 offset must be within +/-2 V");
  if (!OG_FIN(s.sender_offset) || fabsf(s.sender_offset) > 50.0f) OG_BAD("sender offset must be within +/-50 psi");
  // v_zero must stay clear of the 'unplugged / shorted to ground' limit or a
  // genuine 0 psi would be reported as a wiring fault.
  if (!OG_FIN(s.v_zero) || s.v_zero < 0.35f || s.v_zero > 2.0f) OG_BAD("sender zero volts must be 0.35 to 2.0");
  if (!OG_FIN(s.v_full) || s.v_full > 5.0f || s.v_full < s.v_zero + 1.0f) OG_BAD("sender full-scale volts must be at least 1 V above zero, max 5.0");
  if (!OG_FIN(s.bar_full) || s.bar_full < 1.0f || s.bar_full > 50.0f) OG_BAD("sender full-scale pressure must be 1 to 50 bar");
  if (!OG_FIN(s.warn_psi) || s.warn_psi < 1.0f || s.warn_psi > 200.0f) OG_BAD("warning level must be 1 to 200 psi");
  if (!OG_FIN(s.crit_psi) || s.crit_psi < 0.0f || s.crit_psi >= s.warn_psi) OG_BAD("critical level must be below the warning level");
  if (!OG_FIN(s.arm_psi) || s.arm_psi < 1.0f || s.arm_psi > 100.0f) OG_BAD("arm level must be 1 to 100 psi");

  // ---- boost and AFR ----
  { const uint8_t mx = og_max_input(s.sensor_mode); const char* w = og_input_word(s.sensor_mode);
    if (s.boost_ch > mx || s.afr_ch > mx)   OG_BAD("boost / AFR %s must be 0 (not fitted) or 1 to %u", mx == 3 ? "channel" : "input", (unsigned)mx);
    if (s.boost_ch && s.boost_ch == s.ina_channel) OG_BAD("boost is on the same %s as oil pressure", w);
    if (s.afr_ch && s.afr_ch == s.ina_channel)     OG_BAD("AFR is on the same %s as oil pressure", w);
    if (s.boost_ch && s.boost_ch == s.afr_ch)      OG_BAD("boost and AFR are on the same %s", w); }
  if (s.boost_abs > 1)                      OG_BAD("boost sensor type must be absolute (1) or gauge (0)");
  if (s.boost_preset != OG_PRESET_CUSTOM && s.boost_preset >= OG_NBOOST_PRESETS) OG_BAD("unknown boost preset");
  if (s.afr_preset != OG_PRESET_CUSTOM && s.afr_preset >= OG_NAFR_PRESETS)       OG_BAD("unknown AFR preset");
  if (!OG_FIN(s.boost_v1) || s.boost_v1 < 0.0f || s.boost_v1 > 5.0f)            OG_BAD("boost curve: first volts must be 0 to 5");
  if (!OG_FIN(s.boost_v2) || s.boost_v2 > 5.5f || s.boost_v2 < s.boost_v1 + 0.5f) OG_BAD("boost curve: second volts must be at least 0.5 V above the first, max 5.5");
  if (!OG_FIN(s.boost_kpa1) || fabsf(s.boost_kpa1) > 2000.0f)                   OG_BAD("boost curve: first kPa out of range");
  if (!OG_FIN(s.boost_kpa2) || fabsf(s.boost_kpa2) > 2000.0f || s.boost_kpa2 < s.boost_kpa1 + 10.0f) OG_BAD("boost curve: second kPa must be at least 10 above the first");
  if (!OG_FIN(s.boost_atm_kpa) || s.boost_atm_kpa < 70.0f || s.boost_atm_kpa > 110.0f) OG_BAD("atmospheric pressure must be 70 to 110 kPa");
  if (!OG_FIN(s.boost_warn_psi) || s.boost_warn_psi < 0.0f || s.boost_warn_psi > 60.0f) OG_BAD("boost warning must be 0 to 60 psi");
  if (!OG_FIN(s.boost_crit_psi) || s.boost_crit_psi <= s.boost_warn_psi || s.boost_crit_psi > 80.0f) OG_BAD("boost critical must be above the boost warning, max 80 psi");
  if (!OG_FIN(s.afr_v1) || s.afr_v1 < 0.0f || s.afr_v1 > 5.0f)                  OG_BAD("AFR curve: first volts must be 0 to 5");
  if (!OG_FIN(s.afr_v2) || s.afr_v2 > 5.5f || s.afr_v2 < s.afr_v1 + 0.5f)        OG_BAD("AFR curve: second volts must be at least 0.5 V above the first, max 5.5");
  if (!OG_FIN(s.afr_afr1) || s.afr_afr1 < 3.0f || s.afr_afr1 > 40.0f)           OG_BAD("AFR curve: first AFR must be 3 to 40");
  if (!OG_FIN(s.afr_afr2) || s.afr_afr2 > 40.0f || s.afr_afr2 < s.afr_afr1 + 2.0f) OG_BAD("AFR curve: second AFR must be at least 2 above the first, max 40");
  if (!OG_FIN(s.afr_stoich) || s.afr_stoich < 5.0f || s.afr_stoich > 20.0f)      OG_BAD("stoichiometric AFR must be 5 to 20");
  if (!OG_FIN(s.afr_lean_warn) || s.afr_lean_warn < 8.0f || s.afr_lean_warn > 25.0f) OG_BAD("AFR lean warning must be 8 to 25");
  if (!OG_FIN(s.afr_lean_crit) || s.afr_lean_crit <= s.afr_lean_warn || s.afr_lean_crit > 30.0f) OG_BAD("AFR lean critical must be above the lean warning, max 30");
  if (!OG_FIN(s.afr_rich_info) || s.afr_rich_info < 6.0f || s.afr_rich_info >= s.afr_lean_warn) OG_BAD("AFR rich level must be 6 or more and below the lean warning");
  if (!OG_FIN(s.afr_gate_psi) || s.afr_gate_psi < -15.0f || s.afr_gate_psi > 30.0f) OG_BAD("AFR lean-limit gate must be -15 to 30 psi");

  // ---- screens ----
  { int used = 0;
    for (int i = 0; i < OG_MAX_PAGES; i++) {
      const uint8_t c = s.pages[i];
      if (!c) continue;
      used++;
      if (og_page_is_custom(c)) continue;      // a designed view: whether it exists is checked against the loaded layout
      const uint8_t t = og_page_top(c), b = og_page_bot(c);
      if (t < 1 || t > 3 || b > 3 || (b && b == t)) OG_BAD("screen %d is not a valid layout", i + 1);
      const uint8_t g[2] = { t, b };
      for (int k = 0; k < 2; k++) {
        if (g[k] == OG_PG_BOOST && !s.boost_ch) OG_BAD("screen %d shows Boost, but Boost has no input (it is switched off)", i + 1);
        if (g[k] == OG_PG_AFR   && !s.afr_ch)   OG_BAD("screen %d shows AFR, but AFR has no input (it is switched off)", i + 1);
      }
    }
    if (!used) OG_BAD("at least one screen must be set"); }
#undef OG_FIN
#undef OG_BAD
  return true;
}

// Sensor-affecting fields changed? (then the sensor module must re-detect and
// restart its smoothing)
static inline bool og_sensor_group_changed(const OilSettings& a, const OilSettings& b) {
  return a.sensor_mode != b.sensor_mode || a.ina_channel != b.ina_channel ||
         a.boost_ch != b.boost_ch || a.afr_ch != b.afr_ch ||
         a.m5_scale != b.m5_scale || a.m5_offset != b.m5_offset ||
         a.ina_scale != b.ina_scale || a.ina_offset != b.ina_offset ||
         a.hub_addr != b.hub_addr || a.pb_adc != b.pb_adc || a.in_div != b.in_div ||
         a.adc_scale != b.adc_scale || a.adc_offset != b.adc_offset ||
         a.sender_scale != b.sender_scale || a.sender_offset != b.sender_offset ||
         a.v_zero != b.v_zero || a.v_full != b.v_full || a.bar_full != b.bar_full ||
         a.boost_abs != b.boost_abs || a.boost_v1 != b.boost_v1 || a.boost_kpa1 != b.boost_kpa1 ||
         a.boost_v2 != b.boost_v2 || a.boost_kpa2 != b.boost_kpa2 || a.boost_atm_kpa != b.boost_atm_kpa ||
         a.afr_v1 != b.afr_v1 || a.afr_afr1 != b.afr_afr1 || a.afr_v2 != b.afr_v2 || a.afr_afr2 != b.afr_afr2;
}

// ---- field table: one place that knows every field's name and type ----------
struct OgField { const char* name; uint8_t is_float; size_t off; };
#define OG_F(n, f, t) { n, t, offsetof(OilSettings, f) }
static const OgField OG_FIELDS[] = {
  OG_F("sensor_mode", sensor_mode, 0),   OG_F("ina_channel", ina_channel, 0),
  OG_F("units_bar", units_bar, 0),       OG_F("backlight", backlight, 0),
  OG_F("buzzer", buzzer, 0),             OG_F("wifi_auto_off_min", wifi_auto_off_min, 0),
  OG_F("m5_scale", m5_scale, 1),         OG_F("m5_offset", m5_offset, 1),
  OG_F("ina_scale", ina_scale, 1),       OG_F("ina_offset", ina_offset, 1),
  OG_F("sender_scale", sender_scale, 1), OG_F("sender_offset", sender_offset, 1),
  OG_F("v_zero", v_zero, 1),             OG_F("v_full", v_full, 1),
  OG_F("bar_full", bar_full, 1),         OG_F("warn_psi", warn_psi, 1),
  OG_F("crit_psi", crit_psi, 1),         OG_F("arm_psi", arm_psi, 1),
  OG_F("boost_ch", boost_ch, 0),         OG_F("afr_ch", afr_ch, 0),
  OG_F("boost_preset", boost_preset, 0), OG_F("afr_preset", afr_preset, 0),
  OG_F("boost_abs", boost_abs, 0),
  OG_F("hub_addr", hub_addr, 0),         OG_F("pb_adc", pb_adc, 0),
  OG_F("in_div", in_div, 1),             OG_F("adc_scale", adc_scale, 1),
  OG_F("adc_offset", adc_offset, 1),
  { "page1", 0, offsetof(OilSettings, pages) + 0 }, { "page2", 0, offsetof(OilSettings, pages) + 1 },
  { "page3", 0, offsetof(OilSettings, pages) + 2 }, { "page4", 0, offsetof(OilSettings, pages) + 3 },
  OG_F("boost_v1", boost_v1, 1),         OG_F("boost_kpa1", boost_kpa1, 1),
  OG_F("boost_v2", boost_v2, 1),         OG_F("boost_kpa2", boost_kpa2, 1),
  OG_F("boost_atm_kpa", boost_atm_kpa, 1),
  OG_F("boost_warn_psi", boost_warn_psi, 1), OG_F("boost_crit_psi", boost_crit_psi, 1),
  OG_F("afr_v1", afr_v1, 1),             OG_F("afr_afr1", afr_afr1, 1),
  OG_F("afr_v2", afr_v2, 1),             OG_F("afr_afr2", afr_afr2, 1),
  OG_F("afr_stoich", afr_stoich, 1),     OG_F("afr_lean_warn", afr_lean_warn, 1),
  OG_F("afr_lean_crit", afr_lean_crit, 1), OG_F("afr_rich_info", afr_rich_info, 1),
  OG_F("afr_gate_psi", afr_gate_psi, 1),
};
#undef OG_F
#define OG_NFIELDS (sizeof(OG_FIELDS) / sizeof(OG_FIELDS[0]))

typedef const char* (*OgArgFn)(const char* name, void* ctx);   // NULL = not supplied

// Build a candidate from the supplied form fields. Fields not supplied keep
// their current value. A field that is supplied but is not a clean number
// fails the whole request - nothing is half-applied.
static inline bool og_apply_form(OilSettings& s, OgArgFn get, void* ctx, char* err, size_t n) {
  for (size_t i = 0; i < OG_NFIELDS; i++) {
    const char* v = get(OG_FIELDS[i].name, ctx);
    if (!v) continue;
    char* end = nullptr;
    if (!*v) { snprintf(err, n, "%s is empty", OG_FIELDS[i].name); return false; }
    if (OG_FIELDS[i].is_float) {
      float f = strtof(v, &end);
      if (*end || !isfinite(f)) { snprintf(err, n, "%s is not a number", OG_FIELDS[i].name); return false; }
      *(float*)((uint8_t*)&s + OG_FIELDS[i].off) = f;
    } else {
      long l = strtol(v, &end, 10);
      if (*end || l < 0 || l > 255) { snprintf(err, n, "%s must be a whole number 0 to 255", OG_FIELDS[i].name); return false; }
      *((uint8_t*)&s + OG_FIELDS[i].off) = (uint8_t)l;
    }
  }
  // Presets. Choosing one on its own (no curve fields sent) loads its curve. If a
  // curve was sent too, the curve wins and the preset id is kept only if the curve
  // really is that preset - otherwise it becomes Custom, so the label never lies.
  {
    const bool bcurve = get("boost_v1", ctx) || get("boost_kpa1", ctx) || get("boost_v2", ctx) ||
                        get("boost_kpa2", ctx) || get("boost_abs", ctx);
    if (get("boost_preset", ctx) && !bcurve && s.boost_preset < OG_NBOOST_PRESETS) og_load_boost_preset(s, s.boost_preset);
    else if (s.boost_preset < OG_NBOOST_PRESETS && !og_boost_matches_preset(s, s.boost_preset)) s.boost_preset = OG_PRESET_CUSTOM;
    const bool acurve = get("afr_v1", ctx) || get("afr_afr1", ctx) || get("afr_v2", ctx) || get("afr_afr2", ctx);
    if (get("afr_preset", ctx) && !acurve && s.afr_preset < OG_NAFR_PRESETS) og_load_afr_preset(s, s.afr_preset);
    else if (s.afr_preset < OG_NAFR_PRESETS && !og_afr_matches_preset(s, s.afr_preset)) s.afr_preset = OG_PRESET_CUSTOM;
  }
  return og_validate(s, err, n);
}

// {"name":value,...} for the page. Returns the length, or -1 if it did not fit.
static inline int og_settings_json(const OilSettings& s, char* buf, size_t n) {
  size_t p = 0;
  int w = snprintf(buf, n, "{"); if (w < 0 || (size_t)w >= n) return -1; p += w;
  for (size_t i = 0; i < OG_NFIELDS; i++) {
    const uint8_t* base = (const uint8_t*)&s + OG_FIELDS[i].off;
    if (OG_FIELDS[i].is_float) w = snprintf(buf + p, n - p, "%s\"%s\":%.4f", i ? "," : "", OG_FIELDS[i].name, (double)*(const float*)base);
    else                       w = snprintf(buf + p, n - p, "%s\"%s\":%u",   i ? "," : "", OG_FIELDS[i].name, (unsigned)*base);
    if (w < 0 || (size_t)w >= n - p) return -1;
    p += w;
  }
  w = snprintf(buf + p, n - p, "}"); if (w < 0 || (size_t)w >= n - p) return -1;
  return (int)(p + w);
}

// The preset lists for the page, so there is one copy of the numbers (here).
// {"boost":[{"n":..,"v1":..,"k1":..,"v2":..,"k2":..,"abs":..},..],"afr":[{"n":..,"v1":..,"a1":..,"v2":..,"a2":..},..]}
static inline int og_presets_json(char* buf, size_t n) {
  size_t p = 0; int w;
#define OG_APP(...) do { w = snprintf(buf + p, n - p, __VA_ARGS__); if (w < 0 || (size_t)w >= n - p) return -1; p += w; } while (0)
  OG_APP("{\"boost\":[");
  for (int i = 0; i < OG_NBOOST_PRESETS; i++)
    OG_APP("%s{\"n\":\"%s\",\"v1\":%.4f,\"k1\":%.4f,\"v2\":%.4f,\"k2\":%.4f,\"abs\":%u}", i ? "," : "",
           OG_BOOST_PRESETS[i].name, (double)OG_BOOST_PRESETS[i].v1, (double)OG_BOOST_PRESETS[i].kpa1,
           (double)OG_BOOST_PRESETS[i].v2, (double)OG_BOOST_PRESETS[i].kpa2, (unsigned)OG_BOOST_PRESETS[i].abs);
  OG_APP("],\"afr\":[");
  for (int i = 0; i < OG_NAFR_PRESETS; i++)
    OG_APP("%s{\"n\":\"%s\",\"v1\":%.4f,\"a1\":%.4f,\"v2\":%.4f,\"a2\":%.4f}", i ? "," : "",
           OG_AFR_PRESETS[i].name, (double)OG_AFR_PRESETS[i].v1, (double)OG_AFR_PRESETS[i].afr1,
           (double)OG_AFR_PRESETS[i].v2, (double)OG_AFR_PRESETS[i].afr2);
  OG_APP("]}");
#undef OG_APP
  return (int)p;
}

// Wi-Fi name / password rules (WPA2 needs 8..63 printable ASCII).
static inline bool og_wifi_valid(const char* ssid, const char* pass, bool pass_required, char* err, size_t n) {
  size_t ls = strlen(ssid);
  if (ls < 1 || ls > 32) { snprintf(err, n, "Wi-Fi name must be 1 to 32 characters"); return false; }
  for (size_t i = 0; i < ls; i++) if ((unsigned char)ssid[i] < 0x20 || (unsigned char)ssid[i] > 0x7E) {
    snprintf(err, n, "Wi-Fi name: plain letters, numbers and symbols only"); return false; }
  size_t lp = strlen(pass);
  if (lp == 0 && !pass_required) return true;          // blank = keep current
  if (lp < 8 || lp > 63) { snprintf(err, n, "password must be 8 to 63 characters"); return false; }
  for (size_t i = 0; i < lp; i++) if ((unsigned char)pass[i] < 0x20 || (unsigned char)pass[i] > 0x7E) {
    snprintf(err, n, "password: plain letters, numbers and symbols only"); return false; }
  return true;
}

// (implemented in oil_settings.cpp; Arduino build only)
void OilSettings_Load();               // call once at start-up
bool OilSettings_Save();               // false = flash write failed (caller must say so)
void OilSettings_ResetToDefaults();    // back to oil_config.h, and forget the saved copy

// Has the Wi-Fi been unused for longer than the auto-off setting? 0 minutes = never.
// 'last_act' can be a few milliseconds AFTER 'now' when a request was handled
// since 'now' was read; a plain unsigned subtraction would then wrap to ~4.29
// billion and report "idle for 49 days". The signed difference makes that case
// "idle for 0 ms". (Idle spans over ~24 days are not distinguished - irrelevant
// for a setting capped at 120 minutes.)
static inline bool og_idle_expired(uint32_t now, uint32_t last_act, uint8_t minutes) {
  if (minutes == 0) return false;
  int32_t idle = (int32_t)(now - last_act);
  if (idle < 0) return false;
  return (uint32_t)idle > (uint32_t)minutes * 60000UL;
}
