// Host-side "gauge brain" for the page test: runs the REAL settings rules, presets,
// live-JSON builder and zero-boost logic (oil_settings.h / oil_math.h / oil_api.h) behind a
// tiny line protocol on stdin/stdout, so mock_gauge.py can serve the real page against them.
//   g++ -std=c++11 -I../.. gauge_core.cpp -o gauge_core
// One command per line, fields separated by TAB. Replies are one line.
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <sstream>
#include "oil_api.h"
#include "oil_demo.h"

OilSettings g_set;

static std::map<std::string, std::string> g_form;
static const char* getArg(const char* n, void*) { auto it = g_form.find(n); return it == g_form.end() ? NULL : it->second.c_str(); }
static OgPostResult g_last = { false, false };
static unsigned g_views_usable = 0;                       // bit v = designed view v can be a screen (set by the mock when a layout is "uploaded")
static bool viewOk(int v) { return v >= 0 && v < 32 && ((g_views_usable >> v) & 1u); }

static std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> o; std::string cur; std::stringstream ss(s);
  while (std::getline(ss, cur, '\t')) o.push_back(cur);
  return o;
}
static void jsonErr(const char* msg) {
  std::string e; for (const char* p = msg; *p; p++) { if (*p == '"' || *p == '\\') e += '\\'; e += *p; }
  printf("{\"ok\":false,\"error\":\"%s\"}\n", e.c_str());
}
// volts (as text, "off"/"nan" allowed) -> the reading the sensor module would publish
static ChanReading reading(OgChan ch, const std::string& vs, int kind) {
  ChanReading r; r.val = NAN; r.volts = NAN; r.fault = OIL_FAULT_NO_DATA;
  const bool configured = ch == OG_CH_OIL || (ch == OG_CH_BOOST && g_set.boost_ch) || (ch == OG_CH_AFR && g_set.afr_ch);
  if (!configured) { r.fault = OIL_FAULT_OFF; return r; }
  if (kind == 0) { r.fault = OIL_FAULT_NO_SENSOR; return r; }
  if (kind == 1 && ch != OG_CH_OIL) { r.fault = OIL_FAULT_UNSUPPORTED; return r; }
  float v = strtof(vs.c_str(), nullptr);
  r.volts = v; float x = 0; OgConv c;
  switch (ch) { case OG_CH_OIL: c = og_volts_to_psi(v, x); break; case OG_CH_BOOST: c = og_volts_to_boost(v, x); break; default: c = og_volts_to_afr(v, x); }
  if (c == OG_CONV_OK) { r.fault = OIL_FAULT_NONE; r.val = x; }
  else r.fault = (c == OG_CONV_LOW) ? OIL_FAULT_WIRING_LOW : OIL_FAULT_WIRING_HIGH;
  return r;
}

int main() {
  og_defaults(g_set);
  setvbuf(stdout, NULL, _IOLBF, 0);
  std::string line;
  while (std::getline(std::cin, line)) {
    std::vector<std::string> a = split(line);
    if (a.empty()) continue;
    char buf[4096], err[200];
    if (a[0] == "settings") {
      if (og_settings_json(g_set, buf, sizeof buf) < 0) { jsonErr("settings did not fit"); continue; }
      puts(buf);
    } else if (a[0] == "presets") {
      if (og_presets_json(buf, sizeof buf) < 0) { jsonErr("presets did not fit"); continue; }
      puts(buf);
    } else if (a[0] == "post") {
      g_form.clear();
      for (size_t i = 1; i + 1 < a.size(); i += 2) g_form[a[i]] = a[i + 1];
      if (!og_settings_post(g_set, getArg, nullptr, err, sizeof err, g_last, viewOk)) jsonErr(err);
      else puts("{\"ok\":true}");
    } else if (a[0] == "views" && a.size() >= 2) {          // views <usable bit mask>
      g_views_usable = (unsigned)strtoul(a[1].c_str(), nullptr, 10); puts("{\"ok\":true}");
    } else if (a[0] == "flags") {
      printf("{\"sensor_changed\":%s,\"only_backlight\":%s}\n", g_last.sensor_changed ? "true" : "false", g_last.only_backlight ? "true" : "false");
    } else if (a[0] == "defaults") {
      og_defaults(g_set); puts("{\"ok\":true}");
    } else if (a[0] == "live" && a.size() >= 6) {            // live <oil V> <boost V> <afr V> <kind> <armed> <sensor name>
      const int kind = atoi(a[4].c_str());
      ChanReading r[OG_NCH] = { reading(OG_CH_OIL, a[1], kind), reading(OG_CH_BOOST, a[2], kind), reading(OG_CH_AFR, a[3], kind) };
      if (og_live_json(buf, sizeof buf, r, a.size() > 6 ? a[6].c_str() : "", kind, a[5] == "1", a.size() > 7 ? atoi(a[7].c_str()) : 0) < 0) jsonErr("live did not fit"); else puts(buf);
    } else if (a[0] == "demoparse" && a.size() >= 2) {        // demoparse <on field>: the real request parsing
      const int v = og_demo_parse(a[1].c_str());
      if (v < 0) jsonErr("on must be 1 or 0"); else printf("{\"ok\":true,\"on\":%d}\n", v);
    } else if (a[0] == "demowave" && a.size() >= 2) {         // demowave <t ms>: the demo's readings at that time
      ChanReading r[OG_NCH]; og_demo_readings((uint32_t)strtoul(a[1].c_str(), nullptr, 10), r);
      printf("{\"oil\":%.1f,\"boost\":%.1f,\"afr\":%.2f,\"left\":%d}\n", (double)r[OG_CH_OIL].val, (double)r[OG_CH_BOOST].val, (double)r[OG_CH_AFR].val,
             og_demo_left_s(true, (uint32_t)strtoul(a[1].c_str(), nullptr, 10)));
    } else if (a[0] == "zero" && a.size() >= 3) {            // zero <boost V> <kind>
      ChanReading b = reading(OG_CH_BOOST, a[1], atoi(a[2].c_str()));
      float kpa = 0;
      if (!og_zero_boost(b, err, sizeof err, kpa)) jsonErr(err);
      else { g_set.boost_atm_kpa = kpa; printf("{\"ok\":true,\"kpa\":%.1f}\n", (double)kpa); }
    } else {
      jsonErr("unknown command");
    }
  }
  return 0;
}
