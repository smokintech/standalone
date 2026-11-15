// Saves / loads the phone-editable settings in the screen's flash (NVS).
// Wi-Fi name and password are NOT stored here - they have their own namespace
// (see oil_web.cpp) so "reset Wi-Fi settings" cannot touch these, and "reset
// settings to defaults" cannot touch the Wi-Fi.
#include <Arduino.h>
#include <Preferences.h>
#include "oil_settings.h"

OilSettings g_set;

void OilSettings_Load() {
  og_defaults(g_set);
  Preferences p;
  bool have = false;
  if (p.begin("oilcfg", true)) {
    const size_t len = p.getBytesLength("set");
    char err[96];
    if (len == sizeof(OilSettingsV2)) {
      // v1.3 - v1.4.0: the same settings without the ADS1115 / hub fields; they start at their defaults.
      OilSettingsV2 o; OilSettings t;
      p.getBytes("set", &o, sizeof o);
      if (o.version == 2 && o.size == sizeof o) {
        og_migrate_v2(o, t);
        if (og_validate(t, err, sizeof err)) {
          g_set = t; have = true;
          Serial.println("[set] upgraded saved settings (ADS1115 / hub options start at their defaults)");
        } else {
          Serial.printf("[set] old saved settings rejected (%s) - using defaults\n", err);
        }
      }
    } else if (len == sizeof(OilSettings)) {
      OilSettings t;
      p.getBytes("set", &t, sizeof t);
      if (t.version == OG_SETTINGS_VERSION && t.size == sizeof t && og_validate(t, err, sizeof err)) {
        g_set = t; have = true;
        Serial.println("[set] loaded saved settings");
      } else {
        Serial.printf("[set] saved settings rejected (%s) - using defaults\n",
                      t.version == OG_SETTINGS_VERSION ? err : "unknown version");
      }
    } else if (len == sizeof(OilSettingsV1)) {
      // v1.1.0 (oil only): keep its calibration, thresholds and units; boost / AFR start off.
      OilSettingsV1 o; OilSettings t;
      p.getBytes("set", &o, sizeof o);
      if (o.version == 1 && o.size == sizeof o) {
        og_migrate_v1(o, t);
        if (og_validate(t, err, sizeof err)) {
          g_set = t; have = true;
          Serial.println("[set] upgraded saved v1.1.0 settings (boost / AFR off until set on the phone page)");
        } else {
          Serial.printf("[set] old saved settings rejected (%s) - using defaults\n", err);
        }
      }
    } else if (len) {
      Serial.printf("[set] saved settings have an unexpected size (%u bytes) - using defaults\n", (unsigned)len);
    }
    p.end();
  }
  if (!have) {
    // v1.0.0 kept just units and backlight in another namespace: carry them over.
    Preferences old;
    if (old.begin("oilgauge", true)) {
      if (old.isKey("bar")) g_set.units_bar = old.getUChar("bar", g_set.units_bar) ? 1 : 0;
      if (old.isKey("bl"))  { uint8_t b = old.getUChar("bl", g_set.backlight); if (b >= 5 && b <= 100) g_set.backlight = b; }
      old.end();
    }
    Serial.println("[set] no saved settings - using factory defaults");
  }
}

bool OilSettings_Save() {
  Preferences p;
  if (!p.begin("oilcfg", false)) { Serial.println("[set] SAVE FAILED: cannot open flash storage"); return false; }
  size_t n = p.putBytes("set", &g_set, sizeof g_set);
  p.end();
  if (n != sizeof g_set) { Serial.println("[set] SAVE FAILED: write incomplete"); return false; }
  return true;
}

void OilSettings_ResetToDefaults() {
  og_defaults(g_set);
  Preferences p;
  if (p.begin("oilcfg", false)) { p.remove("set"); p.end(); }
}
