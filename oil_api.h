#pragma once
// ---------------------------------------------------------------------------
// oil_api.h - the parts of the phone-page API that are pure logic (no Arduino,
// no WebServer): the live JSON, applying a settings form, and "zero boost".
// oil_web.cpp calls these, and so does the host test server
// (tools/webtest/gauge_core.cpp) that the real page is exercised against - so
// the page is tested against the real rules, not a copy of them.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include "oil_types.h"
#include "oil_settings.h"
#include "oil_math.h"

static inline const char* og_fault_text(OilFault f) {
  switch (f) {
    case OIL_FAULT_NONE:        return "OK";
    case OIL_FAULT_NO_SENSOR:   return "NO SENSOR";
    case OIL_FAULT_WIRING_LOW:  return "CHECK WIRING (low)";
    case OIL_FAULT_WIRING_HIGH: return "CHECK WIRING (high)";
    case OIL_FAULT_NO_DATA:     return "NO DATA";
    case OIL_FAULT_UNSUPPORTED: return "NEEDS INA3221";
    case OIL_FAULT_OFF:         return "OFF";
  }
  return "?";
}

// "key":value,"volts":..,"fault":n,"status":".."   (decimals: digits of the value)
static inline int og_live_chan(char* out, size_t n, const char* valkey, const ChanReading& r, int decimals) {
  char val[24] = "null", v[24] = "null";
  if (r.fault == OIL_FAULT_NONE && isfinite(r.val)) snprintf(val, sizeof val, decimals == 2 ? "%.2f" : "%.1f", (double)(r.val + 0.0f));   // + 0.0f: never print "-0.0"
  if (isfinite(r.volts))                            snprintf(v, sizeof v, "%.3f", (double)r.volts);
  return snprintf(out, n, "\"%s\":%s,\"volts\":%s,\"fault\":%d,\"status\":\"%s\"", valkey, val, v, (int)r.fault, og_fault_text(r.fault));
}

// The page polls this. Oil stays at the top level; boost and AFR are objects.
// demo_s: seconds of "Run demo" left (0 = not running). The values here are always the REAL sensors.
static inline int og_live_json(char* buf, size_t n, const ChanReading r[OG_NCH], const char* sensor, int kind, bool armed, int demo_s = 0) {
  char po[160], pb[160], pa[160];
  og_live_chan(po, sizeof po, "psi", r[OG_CH_OIL], 1);
  og_live_chan(pb, sizeof pb, "psi", r[OG_CH_BOOST], 1);
  og_live_chan(pa, sizeof pa, "afr", r[OG_CH_AFR], 2);
  int w = snprintf(buf, n, "{%s,\"sensor\":\"%s\",\"kind\":%d,\"armed\":%s,\"demo\":%d,\"boost\":{%s},\"afr\":{%s}}",
                   po, sensor, kind, armed ? "true" : "false", demo_s, pb, pa);
  return (w < 0 || (size_t)w >= n) ? -1 : w;
}

// "Zero boost now". With the engine OFF an absolute MAP sensor reads atmospheric
// pressure; accept it as the new zero only if it really looks like atmosphere.
static inline bool og_zero_boost(const ChanReading& b, char* err, size_t n, float& kpa_out) {
  if (b.fault == OIL_FAULT_OFF)  { snprintf(err, n, "Boost is switched off - give it an INA3221 channel first."); return false; }
  if (b.fault != OIL_FAULT_NONE) { snprintf(err, n, "No good boost reading to zero (%s).", og_fault_text(b.fault)); return false; }
  if (!g_set.boost_abs)          { snprintf(err, n, "This sensor type reads gauge pressure, so it already reads 0 at atmosphere - nothing to zero."); return false; }
  if (!og_boost_ambient(b.volts, kpa_out)) {
    snprintf(err, n, "The sensor reads %.1f kPa, which is not atmospheric pressure (80 to 110). Engine off? Right sensor preset?",
             (double)og_boost_sensor_kpa(b.volts));
    return false;
  }
  return true;
}

struct OgPostResult { bool sensor_changed; bool only_backlight; };

// Apply a settings form to 'live'. On failure nothing is changed.
// `view_ok(v)` says whether designed view v can be a screen (it is in the loaded
// layout, switched on, and offered to this gauge). Null = no layout support, so
// designed views are refused.
typedef bool (*OgViewOkFn)(int view);
static inline bool og_settings_post(OilSettings& live, OgArgFn get, void* ctx, char* err, size_t n, OgPostResult& out,
                                    OgViewOkFn view_ok = nullptr) {
  OilSettings c = live;
  if (!og_apply_form(c, get, ctx, err, n)) return false;
  for (int i = 0; i < OG_MAX_PAGES; i++) {
    if (!og_page_is_custom(c.pages[i])) continue;
    const int v = og_page_custom_view(c.pages[i]);
    if (!view_ok || !view_ok(v)) {
      snprintf(err, n, "screen %d uses designed view %d, which is not in the layout on the gauge", i + 1, v + 1);
      return false;
    }
  }
  out.sensor_changed = og_sensor_group_changed(live, c);
  OilSettings probe = c; probe.backlight = live.backlight;
  out.only_backlight = (memcmp(&probe, &live, sizeof probe) == 0);       // the brightness slider: no redraw needed
  live = c;
  return true;
}
