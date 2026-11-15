// Host-side check of oil_settings.h.   g++ -std=c++11 -I.. test_oil_settings.cpp -o t && ./t
#include <stdio.h>
#include <map>
#include <string>
#include "oil_settings.h"
#include "oil_api.h"
OilSettings g_set;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef std::map<std::string, std::string> Form;
static const char* getArg(const char* n, void* c) { Form* f = (Form*)c; auto it = f->find(n); return it == f->end() ? NULL : it->second.c_str(); }
static bool apply(OilSettings& s, Form f, char* err) { return og_apply_form(s, getArg, &f, err, 96); }

int main() {
  char err[96] = "";
  OilSettings d; og_defaults(d);
  CHECK(og_validate(d, err, sizeof err), "defaults must validate: %s", err);
  CHECK(d.size == sizeof(OilSettings) && d.version == OG_SETTINGS_VERSION, "header");

  // each rule rejects its own bad value
  struct { const char* name; const char* bad; } bads[] = {
    {"sensor_mode","6"},{"ina_channel","0"},{"ina_channel","4"},{"units_bar","2"},{"buzzer","2"},
    {"backlight","4"},{"backlight","101"},{"wifi_auto_off_min","121"},
    {"m5_scale","0.4"},{"m5_scale","2.1"},{"ina_scale","0"},{"sender_scale","3"},
    {"m5_offset","2.5"},{"ina_offset","-2.5"},{"sender_offset","51"},
    {"v_zero","0.2"},{"v_zero","2.5"},{"v_full","5.1"},{"v_full","1.0"},{"bar_full","0.5"},{"bar_full","51"},
    {"warn_psi","0.5"},{"warn_psi","201"},{"crit_psi","-1"},{"crit_psi","20"},{"crit_psi","25"},
    {"arm_psi","0.5"},{"arm_psi","101"},
    {"pb_adc","3"},{"in_div","0.9"},{"in_div","21"},{"adc_scale","0.4"},{"adc_scale","2.1"},{"adc_offset","2.5"},{"adc_offset","-2.5"},
  };
  for (auto& b : bads) {
    OilSettings s = d; Form f; f[b.name] = b.bad;
    CHECK(!apply(s, f, err), "%s=%s should be rejected", b.name, b.bad);
  }
  // values that must be accepted
  { OilSettings s = d; Form f; f["warn_psi"]="25"; f["crit_psi"]="12.5"; f["arm_psi"]="18"; f["backlight"]="100";
    CHECK(apply(s, f, err), "good thresholds: %s", err); CHECK(s.warn_psi == 25 && s.crit_psi == 12.5f && s.arm_psi == 18 && s.backlight == 100, "applied"); }
  // not clean numbers
  const char* junk[] = { "abc", "1.2.3", "5x", "nan", "inf", "-inf", "", " ", "1e999" };
  for (auto j : junk) { OilSettings s = d; Form f; f["warn_psi"] = j; CHECK(!apply(s, f, err), "warn_psi='%s' should be rejected", j); }
  { OilSettings s = d; Form f; f["backlight"] = "300"; CHECK(!apply(s, f, err), "300 for a u8"); }
  { OilSettings s = d; Form f; f["backlight"] = "12.5"; CHECK(!apply(s, f, err), "12.5 for a u8"); }
  { OilSettings s = d; Form f; f["backlight"] = "-5"; CHECK(!apply(s, f, err), "-5 for a u8"); }
  // partial form keeps the rest; unknown fields are ignored
  { OilSettings s = d; Form f; f["units_bar"]="1"; f["bogus"]="9"; CHECK(apply(s, f, err), "%s", err);
    CHECK(s.units_bar == 1 && s.warn_psi == d.warn_psi && s.m5_scale == d.m5_scale, "partial apply"); }
  // one bad field fails the lot: nothing half-applied
  { OilSettings s = d; Form f; f["units_bar"]="1"; f["warn_psi"]="oops";
    bool ok = apply(s, f, err); CHECK(!ok, "mixed good/bad must fail");
    CHECK(strstr(err, "warn_psi"), "error should name the field: %s", err); }
  // cross-field: crit must stay under warn even when only warn changes
  { OilSettings s = d; Form f; f["warn_psi"]="9"; CHECK(!apply(s, f, err), "warn below existing crit"); }

  // JSON
  { char js[4096]; int n = og_settings_json(d, js, sizeof js);
    CHECK(n > 0 && js[0]=='{' && js[n-1]=='}', "json shape: %s", js);
    CHECK(strstr(js, "\"warn_psi\":20.0000") && strstr(js, "\"sensor_mode\":0") && strstr(js, "\"v_full\":4.5000"), "json content: %s", js);
    CHECK(!strstr(js, ":nan") && !strstr(js, ":inf") && !strstr(js, ":-nan") && !strstr(js, ":-inf"), "no nan/inf values");
    char tiny[20]; CHECK(og_settings_json(d, tiny, sizeof tiny) == -1, "too-small buffer must report failure");
    printf("INFO  settings JSON is %d bytes\n", n); }

  // sensor group
  { OilSettings a = d, b = d; CHECK(!og_sensor_group_changed(a, b), "same");
    b.backlight = 50; b.warn_psi = 30; CHECK(!og_sensor_group_changed(a, b), "display/alert fields do not restart the sensor");
    b.m5_scale = 1.01f; CHECK(og_sensor_group_changed(a, b), "cal change restarts the sensor"); }

  // wifi names / passwords
  CHECK(og_wifi_valid("OilGauge-1A2B", "k7m2x9qaab", true, err, 96), "%s", err);
  CHECK(!og_wifi_valid("", "k7m2x9qaab", true, err, 96), "empty ssid");
  CHECK(!og_wifi_valid("12345678901234567890123456789012x", "k7m2x9qaab", true, err, 96), "33-char ssid");
  CHECK(og_wifi_valid("12345678901234567890123456789012", "k7m2x9qaab", true, err, 96), "32-char ssid ok");
  CHECK(!og_wifi_valid("ok", "short", true, err, 96), "7-char password");
  CHECK(og_wifi_valid("ok", "12345678", true, err, 96), "8-char password ok");
  CHECK(!og_wifi_valid("ok", "", true, err, 96), "blank password when required");
  CHECK(og_wifi_valid("ok", "", false, err, 96), "blank password = keep");
  CHECK(!og_wifi_valid("ok\n", "12345678", true, err, 96), "control char in ssid");
  CHECK(!og_wifi_valid("ok", "pass\x01word", true, err, 96), "control char in password");
  { std::string p(64, 'a'); CHECK(!og_wifi_valid("ok", p.c_str(), true, err, 96), "64-char password"); }

  // Wi-Fi idle timer. The regression: last_act stamped AFTER 'now' was read.
  CHECK(!og_idle_expired(1000, 1000, 15), "just switched on");
  CHECK(!og_idle_expired(1000, 1003, 15), "REGRESSION: request handled after 'now' was read (last_act ahead of now)");
  CHECK(!og_idle_expired(155347, 155350, 15), "the 155 s case from the real log");
  CHECK(!og_idle_expired(20000, 1000, 15), "19 s idle, limit 15 min");
  CHECK(!og_idle_expired(1000 + 15 * 60000, 1000, 15), "exactly at the limit is not yet expired");
  CHECK(og_idle_expired(1000 + 15 * 60000 + 1, 1000, 15), "one ms past the limit expires");
  CHECK(!og_idle_expired(4000000000u, 1000, 0), "0 = never");
  CHECK(!og_idle_expired(5, 4294967000u, 15), "millis() wrap: last_act just before wrap, now just after");
  CHECK(og_idle_expired(5 + 16 * 60000u, 4294967000u, 15), "millis() wrap, genuinely idle 16 min");
  CHECK(!og_idle_expired(60000, 1, 1) && og_idle_expired(60002, 1, 1), "1 minute setting");

  // ======================= boost / AFR / screens (v2) =======================
  // each new rule rejects its own bad value
  struct { const char* name; const char* bad; } bads2[] = {
    {"boost_ch","4"},{"afr_ch","4"},{"boost_abs","2"},{"boost_preset","6"},{"boost_preset","254"},{"afr_preset","3"},
    {"boost_v1","-0.1"},{"boost_v1","5.1"},{"boost_v2","5.6"},{"boost_v2","0.5"},{"boost_kpa1","2001"},{"boost_kpa2","5"},
    {"boost_atm_kpa","69"},{"boost_atm_kpa","111"},{"boost_warn_psi","-1"},{"boost_warn_psi","61"},
    {"boost_crit_psi","18"},{"boost_crit_psi","81"},
    {"afr_v1","-1"},{"afr_v1","5.5"},{"afr_v2","0.3"},{"afr_afr1","2.9"},{"afr_afr2","9.9"},{"afr_afr2","41"},
    {"afr_stoich","4.9"},{"afr_stoich","20.1"},{"afr_lean_warn","7.9"},{"afr_lean_warn","25.1"},
    {"afr_lean_crit","13.4"},{"afr_lean_crit","31"},{"afr_rich_info","5.9"},{"afr_rich_info","13.4"},
    {"afr_gate_psi","-15.1"},{"afr_gate_psi","30.1"},
    {"page1","0"},                // would leave no screen at all
    {"page1","49"},               // 0x31 = oil on top, AFR below - but AFR is switched off
    {"page1","4"},{"page1","17"}, // gauge 4 does not exist; oil over oil
  };
  for (auto& b : bads2) {
    OilSettings s = d; Form f; f[b.name] = b.bad;
    CHECK(!apply(s, f, err), "%s=%s should be rejected", b.name, b.bad);
  }
  // channel clashes
  { OilSettings s = d; Form f; f["boost_ch"]="1"; CHECK(!apply(s, f, err), "boost on the oil channel"); CHECK(strstr(err, "oil"), "says why: %s", err); }
  { OilSettings s = d; Form f; f["afr_ch"]="1"; CHECK(!apply(s, f, err), "AFR on the oil channel"); }
  { OilSettings s = d; Form f; f["boost_ch"]="2"; f["afr_ch"]="2"; CHECK(!apply(s, f, err), "boost and AFR on one channel"); }
  { OilSettings s = d; Form f; f["ina_channel"]="2"; f["boost_ch"]="2"; CHECK(!apply(s, f, err), "moving oil onto boost's channel"); }
  // a screen may not show a gauge that is switched off
  { OilSettings s = d; Form f; f["page2"] = "2"; CHECK(!apply(s, f, err), "boost screen with boost off"); CHECK(strstr(err, "screen 2") && strstr(err, "Boost"), "names the screen: %s", err); }
  { OilSettings s = d; Form f; f["page2"] = "3"; CHECK(!apply(s, f, err), "AFR screen with AFR off"); }
  { OilSettings s = d; Form f; f["page2"] = "18"; CHECK(!apply(s, f, err), "split oil/boost with boost off"); }
  { OilSettings s = d; Form f; f["boost_ch"]="2"; f["page2"]="2"; CHECK(apply(s, f, err), "enabled together with its screen: %s", err); CHECK(s.pages[1] == 2, "stored"); }
  // switching a gauge off while a screen still uses it is refused, not silently fixed
  { OilSettings s = d; Form f; f["boost_ch"]="2"; f["page2"]="2"; CHECK(apply(s, f, err), "%s", err);
    Form g; g["boost_ch"]="0"; CHECK(!apply(s, g, err), "boost switched off while screen 2 uses it"); }
  // the whole wished-for set-up: oil, boost, AFR, and three splits
  { OilSettings s = d; Form f;
    f["boost_ch"]="2"; f["afr_ch"]="3";
    f["page1"]="1"; f["page2"]="2"; f["page3"]="3";
    f["page4"]=std::to_string(og_page_make(OG_PG_BOOST, OG_PG_AFR));           // boost on top, AFR below
    CHECK(apply(s, f, err), "full set-up: %s", err);
    CHECK(og_page_top(s.pages[3]) == OG_PG_BOOST && og_page_bot(s.pages[3]) == OG_PG_AFR, "split decoded");
    Form g; g["page4"]=std::to_string(og_page_make(OG_PG_OIL, OG_PG_BOOST));    // oil top, boost bottom
    CHECK(apply(s, g, err), "%s", err);
    Form h; h["page4"]=std::to_string(og_page_make(OG_PG_BOOST, OG_PG_OIL));    // boost top, oil bottom
    CHECK(apply(s, h, err), "%s", err);
    Form z; z["page2"]="0"; z["page3"]="0"; z["page4"]="0"; CHECK(apply(s, z, err), "unused slots are fine while one screen remains: %s", err);
    Form n; n["page1"]="0"; CHECK(!apply(s, n, err), "no screens at all is refused"); }
  CHECK(og_page_make(2, 3) == 0x32 && og_page_top(0x32) == 2 && og_page_bot(0x32) == 3, "page code layout");

  // ---- designed (custom) screens ----
  CHECK(og_page_is_custom(0x40) && og_page_is_custom(0x5F) && !og_page_is_custom(0x3F) && !og_page_is_custom(0x60) && !og_page_is_custom(0), "custom code range 0x40..0x5F");
  CHECK(og_page_custom_view(og_page_custom_make(7)) == 7 && og_page_custom_view(0x33) == -1, "custom view number round-trips");
  { // every ordinary gauge/split code stays out of the custom range
    for (int t = 1; t <= 3; t++) for (int b = 0; b <= 3; b++) CHECK(!og_page_is_custom(og_page_make(t, b)), "split code %d/%d is not custom", t, b); }
  { OilSettings s = d; Form f; f["page2"] = "65"; CHECK(apply(s, f, err), "validation accepts a designed screen: %s", err); CHECK(s.pages[1] == 65, "stored"); }
  { OilSettings s = d; Form f; f["page2"] = "96"; CHECK(!apply(s, f, err), "code just past the custom range is refused"); }
  { OilSettings s = d; Form f; f["page1"] = "64"; f["page2"] = "0"; f["page3"] = "0"; f["page4"] = "0"; CHECK(apply(s, f, err), "a designed screen alone counts as a screen: %s", err); }
  { // the post checks the view really exists in the loaded layout
    struct L { static bool only_view_2(int v) { return v == 2; } };
    OilSettings live = d; Form f; f["page2"] = "66"; OgPostResult r;
    char e2[160];
    CHECK(og_settings_post(live, getArg, &f, e2, sizeof e2, r, L::only_view_2), "view 2 exists: %s", e2);
    CHECK(live.pages[1] == 66, "applied");
    OilSettings live2 = d; Form g; g["page2"] = "67";
    CHECK(!og_settings_post(live2, getArg, &g, e2, sizeof e2, r, L::only_view_2), "view 3 does not exist: refused");
    CHECK(strstr(e2, "screen 2") && strstr(e2, "view 4"), "names screen and view: %s", e2);
    CHECK(live2.pages[1] == d.pages[1], "nothing half-applied");
    OilSettings live3 = d; CHECK(!og_settings_post(live3, getArg, &g, e2, sizeof e2, r), "no layout support: designed screens refused"); }

  // presets
  { OilSettings s = d; Form f; f["boost_preset"]="1"; CHECK(apply(s, f, err), "%s", err);
    CHECK(s.boost_preset == 1 && s.boost_v1 == 0.0f && s.boost_kpa1 == 1.1f && s.boost_kpa2 == 315.5f && s.boost_abs == 1, "choosing GM loads its curve"); }
  { OilSettings s = d; Form f; f["boost_preset"]="5"; CHECK(apply(s, f, err), "%s", err);
    CHECK(s.boost_abs == 0 && s.boost_v1 == 0.5f, "gauge-type preset sets the sensor type too"); }
  { OilSettings s = d; Form f; f["afr_preset"]="1"; CHECK(apply(s, f, err), "%s", err);
    CHECK(s.afr_afr1 == 7.35f && s.afr_afr2 == 22.39f && s.afr_preset == 1, "choosing LC-2 loads its curve"); }
  { OilSettings s = d; Form f; f["afr_preset"]="2"; CHECK(apply(s, f, err), "%s", err); CHECK(s.afr_afr1 == 8.5f && s.afr_afr2 == 18.0f, "AEM"); }
  // the page sends preset AND curve: matching curve keeps the label, an edited one becomes Custom
  { OilSettings s = d; Form f; f["boost_preset"]="1"; f["boost_v1"]="0"; f["boost_kpa1"]="1.1"; f["boost_v2"]="5"; f["boost_kpa2"]="315.5"; f["boost_abs"]="1";
    CHECK(apply(s, f, err), "%s", err); CHECK(s.boost_preset == 1, "matching curve keeps preset 1, got %d", s.boost_preset); }
  { OilSettings s = d; Form f; f["boost_preset"]="1"; f["boost_v1"]="0"; f["boost_kpa1"]="1.1"; f["boost_v2"]="5"; f["boost_kpa2"]="320"; f["boost_abs"]="1";
    CHECK(apply(s, f, err), "%s", err); CHECK(s.boost_preset == OG_PRESET_CUSTOM, "edited curve is labelled Custom, got %d", s.boost_preset); CHECK(s.boost_kpa2 == 320.0f, "the edit was kept"); }
  { OilSettings s = d; Form f; f["afr_v2"]="4.5"; CHECK(apply(s, f, err), "%s", err); CHECK(s.afr_preset == OG_PRESET_CUSTOM, "editing one AFR value -> Custom"); }
  { OilSettings s = d; Form f; f["boost_preset"]="255"; f["boost_kpa2"]="300"; CHECK(apply(s, f, err), "%s", err); CHECK(s.boost_preset == 255 && s.boost_kpa2 == 300.0f, "explicit custom"); }
  // every preset round-trips through JSON -> form -> same settings (what the page does)
  for (int i = 0; i < OG_NBOOST_PRESETS; i++) {
    OilSettings a = d; og_load_boost_preset(a, i);
    char js[4096]; og_settings_json(a, js, sizeof js);
    OilSettings b = d; Form f; f["boost_preset"] = std::to_string(i);
    f["boost_v1"]=std::to_string(a.boost_v1); f["boost_kpa1"]=std::to_string(a.boost_kpa1); f["boost_v2"]=std::to_string(a.boost_v2);
    f["boost_kpa2"]=std::to_string(a.boost_kpa2); f["boost_abs"]=std::to_string(a.boost_abs);
    CHECK(apply(b, f, err), "preset %d: %s", i, err); CHECK(b.boost_preset == i, "preset %d label survives a page save (got %d)", i, b.boost_preset);
  }
  for (int i = 0; i < OG_NAFR_PRESETS; i++) {
    OilSettings a = d; og_load_afr_preset(a, i);
    OilSettings b = d; Form f; f["afr_preset"] = std::to_string(i);
    f["afr_v1"]=std::to_string(a.afr_v1); f["afr_afr1"]=std::to_string(a.afr_afr1); f["afr_v2"]=std::to_string(a.afr_v2); f["afr_afr2"]=std::to_string(a.afr_afr2);
    CHECK(apply(b, f, err), "afr preset %d: %s", i, err); CHECK(b.afr_preset == i, "afr preset %d label survives a page save (got %d)", i, b.afr_preset);
  }
  // presets JSON for the page
  { char pj[4096]; int n = og_presets_json(pj, sizeof pj);
    CHECK(n > 0 && pj[0]=='{' && pj[n-1]=='}', "presets json shape");
    int commas = 0; for (int i = 0; i < n; i++) if (pj[i]=='"' && pj[i+1]=='n' && pj[i+2]=='"') commas++;
    CHECK(commas == OG_NBOOST_PRESETS + OG_NAFR_PRESETS, "all presets listed (%d)", commas);
    CHECK(strstr(pj, "GM 3 bar") && strstr(pj, "Innovate") && strstr(pj, "AEM"), "named");
    for (int i = 0; i < OG_NBOOST_PRESETS; i++) CHECK(!strpbrk(OG_BOOST_PRESETS[i].name, "\"\\"), "preset name needs no JSON escaping");
    for (int i = 0; i < OG_NAFR_PRESETS; i++) CHECK(!strpbrk(OG_AFR_PRESETS[i].name, "\"\\"), "preset name needs no JSON escaping");
    char tiny[40]; CHECK(og_presets_json(tiny, sizeof tiny) == -1, "too-small buffer reports failure");
    printf("INFO  presets JSON is %d bytes\n", n); }

  // sensor group: the new sensor-side fields restart the sensor, display ones do not
  { OilSettings a = d, b = d; b.boost_ch = 2; CHECK(og_sensor_group_changed(a, b), "channel change");
    b = d; b.boost_kpa2 = 310; CHECK(og_sensor_group_changed(a, b), "boost curve");
    b = d; b.afr_afr2 = 21; CHECK(og_sensor_group_changed(a, b), "afr curve");
    b = d; b.boost_atm_kpa = 99; CHECK(og_sensor_group_changed(a, b), "atmosphere");
    b = d; b.pages[0] = 2; b.boost_warn_psi = 17; b.boost_crit_psi = 23; b.afr_lean_warn = 13; b.afr_gate_psi = 0; b.afr_stoich = 14.0f; b.afr_rich_info = 10.5f;
    CHECK(!og_sensor_group_changed(a, b), "screens and alert limits do not restart the sensor"); }

  // ---- ADS1115 / PaHUB / PbHUB: input numbers, addresses, new fields ----
  { auto ok = [&](std::initializer_list<std::pair<const char*, const char*>> kv, OilSettings base) { OilSettings s = base; Form f; for (auto& p : kv) f[p.first] = p.second; return apply(s, f, err); };
    // the input number limit follows the board
    CHECK(!ok({{"ina_channel","4"}}, d), "INA3221 has 3 channels");
    CHECK(ok({{"sensor_mode","3"},{"ina_channel","4"}}, d), "ADS1115 has 4 inputs: %s", err);
    CHECK(!ok({{"sensor_mode","3"},{"ina_channel","5"}}, d) && strstr(err, "ADS1115 input"), "ADS1115 has no input 5: %s", err);
    CHECK(ok({{"sensor_mode","4"},{"ina_channel","6"},{"boost_ch","1"},{"afr_ch","3"}}, d), "PaHUB has 6 ports: %s", err);
    CHECK(!ok({{"sensor_mode","4"},{"ina_channel","7"}}, d), "PaHUB has no port 7");
    CHECK(ok({{"sensor_mode","5"},{"ina_channel","6"},{"boost_ch","5"},{"afr_ch","1"}}, d), "PbHUB has 6 ports: %s", err);
    CHECK(!ok({{"sensor_mode","5"},{"ina_channel","1"},{"boost_ch","7"}}, d), "boost on a port that is not there");
    CHECK(!ok({{"sensor_mode","4"},{"ina_channel","2"},{"boost_ch","2"}}, d) && strstr(err, "PaHUB port"), "same port twice names the board's own word: %s", err);
    CHECK(!ok({{"sensor_mode","3"},{"ina_channel","1"},{"afr_ch","1"}}, d) && strstr(err, "ADS1115 input"), "same input twice: %s", err);
    // switching to a board with fewer inputs while a higher one is set is refused, not clamped
    { OilSettings a = d; a.sensor_mode = 3; a.ina_channel = 4; CHECK(og_validate(a, err, sizeof err), "ADS on input 4");
      Form f; f["sensor_mode"] = "2"; CHECK(!apply(a, f, err), "back to INA3221 while oil is on input 4"); }
    // addresses
    CHECK(ok({{"sensor_mode","3"},{"hub_addr","74"}}, d), "ADS1115 at 0x4A: %s", err);                  // 74 = 0x4A
    CHECK(ok({{"sensor_mode","3"},{"hub_addr","73"}}, d), "ADS1115 at 0x49 is allowed (range 0x48-0x4B): %s", err);
    CHECK(!ok({{"sensor_mode","3"},{"hub_addr","76"}}, d) && strstr(err, "0x48 to 0x4B"), "ADS1115 address range: %s", err);
    CHECK(ok({{"sensor_mode","4"},{"hub_addr","115"}}, d), "PaHUB at 0x73: %s", err);
    CHECK(!ok({{"sensor_mode","4"},{"hub_addr","97"}}, d) && strstr(err, "0x70 to 0x77"), "PaHUB address range: %s", err);
    CHECK(ok({{"sensor_mode","5"},{"hub_addr","104"}}, d), "PbHUB at 0x68: %s", err);
    CHECK(!ok({{"sensor_mode","5"},{"hub_addr","112"}}, d) && strstr(err, "0x61 to 0x68"), "PbHUB address range: %s", err);
    CHECK(ok({{"sensor_mode","1"},{"hub_addr","200"}}, d), "the address is ignored by the boards that do not use it");
    { OilSettings a = d; a.sensor_mode = 4; CHECK(og_hub_addr(a) == 0x70, "PaHUB default 0x70"); a.sensor_mode = 5; CHECK(og_hub_addr(a) == 0x61, "PbHUB default 0x61");
      a.sensor_mode = 3; CHECK(og_hub_addr(a) == 0x48, "ADS1115 default 0x48"); a.hub_addr = 0x4B; CHECK(og_hub_addr(a) == 0x4B, "explicit address wins"); }
    CHECK(ok({{"in_div","2"},{"adc_scale","1.05"},{"adc_offset","-0.01"},{"pb_adc","2"}}, d), "divider, trim and PbHUB reading: %s", err);
    { OilSettings a = d, b = d; b.hub_addr = 0x4A; CHECK(og_sensor_group_changed(a, b), "address restarts the sensor");
      b = d; b.pb_adc = 1; CHECK(og_sensor_group_changed(a, b), "PbHUB reading restarts the sensor");
      b = d; b.in_div = 2; CHECK(og_sensor_group_changed(a, b), "divider restarts the sensor");
      b = d; b.adc_scale = 1.01f; CHECK(og_sensor_group_changed(a, b), "trim restarts the sensor"); }
    { char js[4096]; CHECK(og_settings_json(d, js, sizeof js) > 0 && strstr(js, "\"hub_addr\":0") && strstr(js, "\"in_div\":1.0000") && strstr(js, "\"pb_adc\":0") && strstr(js, "\"adc_scale\":1.0000"), "new fields in the JSON: %s", js); }
  }

  // sizes / layout
  CHECK(sizeof(OilSettings) % 4 == 0, "struct size multiple of 4: %u", (unsigned)sizeof(OilSettings));
  CHECK(sizeof(OilSettings) != sizeof(OilSettingsV1), "v1 and v2 blobs are told apart by size");
  printf("INFO  OilSettings = %u bytes (v1 was %u)\n", (unsigned)sizeof(OilSettings), (unsigned)sizeof(OilSettingsV1));

  // ---- upgrade from v1.1.0: saved calibration and units survive ----
  { OilSettingsV1 o; memset(&o, 0, sizeof o);
    o.version = 1; o.size = sizeof o; o.sensor_mode = 2; o.ina_channel = 1; o.units_bar = 1; o.backlight = 45; o.buzzer = 0; o.wifi_auto_off_min = 30;
    o.m5_scale = 1.0123f; o.m5_offset = -0.004f; o.ina_scale = 0.9981f; o.ina_offset = 0.012f; o.sender_scale = 1.03f; o.sender_offset = -1.5f;
    o.v_zero = 0.5f; o.v_full = 4.5f; o.bar_full = 10.0f; o.warn_psi = 25; o.crit_psi = 12; o.arm_psi = 18;
    OilSettings n; og_migrate_v1(o, n);
    CHECK(og_validate(n, err, sizeof err), "migrated settings validate: %s", err);
    CHECK(n.version == OG_SETTINGS_VERSION && n.size == sizeof n, "header is the new one");
    CHECK(n.sensor_mode == 2 && n.units_bar == 1 && n.backlight == 45 && n.buzzer == 0 && n.wifi_auto_off_min == 30, "bytes carried");
    CHECK(n.m5_scale == o.m5_scale && n.ina_scale == o.ina_scale && n.ina_offset == o.ina_offset && n.sender_scale == o.sender_scale && n.sender_offset == o.sender_offset, "calibration carried");
    CHECK(n.warn_psi == 25 && n.crit_psi == 12 && n.arm_psi == 18, "thresholds carried");
    CHECK(n.boost_ch == 0 && n.afr_ch == 0 && n.pages[0] == og_page_make(OG_PG_OIL, 0) && n.pages[1] == 0, "boost/AFR start off; one oil screen");
    CHECK(n.boost_preset == 0 && n.afr_preset == 0 && n.boost_atm_kpa == OG_BOOST_ATM_KPA, "sensible boost/AFR starting curves"); }
  // ---- upgrade from settings version 2 (v1.3.x - v1.4.0): everything saved survives, new fields default ----
  { OilSettingsV2 o; memset(&o, 0, sizeof o);
    o.version = 2; o.size = sizeof o; o.sensor_mode = 2; o.ina_channel = 3; o.units_bar = 1; o.backlight = 33; o.buzzer = 0; o.wifi_auto_off_min = 20;
    o.boost_ch = 1; o.afr_ch = 2; o.boost_preset = 2; o.afr_preset = 1; o.boost_abs = 1;
    o.pages[0] = og_page_make(OG_PG_OIL, OG_PG_BOOST); o.pages[1] = 3; o.pages[2] = og_page_custom_make(5);
    o.m5_scale = 1.0123f; o.m5_offset = -0.004f; o.ina_scale = 0.9981f; o.ina_offset = 0.012f; o.sender_scale = 1.03f; o.sender_offset = -1.5f;
    o.v_zero = 0.5f; o.v_full = 4.5f; o.bar_full = 10.0f; o.warn_psi = 25; o.crit_psi = 12; o.arm_psi = 18;
    o.boost_v1 = 0.5f; o.boost_kpa1 = 0; o.boost_v2 = 4.5f; o.boost_kpa2 = 200; o.boost_atm_kpa = 99.5f; o.boost_warn_psi = 17; o.boost_crit_psi = 23;
    o.afr_v1 = 0; o.afr_afr1 = 7.35f; o.afr_v2 = 5; o.afr_afr2 = 22.39f; o.afr_stoich = 14.7f; o.afr_lean_warn = 13.2f; o.afr_lean_crit = 13.9f; o.afr_rich_info = 10.8f; o.afr_gate_psi = -1.5f;
    OilSettings n; og_migrate_v2(o, n);
    CHECK(og_validate(n, err, sizeof err), "migrated v2 settings validate: %s", err);
    CHECK(n.version == OG_SETTINGS_VERSION && n.size == sizeof n, "header is the new one");
    CHECK(n.sensor_mode == 2 && n.ina_channel == 3 && n.units_bar == 1 && n.backlight == 33 && n.buzzer == 0 && n.wifi_auto_off_min == 20, "basic fields");
    CHECK(n.boost_ch == 1 && n.afr_ch == 2 && n.boost_preset == 2 && n.afr_preset == 1 && n.boost_abs == 1, "boost / AFR setup");
    CHECK(n.pages[0] == og_page_make(OG_PG_OIL, OG_PG_BOOST) && n.pages[1] == 3 && n.pages[2] == og_page_custom_make(5) && n.pages[3] == 0, "screens incl. a designed one");
    CHECK(n.m5_scale == o.m5_scale && n.ina_offset == o.ina_offset && n.sender_offset == o.sender_offset && n.warn_psi == 25 && n.crit_psi == 12 && n.arm_psi == 18, "calibration and thresholds");
    CHECK(n.boost_kpa2 == 200 && n.boost_atm_kpa == 99.5f && n.boost_crit_psi == 23 && n.afr_afr2 == 22.39f && n.afr_lean_warn == 13.2f && n.afr_gate_psi == -1.5f, "boost / AFR curves and limits (last fields of the old layout)");
    CHECK(n.hub_addr == 0 && n.pb_adc == 0 && n.in_div == 1.0f && n.adc_scale == 1.0f && n.adc_offset == 0.0f, "new fields start at their defaults");
    CHECK(sizeof(OilSettingsV2) != sizeof(OilSettings) && sizeof(OilSettingsV2) != sizeof(OilSettingsV1), "all three blobs are told apart by size"); }
  // a v1 blob that was already invalid is not blindly trusted: validation catches it
  { OilSettingsV1 o; memset(&o, 0, sizeof o); o.version = 1; o.size = sizeof o; o.ina_channel = 9;
    OilSettings n; og_migrate_v1(o, n); CHECK(!og_validate(n, err, sizeof err), "garbage v1 is rejected after migration"); }
  { OilSettings x = d; x.version = 2; char js[4096]; CHECK(og_settings_json(x, js, sizeof js) > 0 && strstr(js, "\"page1\":1") && strstr(js, "\"boost_ch\":0") && strstr(js, "\"afr_gate_psi\":-2.0000"), "new fields in the JSON: %s", js); }

  printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
  return fails != 0;
}
