#define GL_CAN_BUS 1   // the full CAN-bus screens (adds the gear format and the brain's oil verdict)
// Gauge_Layout.cpp - layout engine. Format: LAYOUT_FORMAT.md.
//
// Each custom view is a single transparent LVGL object; its draw callback
// paints every gauge (bands, segments, zones, ticks, numbers, needles, bars,
// lights, text) straight into the frame. GL_Update() does all the logic
// (smoothing, alerts, flashing, sweep, peak) and stores what to draw; the draw
// callback only paints. It invalidates only when something visible changed.
#include "Gauge_Layout.h"
// "Only when running": the full 3-screen system judges it by RPM; a sketch with no RPM
// defines GL_ENGINE_RUNNING(kv) in its Gauge_Inputs.h to say what running means there.
// Two builds share this file. The standalone oil gauge has no CAN bus. The full CAN-bus screens
// define GL_CAN_BUS (the copy made for them does it at the top), which adds the "gear" number
// format and the brain's oil verdict ("alerts.status").
#ifdef GL_CAN_BUS
#define GL_GEARFMT(g) ((g).gear_fmt)
#else
#define GL_GEARFMT(g) false
#endif
#ifndef GL_ENGINE_RUNNING
#define GL_ENGINE_RUNNING(kv) (isfinite((kv)[GI_rpm]) && (kv)[GI_rpm] >= 400.0f)
#endif
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

// The gauges' data lives in PSRAM on the screens: internal RAM is what Wi-Fi
// and the display stream need, and this is only read ~10 times a second.
static void* glCalloc(size_t n, size_t sz) {
#ifdef ESP_PLATFORM
  void* p = heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p) return p;
#endif
  return calloc(n, sz);
}

// ---------------------------------------------------------------- types ---
enum GlType  : uint8_t { GT_ARC = 0, GT_NEEDLE, GT_BAR, GT_DIGITAL, GT_LIGHT, GT_TEXT, GT_LINE, GT_CLOCK, GT_IMAGE };
enum GlUnit  : uint8_t { GU_AUTO = 0, GU_C, GU_F, GU_PSI, GU_BAR, GU_KPA, GU_MPH, GU_KMH, GU_RAW };
enum GlRole  : uint8_t { GR_HEX = 0, GR_GAUGE, GR_TEXT, GR_DIM, GR_ACCENT, GR_TRACK, GR_TICK,
                         GR_TICKMAJ, GR_DIAL, GR_WARN, GR_CRIT, GR_OK };
enum GlOp    : uint8_t { GO_GE = 0, GO_GT, GO_LE, GO_LT, GO_EQ, GO_NE };

struct GlColor { uint8_t role; uint32_t rgb; };
struct GlPart  { bool show; char text[24]; uint8_t font; int16_t dx, dy; };
struct GlMark  { float value; GlColor c; uint8_t w; int16_t out, in; };     // reference line across the band
struct GlScale { uint8_t unit; float min, max; uint8_t major; float step; };             // scale used in one display unit
// what the note text says: no reading / condition not met / normal / which alert band
enum GlNote : uint8_t { NS_NONE = 0, NS_IDLE, NS_OK, NS_LC, NS_LW, NS_HW, NS_HC, NS_COUNT };
#define GL_MAX_MARKS  4
#define GL_MAX_SCALES 3

struct GlGauge {
  // --- from the file ---
  uint8_t type;
  int16_t x, y;
  int8_t  input;                       // GiId, -1 for text / line
  float   vmin, vmax, origin;              // scale in use (base units); origin NAN = from vmin
  float   vmin0, vmax0; uint8_t t_major0;  // as in the file (before a by_unit scale)
  GlScale scales[GL_MAX_SCALES]; uint8_t nscales;
  uint8_t decimals, units;
#ifdef GL_CAN_BUS
  bool    gear_fmt;
  int8_t  status_in;                   // oil_status-style input whose verdict sets the level, or -1
#endif
  GlColor c_fill, c_track, c_text, c_label, c_needle;
  bool    has_nodata; GlColor c_nodata;    // colour of "--" (default: the text colour)
  GlColor c_scale, c_tmaj, c_tmin;         // scale numbers, major / minor ticks (dials and clock)
  GlColor c_unit;                          // the unit text
  float   lc, lw, hw, hc;              // alert levels, NAN = unused
  bool    has_alert, when_running, flash, takeover;
  GlColor c_alert[4]; uint8_t ac_set;  // own colour per alert level (low_crit, low_warn, high_warn, high_crit); bit set = in use
  bool    num_alert;                   // alerts colour the reading as well as the fill
  int8_t  when_in; uint8_t when_op; float when_v;   // judge only when this holds (-1 = always)
  float   smooth; bool sweep_anim, peak; float peak_s;
  // arc / needle
  int16_t r, width; float a_start, a_sweep; uint8_t segments;
  float   t_step, t_step0;             // >0: marks every this many display units, counted from zero (0 = even divisions)
  uint8_t t_major, t_minor; bool t_numbers; uint8_t t_font; int16_t t_inset, t_len; float t_minlen;
  GlMark  marks[GL_MAX_MARKS]; uint8_t nmarks;
  bool    zones, rounded;                // rounded: round ends on the band and the fill
  float   unlit_shade;                   // >0: unlit blocks = their lit colour at this strength over the track
  bool    zone_fill;                     // lit blocks take their zone's colour (green / amber / red)
  // second ring beside the band: zones (and optionally the reading)
  int16_t ring_off; uint8_t ring_w, ring_seg; bool ring_zones, ring_fill;
  // notch gaps: blocks marked by lines in the face colour across a solid band
  bool    notch; uint8_t gap_w; int16_t gap_out, gap_in; GlColor c_gap;
  int16_t n_len, n_w, n_tail, n_hub;
  // bar
  int16_t w, h; bool vertical;
  // line
  int16_t x2, y2;
  // light
  int16_t size; bool square; uint8_t op; float on_value; GlColor c_on, c_off; bool blink;
  // clock: style 0 = the screen's setting, 1 analogue, 2 digital. Hands are
  // fractions of r; digits are h tall with segments seg_t thick.
  uint8_t clk_style; float hand_h, hand_m, hand_s; bool show_sec, label_analog; uint8_t seg_t;
  // image: index into the images the screen code registered (GL_SetImages)
  int8_t  img; bool has_tint; GlColor c_tint;
  GlPart  number, label, unit;
  // glow look (arcs, bars, seven-segment numbers)
  float   glow;                        // 0 = off, up to 1: soft halo round the lit parts
  bool    core;                        // lit parts get a brighter centre
  // seven-segment number ("number": {"style": "seg7", ...})
  bool    n_fixed;                     // Big number gauge: number keeps its digits in place (width from the scale, anchored on the point)
  bool    n7;
  uint8_t n7_h, n7_t, n7_digits;       // digit height, stroke (0 = from height), fixed field width in digits (0 = just the text)
  float   n7_glow; bool n7_core;
  float   n7_slant, n7_ghost;          // lean in degrees; unlit segments' strength (0 = hidden)
  GlPart  note; char notes[NS_COUNT][16];      // status text by state
  GlPart  pk;   float pk_start;                // "peak 12.3": highest reading since the screen started
  // --- runtime ---
  char     label_txt[24];                      // label with {u} filled in
  float    pk_raw; bool pk_have;               // kept while the view is closed (until the next load)
  uint8_t  d_note; bool d_pk; bool d_dark;    // d_dark: flashing, in the dark phase
  float    disp; bool dvalid; uint32_t last_t;
  float    peak_v; bool peak_valid; uint32_t peak_t;
  uint32_t crit_since;
  // what the draw callback paints
  bool     d_valid; float d_frac;      // fill / needle position 0..1
  bool     d_peak; float d_peak_frac;
  lv_color_t d_fill, d_num;
  bool     d_lit;
  char     d_txt[16];
  char     d_pktxt[40];
  char     unit_txt[8];
  // change detection
  uint32_t sig; bool dirty;          // last drawn state; dirty = must redraw
};

struct GlView {
  char id[16]; char name[24];
  char builtin[16];                    // "" = custom view, else a built-in view's key
  uint16_t screens;                    // bit n = screen n (1..15); 0 = all
  bool enabled;                        // false = on no screen
  bool face;
  bool has_bg;                         // "bg": the view's own background colour
  GlColor bg;
  uint8_t n;
  GlGauge* g;
};

#define GL_MAX_CUSTOM 12               // custom views (each screen keeps up to 12)
#define GL_MAX_VIEWS  32               // custom + built-in entries
#define GL_MAX_GAUGES 16

static GlView    s_views[GL_MAX_VIEWS];
static int       s_nviews = 0;
static int       s_built = -1;
static lv_obj_t* s_obj = nullptr;
static uint32_t  s_built_t = 0;
static GlUnits   s_units = {false, false};
static const GlImage* s_imgs = nullptr;  // images layouts may use (the caller keeps them)
static int       s_nimgs = 0;
static GlClock   s_clk = {false, 2000, 1, 1, 6, 0, 0, 0};
static bool      s_clk_analog = true;   // the screen's clock setting, for clocks on "auto"
static GlPalette s_pal = {
  lv_color_hex(0x1db954), lv_color_hex(0x1db954), lv_color_hex(0x8a8a94), lv_color_hex(0x1db954),
  lv_color_hex(0x1a1a1e), lv_color_hex(0x3a3a42), lv_color_hex(0x6a6a76), lv_color_hex(0x0d0d0f),
  lv_color_hex(0xf5a623), lv_color_hex(0xe2213b), lv_color_hex(0x1db954)
};

// ----------------------------------------------------------- small utils ---
static void scopy(char* d, const char* s, size_t n) {
  if (!s) s = "";
  strncpy(d, s, n - 1); d[n - 1] = 0;
}
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

static lv_color_t palOf(const GlColor& c);
// The built view's background, if it sets one ("bg"): the dial face colour
// for that view, so notches and anything else in "dial" colour match it.
static bool builtBg(lv_color_t* out) {
  if (s_built < 0 || !s_views[s_built].has_bg) return false;
  *out = palOf(s_views[s_built].bg);
  return true;
}
static lv_color_t colOf(const GlColor& c) {
  lv_color_t b;
  if (c.role == GR_DIAL && builtBg(&b)) return b;
  return palOf(c);
}
static lv_color_t palOf(const GlColor& c) {
  switch (c.role) {
    case GR_GAUGE: return s_pal.gauge;   case GR_TEXT: return s_pal.text;
    case GR_DIM: return s_pal.dim;       case GR_ACCENT: return s_pal.accent;
    case GR_TRACK: return s_pal.track;   case GR_TICK: return s_pal.tick;
    case GR_TICKMAJ: return s_pal.tickmaj; case GR_DIAL: return s_pal.dial;
    case GR_WARN: return s_pal.warn;     case GR_CRIT: return s_pal.crit;
    case GR_OK: return s_pal.ok;
    default: return lv_color_hex(c.rgb);
  }
}

static const lv_font_t* fontFor(int size) {
  struct F { int s; const lv_font_t* f; };
  static const F fl[] = {
#if LV_FONT_MONTSERRAT_48
    {48, &lv_font_montserrat_48},
#endif
#if LV_FONT_MONTSERRAT_28
    {28, &lv_font_montserrat_28},
#endif
#if LV_FONT_MONTSERRAT_20
    {20, &lv_font_montserrat_20},
#endif
#if LV_FONT_MONTSERRAT_16
    {16, &lv_font_montserrat_16},
#endif
#if LV_FONT_MONTSERRAT_14
    {14, &lv_font_montserrat_14},
#endif
#if LV_FONT_MONTSERRAT_12
    {12, &lv_font_montserrat_12},
#endif
    {0, LV_FONT_DEFAULT}
  };
  for (const F& f : fl) if (f.s && f.s <= size) return f.f;
  const int n = sizeof fl / sizeof fl[0];
  return n >= 2 ? fl[n - 2].f : LV_FONT_DEFAULT;     // smaller than any: the smallest we have
}

// ------------------------------------------------------------------ units ---
// Which display unit a gauge really uses (resolves "auto").
static uint8_t unitOf(const GlGauge& g) {
  if (g.input < 0) return GU_RAW;
  const GiInput& in = GI_TABLE[g.input];
  uint8_t u = g.units;
  switch (in.cat) {
    case GI_TEMP:
      if (u == GU_C || u == GU_F) return u;
      return (u == GU_AUTO && s_units.temp_f) ? GU_F : GU_C;
    case GI_PRESS:
      if (u == GU_PSI || u == GU_BAR || u == GU_KPA) return u;
      if (u == GU_AUTO && s_units.oil_bar &&
          (g.input == GI_oil_psi || g.input == GI_oil_psi_direct || g.input == GI_oil_psi_ecutek)) return GU_BAR;
      return GU_PSI;
    case GI_PRESS_KPA:
      if (u == GU_PSI || u == GU_BAR || u == GU_KPA) return u;
      return GU_KPA;
    case GI_SPEED:
      return (u == GU_KMH) ? GU_KMH : GU_MPH;
    default:
      return GU_RAW;
  }
}
// base value -> display value
static float toDisp(const GlGauge& g, float v) {
  if (g.input < 0) return v;
  uint8_t u = unitOf(g);
  switch (GI_TABLE[g.input].cat) {
    case GI_TEMP:      return u == GU_F ? v * 9.0f / 5.0f + 32.0f : v;
    case GI_PRESS:     return u == GU_BAR ? v / 14.5038f : u == GU_KPA ? v * 6.89476f : v;
    case GI_PRESS_KPA: return u == GU_PSI ? v * 0.145038f : u == GU_BAR ? v / 100.0f : v;
    case GI_SPEED:     return u == GU_KMH ? v * 1.609344f : v;
    default:           return v;
  }
}
static int decimalsOf(const GlGauge& g) {
  int d = g.decimals;
  if (g.input >= 0 && GI_TABLE[g.input].cat == GI_PRESS && unitOf(g) == GU_BAR && d == 0) d = 1;
  return d;
}
static void unitText(const GlGauge& g, char* out, size_t n) {
  if (g.input < 0) { out[0] = 0; return; }
  switch (unitOf(g)) {
    case GU_C:   scopy(out, "\xC2\xB0" "C", n); return;
    case GU_F:   scopy(out, "\xC2\xB0" "F", n); return;
    case GU_PSI: scopy(out, "PSI", n); return;
    case GU_BAR: scopy(out, "BAR", n); return;
    case GU_KPA: scopy(out, "kPa", n); return;
    case GU_MPH: scopy(out, "MPH", n); return;
    case GU_KMH: scopy(out, "KM/H", n); return;
    default:    scopy(out, GI_TABLE[g.input].unit, n); return;
  }
}
static void fmtValue(const GlGauge& g, float v, char* out, size_t n) {
  if (!isfinite(v)) { scopy(out, "--", n); return; }
#ifdef GL_CAN_BUS
  if (g.gear_fmt) {
    int gi = (int)lroundf(v);
    if (gi < 0) scopy(out, "R", n); else if (gi == 0) scopy(out, "N", n); else snprintf(out, n, "%d", gi);
    return;
  }
#endif
  float d = toDisp(g, v);
  int dp = decimalsOf(g);
  snprintf(out, n, "%.*f", dp, (double)d);
  if (!strcmp(out, "-0") || !strcmp(out, "-0.0") || !strcmp(out, "-0.00")) memmove(out, out + 1, strlen(out));
}

// display value -> base value (inverse of toDisp)
static float fromDisp(const GlGauge& g, float v) {
  if (g.input < 0) return v;
  uint8_t u = unitOf(g);
  switch (GI_TABLE[g.input].cat) {
    case GI_TEMP:      return u == GU_F ? (v - 32.0f) * 5.0f / 9.0f : v;
    case GI_PRESS:     return u == GU_BAR ? v * 14.5038f : u == GU_KPA ? v / 6.89476f : v;
    case GI_PRESS_KPA: return u == GU_PSI ? v / 0.145038f : u == GU_BAR ? v * 100.0f : v;
    case GI_SPEED:     return u == GU_KMH ? v / 1.609344f : v;
    default:           return v;
  }
}
// The scale for the screen's current units: a by_unit entry if one matches.
static void applyScale(GlGauge& g) {
  g.vmin = g.vmin0; g.vmax = g.vmax0; g.t_major = g.t_major0; g.t_step = g.t_step0;
  uint8_t u = unitOf(g);
  for (int i = 0; i < g.nscales; i++) {
    const GlScale& sc = g.scales[i];
    if (sc.unit != u) continue;
    float a = fromDisp(g, sc.min), b = fromDisp(g, sc.max);
    if (b > a) { g.vmin = a; g.vmax = b; }
    if (sc.major) g.t_major = sc.major;
    g.t_step = sc.step;                        // this unit has its own scale: its own step (or none), never the other unit's
  }
}
// Label text with {u} replaced by the unit ("OIL  {u}" -> "OIL  PSI").
// {u} = the unit as shown ("°C"), {U} = without the degree sign ("C").
static void labelText(const GlGauge& g, char* out, size_t n) {
  const char* t = g.label.text;
  const char* p = strstr(t, "{u}"); const char* P = strstr(t, "{U}");
  if (!p || (P && P < p)) p = P;
  if (!p) { scopy(out, t, n); return; }
  char u[8]; unitText(g, u, sizeof u);
  const char* us = (p[1] == 'U' && !strncmp(u, "\xC2\xB0", 2)) ? u + 2 : u;
  snprintf(out, n, "%.*s%s%s", (int)(p - t), t, us, p + 3);
}
static void refreshUnits(GlGauge& g) {
  applyScale(g);
  unitText(g, g.unit_txt, sizeof g.unit_txt);
  if (!g.unit.show) g.unit_txt[0] = 0;
  labelText(g, g.label_txt, sizeof g.label_txt);
}
static bool cmpOp(uint8_t op, float v, float ref) {
  switch (op) {
    case GO_GT: return v > ref;   case GO_LE: return v <= ref;
    case GO_LT: return v < ref;   case GO_EQ: return fabsf(v - ref) < 1e-3f;
    case GO_NE: return fabsf(v - ref) >= 1e-3f;
    default:    return v >= ref;
  }
}
static uint8_t opOf(const char* s) {
  static const char* ops[] = {">=", ">", "<=", "<", "==", "!="};
  for (int i = 0; i < 6; i++) if (s && !strcmp(s, ops[i])) return (uint8_t)i;
  return GO_GE;
}
static int unitByName(const char* s) {
  static const char* units[] = {"auto", "c", "f", "psi", "bar", "kpa", "mph", "kmh", "raw"};
  for (int i = 0; i < 9; i++) if (s && !strcmp(s, units[i])) return i;
  return -1;
}

// ------------------------------------------------------------------ parse ---
static bool parseColor(JsonVariantConst v, GlColor& c) {
  const char* s = v | (const char*)nullptr;
  if (!s) return true;                                  // keep default
  static const struct { const char* n; uint8_t r; } roles[] = {
    {"gauge", GR_GAUGE}, {"text", GR_TEXT}, {"dim", GR_DIM}, {"accent", GR_ACCENT}, {"track", GR_TRACK},
    {"tick", GR_TICK}, {"tickmaj", GR_TICKMAJ}, {"dial", GR_DIAL}, {"warn", GR_WARN}, {"crit", GR_CRIT}, {"ok", GR_OK}};
  for (auto& r : roles) if (!strcmp(s, r.n)) { c.role = r.r; c.rgb = 0; return true; }
  if (s[0] == '#' && strlen(s) == 7) {
    char* end = nullptr; unsigned long x = strtoul(s + 1, &end, 16);
    if (end && *end == 0) { c.role = GR_HEX; c.rgb = (uint32_t)x; return true; }
  }
  return false;
}
static float fnum(JsonObjectConst o, const char* k, float def) {
  JsonVariantConst v = o[k];
  return v.is<float>() ? v.as<float>() : def;
}
static float fnull(JsonObjectConst o, const char* k) {      // number or NAN
  JsonVariantConst v = o[k];
  return v.is<float>() ? v.as<float>() : NAN;
}
static bool fbool(JsonObjectConst o, const char* k, bool def) {
  JsonVariantConst v = o[k];
  return v.is<bool>() ? v.as<bool>() : def;
}
static void parsePart(JsonObjectConst o, const char* k, GlPart& p) {
  JsonObjectConst q = o[k];
  if (q.isNull()) return;
  p.show = fbool(q, "show", p.show);
  if (q["text"].is<const char*>()) { scopy(p.text, q["text"].as<const char*>(), sizeof p.text); p.show = p.text[0] != 0; }
  p.font = (uint8_t)clampf(fnum(q, "font", p.font), 8, 48);
  p.dx = (int16_t)clampf(fnum(q, "dx", p.dx), -480, 480);
  p.dy = (int16_t)clampf(fnum(q, "dy", p.dy), -480, 480);
}

static void setDefaults(GlGauge& g, uint8_t type, int input) {
  memset(&g, 0, sizeof g);
  g.type = type; g.x = 240; g.y = 240; g.input = (int8_t)input;
  const GiInput* in = input >= 0 ? &GI_TABLE[input] : nullptr;
  g.vmin = in ? in->def_min : 0; g.vmax = in ? in->def_max : 100; g.origin = NAN;
  g.decimals = in ? in->def_dp : 0; g.units = GU_AUTO;
#ifdef GL_CAN_BUS
  g.gear_fmt = in && in->kind == GI_GEAR; g.status_in = -1;
#endif
  g.c_fill = {GR_GAUGE, 0}; g.c_track = {GR_TRACK, 0}; g.c_text = {GR_TEXT, 0};
  g.c_label = {GR_DIM, 0}; g.c_needle = {GR_CRIT, 0};
  g.c_scale = {GR_DIM, 0}; g.c_tmaj = {GR_TICKMAJ, 0}; g.c_tmin = {GR_TICK, 0}; g.c_unit = {GR_DIM, 0};
  g.lc = g.lw = g.hw = g.hc = NAN;
  g.num_alert = true; g.when_in = -1; g.when_op = GO_GE; g.when_v = 0;
  g.t_minlen = NAN; g.pk_start = NAN;
  g.smooth = 0.5f; g.peak_s = 3;
  g.r = 200; g.width = 30; g.a_start = 225; g.a_sweep = 270;
  g.segments = (type == GT_ARC) ? 20 : 0;
  g.t_major = 6; g.t_minor = 2; g.t_numbers = true; g.t_font = 20; g.t_inset = 6; g.t_len = 16;
  g.zones = true;
  g.n_len = 180; g.n_w = 6; g.n_tail = 20; g.n_hub = 14;
  g.w = 300; g.h = 36;
  g.size = 40; g.op = GO_GE; g.on_value = 1; g.c_on = {GR_CRIT, 0}; g.c_off = {GR_TRACK, 0};
  const char* lbl = in ? in->label : "";
  g.number = {true, "", 48, 0, 0};
  g.label  = {lbl[0] != 0, "", 20, 0, 70};  scopy(g.label.text, lbl, sizeof g.label.text);
  g.unit   = {true, "", 16, 0, -50};
  switch (type) {
    case GT_NEEDLE: g.width = 12; break;
    case GT_BAR:    g.number = {true, "", 28, 0, -50}; g.label.font = 16; g.label.dy = 40; g.unit.show = false; break;
    case GT_DIGITAL:g.label.dy = 50; g.unit.dy = -40; break;
    case GT_LIGHT:  g.label.font = 16; g.label.dy = 40; g.number.show = false; g.unit.show = false; break;
    case GT_TEXT:   g.number.show = false; g.unit.show = false; g.label = {true, "", 20, 0, 0}; g.c_label = {GR_DIM, 0}; break;
    case GT_LINE:   g.number.show = false; g.unit.show = false; g.label.show = false; g.width = 2; g.c_label = {GR_TICKMAJ, 0}; break;
    case GT_CLOCK:  // the built-in clock view's look (Gauge.cpp buildClock)
      g.number = {true, "", 48, 0, -10}; g.unit.show = false;
      g.label = {true, "{date}", 20, 0, 96};
      g.t_major = 12; g.t_minor = 1; g.t_inset = 8; g.t_len = 26;
      g.c_needle = {GR_ACCENT, 0};
      g.hand_h = 0.48f; g.hand_m = 0.69f; g.hand_s = 0.76f; g.show_sec = true;
      g.h = 132; g.seg_t = 15; g.n_hub = 14;
      break;
    case GT_IMAGE:  g.number.show = false; g.unit.show = false; g.label.show = false; g.img = -1; break;
  }
  g.note = {false, "", 16, 0, 104};
  g.pk   = {false, "", 20, 0, 104};
}

static bool parseGauge(JsonObjectConst o, GlGauge& g, char* why, size_t wn) {
  const char* ts = o["type"] | "";
  static const char* types[] = {"arc", "needle", "bar", "digital", "light", "text", "line", "clock", "image"};
  int type = -1;
  for (int i = 0; i < 9; i++) if (!strcmp(ts, types[i])) type = i;
  if (type < 0) { snprintf(why, wn, "unknown type '%s'", ts); return false; }
  int input = -1;
  if (type != GT_TEXT && type != GT_LINE && type != GT_CLOCK && type != GT_IMAGE) {
    const char* ik = o["input"] | "";
    input = gi_find(ik);
    if (input < 0) { snprintf(why, wn, "unknown input '%s'", ik); return false; }
  }
  setDefaults(g, (uint8_t)type, input);
  g.x = (int16_t)clampf(fnum(o, "x", g.x), -100, 580);
  g.y = (int16_t)clampf(fnum(o, "y", g.y), -100, 580);
  g.vmin = fnum(o, "min", g.vmin); g.vmax = fnum(o, "max", g.vmax);
  if (!(g.vmax > g.vmin)) { snprintf(why, wn, "max must be above min"); return false; }
  g.origin = fnull(o, "origin");                     // NAN = fill from the start of the scale
  g.vmin0 = g.vmin; g.vmax0 = g.vmax;
  g.decimals = (uint8_t)clampf(fnum(o, "decimals", g.decimals), 0, 3);
  { int u = unitByName(o["units"] | "auto"); g.units = (uint8_t)(u < 0 ? (int)GU_AUTO : u); }
  JsonObjectConst bu = o["by_unit"];                 // {"bar": {"min":0,"max":8,"major":4}, ...}
  if (!bu.isNull()) {
    for (JsonPairConst kv : bu) {
      int u = unitByName(kv.key().c_str());
      if (u <= 0 || u == GU_RAW || g.nscales >= GL_MAX_SCALES) continue;
      JsonObjectConst so = kv.value();
      GlScale& sc = g.scales[g.nscales];
      sc.unit = (uint8_t)u; sc.min = fnum(so, "min", NAN); sc.max = fnum(so, "max", NAN);
      sc.major = (uint8_t)clampf(fnum(so, "major", 0), 0, 20);
      sc.step = clampf(fnum(so, "step", 0), 0, 100000);
      if (!(isfinite(sc.min) && isfinite(sc.max) && sc.max > sc.min)) { snprintf(why, wn, "by_unit '%s': max must be above min", kv.key().c_str()); return false; }
      g.nscales++;
    }
  }

#ifdef GL_CAN_BUS
  { const char* fs = o["format"] | (const char*)nullptr;
    if (fs) g.gear_fmt = !strcmp(fs, "gear"); }
#endif

  JsonObjectConst c = o["colors"];
  if (!c.isNull()) {
    if (!parseColor(c["fill"], g.c_fill) || !parseColor(c["track"], g.c_track) ||
        !parseColor(c["text"], g.c_text) || !parseColor(c["label"], g.c_label) ||
        !parseColor(c["needle"], g.c_needle) || !parseColor(c["scale"], g.c_scale) ||
        !parseColor(c["ticks"], g.c_tmaj) || !parseColor(c["minor_ticks"], g.c_tmin) ||
        !parseColor(c["unit"], g.c_unit)) { snprintf(why, wn, "bad colour"); return false; }
    if (!c["nodata"].isNull()) {
      if (!parseColor(c["nodata"], g.c_nodata)) { snprintf(why, wn, "bad colour"); return false; }
      g.has_nodata = true;
    }
  }
  JsonObjectConst a = o["alerts"];
  if (!a.isNull()) {
    g.lc = fnull(a, "low_crit"); g.lw = fnull(a, "low_warn");
    g.hw = fnull(a, "high_warn"); g.hc = fnull(a, "high_crit");
#ifdef GL_CAN_BUS
    const char* st = a["status"] | (const char*)nullptr;
    if (st && st[0]) {
      int si = gi_find(st);
      if (si < 0 || GI_TABLE[si].kind != GI_OILST) { snprintf(why, wn, "alerts.status '%s' is not a status input", st); return false; }
      g.status_in = (int8_t)si;
    }
#endif
    JsonObjectConst ac = a["colors"];                 // {"low_warn":"#3a8dff", ...} - colour of that level's zone and of the reading in it
    if (!ac.isNull()) {
      static const char* ks[4] = {"low_crit", "low_warn", "high_warn", "high_crit"};
      for (int i = 0; i < 4; i++) {
        if (ac[ks[i]].isNull()) continue;
        if (!parseColor(ac[ks[i]], g.c_alert[i])) { snprintf(why, wn, "alerts.colors.%s: bad colour", ks[i]); return false; }
        g.ac_set |= (uint8_t)(1u << i);
      }
    }
    g.when_running = fbool(a, "when_running", false);
    g.flash = fbool(a, "flash", false);
    g.takeover = fbool(a, "takeover", false);
    g.num_alert = fbool(a, "number", true);
    JsonObjectConst wo = a["when"];
    if (!wo.isNull()) {
      const char* wi = wo["input"] | "";
      int k = gi_find(wi);
      if (k < 0) { snprintf(why, wn, "alerts.when: unknown input '%s'", wi); return false; }
      g.when_in = (int8_t)k; g.when_op = opOf(wo["op"] | ">="); g.when_v = fnum(wo, "value", 0);
    }
  }
  g.has_alert = isfinite(g.lc) || isfinite(g.lw) || isfinite(g.hw) || isfinite(g.hc);
#ifdef GL_CAN_BUS
  if (g.status_in >= 0) g.has_alert = true;
#endif
  JsonObjectConst an = o["anim"];
  if (!an.isNull()) {
    g.smooth = clampf(fnum(an, "smooth", g.smooth), 0, 0.95f);
    g.sweep_anim = fbool(an, "sweep", false);
    g.peak = fbool(an, "peak", false);
    g.peak_s = clampf(fnum(an, "peak_s", g.peak_s), 0.5f, 60);
  }
  // arc / needle
  g.r = (int16_t)clampf(fnum(o, "r", g.r), 20, 240);
  g.width = (int16_t)clampf(fnum(o, "width", g.width), 2, 80);
  g.a_start = fnum(o, "start", g.a_start);
  g.a_sweep = clampf(fnum(o, "sweep", g.a_sweep), -360, 360);   // negative = anticlockwise
  if (fabsf(g.a_sweep) < 10) { snprintf(why, wn, "sweep must be at least 10 degrees either way"); return false; }
  g.segments = (uint8_t)clampf(fnum(o, "segments", g.segments), 0, 60);
  g.zones = fbool(o, "zones", g.zones);
  g.rounded = fbool(o, "rounded", false);
  g.unlit_shade = clampf(fnum(o, "unlit_shade", 0), 0, 1);
  JsonObjectConst ro = o["ring"];                    // {"offset":-26,"width":10,"segments":0,"zones":true,"fill":false}
  if (!ro.isNull() && fbool(ro, "show", true)) {
    g.ring_off = (int16_t)clampf(fnum(ro, "offset", -26), -200, 200);
    g.ring_w = (uint8_t)clampf(fnum(ro, "width", 10), 1, 80);
    g.ring_seg = (uint8_t)clampf(fnum(ro, "segments", 0), 0, 60);
    g.ring_zones = fbool(ro, "zones", true);
    g.ring_fill = fbool(ro, "fill", false);
  }
  g.zone_fill = fbool(o, "zone_fill", false);
  JsonObjectConst go = o["gaps"];                    // {"width":6,"outer":18,"inner":18,"color":"dial"}
  if (!go.isNull()) {
    g.notch = true; g.c_gap = {GR_DIAL, 0};
    g.gap_w = (uint8_t)clampf(fnum(go, "width", 6), 1, 20);
    g.gap_out = (int16_t)clampf(fnum(go, "outer", 18), -100, 100);
    g.gap_in = (int16_t)clampf(fnum(go, "inner", 18), -100, 200);
    if (!parseColor(go["color"], g.c_gap)) { snprintf(why, wn, "bad colour"); return false; }
  }
  JsonObjectConst tk = o["ticks"];
  if (!tk.isNull()) {
    g.t_major = (uint8_t)clampf(fnum(tk, "major", g.t_major), 0, 20);
    g.t_minor = (uint8_t)clampf(fnum(tk, "minor", g.t_minor), 1, 10);
    g.t_step0 = clampf(fnum(tk, "step", 0), 0, 100000);
    g.t_numbers = fbool(tk, "numbers", g.t_numbers);
    g.t_font = (uint8_t)clampf(fnum(tk, "font", g.t_font), 8, 48);
    g.t_inset = (int16_t)clampf(fnum(tk, "inset", g.t_inset), -60, 120);
    g.t_len = (int16_t)clampf(fnum(tk, "length", g.t_len), 2, 60);
    if (tk["minor_length"].is<float>()) g.t_minlen = clampf(tk["minor_length"].as<float>(), 1, 60);
  }
  g.t_major0 = g.t_major; g.t_step = g.t_step0;
  for (JsonObjectConst mo : o["marks"].as<JsonArrayConst>()) {    // reference lines across the band
    if (g.nmarks >= GL_MAX_MARKS) break;
    GlMark& m = g.marks[g.nmarks];
    if (!mo["value"].is<float>()) { snprintf(why, wn, "marks: each needs a value"); return false; }
    m.value = mo["value"].as<float>(); m.c = {GR_TEXT, 0};
    if (!parseColor(mo["color"], m.c)) { snprintf(why, wn, "bad colour"); return false; }
    m.w = (uint8_t)clampf(fnum(mo, "width", 4), 1, 20);
    m.out = (int16_t)clampf(fnum(mo, "outer", 16), -100, 100);
    m.in = (int16_t)clampf(fnum(mo, "inner", 46), -100, 200);
    g.nmarks++;
  }
  JsonObjectConst nd = o["needle"];
  if (!nd.isNull()) {
    g.n_len = (int16_t)clampf(fnum(nd, "length", g.n_len), 10, 240);
    g.n_w = (int16_t)clampf(fnum(nd, "width", g.n_w), 1, 30);
    g.n_tail = (int16_t)clampf(fnum(nd, "tail", g.n_tail), 0, 120);
    g.n_hub = (int16_t)clampf(fnum(nd, "hub", g.n_hub), 0, 80);
  }
  // bar
  g.w = (int16_t)clampf(fnum(o, "w", g.w), 4, 480);
  g.h = (int16_t)clampf(fnum(o, "h", g.h), 4, 480);
  g.vertical = fbool(o, "vertical", false);
  // light
  g.size = (int16_t)clampf(fnum(o, "size", g.size), 6, 400);
  g.square = !strcmp(o["shape"] | "circle", "square");
  JsonObjectConst on = o["on"];
  if (!on.isNull()) {
    const char* op = on["op"] | ">=";
    static const char* ops[] = {">=", ">", "<=", "<", "==", "!="};
    g.op = GO_GE;
    for (int i = 0; i < 6; i++) if (!strcmp(op, ops[i])) g.op = (uint8_t)i;
    g.on_value = fnum(on, "value", g.on_value);
  }
  if (!parseColor(o["on_color"], g.c_on) || !parseColor(o["off_color"], g.c_off)) {
    snprintf(why, wn, "bad colour"); return false;
  }
  g.blink = fbool(o, "blink", false);
  // line: from (x, y) to (x2, y2)
  g.x2 = (int16_t)clampf(fnum(o, "x2", g.x + 100), -100, 580);
  g.y2 = (int16_t)clampf(fnum(o, "y2", g.y), -100, 580);
  if (type == GT_LINE) {
    g.width = (int16_t)clampf(fnum(o, "width", 2), 1, 40);
    if (!parseColor(o["color"], g.c_label)) { snprintf(why, wn, "bad colour"); return false; }
  }
  // text gauge: text/font/color at top level
  if (type == GT_TEXT) {
    scopy(g.label.text, o["text"] | "", sizeof g.label.text);
    g.label.font = (uint8_t)clampf(fnum(o, "font", 20), 8, 48);
    if (!parseColor(o["color"], g.c_label)) { snprintf(why, wn, "bad colour"); return false; }
  }
  if (type == GT_CLOCK) {
    const char* st = o["style"] | "auto";
    g.clk_style = !strcmp(st, "analog") ? 1 : !strcmp(st, "digital") ? 2 : 0;
    JsonObjectConst hd = o["hands"];                 // {"hour":0.48,"minute":0.69,"second":0.76,"seconds":true}
    if (!hd.isNull()) {
      g.hand_h = clampf(fnum(hd, "hour", g.hand_h), 0.05f, 1.2f);
      g.hand_m = clampf(fnum(hd, "minute", g.hand_m), 0.05f, 1.2f);
      g.hand_s = clampf(fnum(hd, "second", g.hand_s), 0.05f, 1.2f);
      g.show_sec = fbool(hd, "seconds", true);
    }
    g.h = (int16_t)clampf(fnum(o, "digit_height", 132), 20, 300);
    g.seg_t = (uint8_t)clampf(fnum(o, "thickness", lroundf(15.0f * g.h / 132.0f)), 2, 60);
    g.label_analog = fbool(o, "date_on_analog", false);
    g.t_major = 12;
    if (g.t_minor > 5) g.t_minor = 5;                  // 5 = a tick every minute
  }
  if (type == GT_IMAGE) {
    const char* nm = o["image"] | "";
    g.img = -1;
    for (int i = 0; i < s_nimgs; i++) if (s_imgs[i].name && !strcmp(s_imgs[i].name, nm)) { g.img = (int8_t)i; break; }
    if (g.img < 0) { snprintf(why, wn, "unknown image '%s' (not in this firmware)", nm); return false; }
    if (!o["tint"].isNull()) {
      if (!parseColor(o["tint"], g.c_tint)) { snprintf(why, wn, "bad colour"); return false; }
      g.has_tint = true;
    }
  }
  parsePart(o, "number", g.number);
  g.glow = clampf(fnum(o, "glow", 0), 0, 1);
  g.core = fbool(o, "core", false);
  g.n_fixed = type == GT_DIGITAL && !GL_GEARFMT(g) && fbool(o["number"], "fixed", true);   // only the Big number gauge; arcs, needles and bars centre their number as before
  { JsonObjectConst no7 = o["number"];                 // {"style":"seg7","height":64,"digits":3,"slant":8,"thickness":0,"ghost":0.12,"glow":0.7}
    if (!no7.isNull() && !strcmp(no7["style"] | "", "seg7")) {
      g.n7 = true;
      g.n7_h = (uint8_t)clampf(fnum(no7, "height", g.number.font * 1.3f), 16, 200);
      g.n7_t = (uint8_t)clampf(fnum(no7, "thickness", 0), 0, 40);
      g.n7_digits = (uint8_t)clampf(fnum(no7, "digits", 0), 0, 8);
      g.n7_slant = clampf(fnum(no7, "slant", 8), 0, 20);
      g.n7_ghost = clampf(fnum(no7, "ghost", g.n7_digits ? 0.12f : 0), 0, 0.6f);
      g.n7_glow = clampf(fnum(no7, "glow", 0.7f), 0, 1);   // the number's own glow; the gauge-level "glow" is for the band
      g.n7_core = fbool(no7, "core", true);
    } }
  parsePart(o, "label", g.label);
  parsePart(o, "unit", g.unit);
  if (type == GT_TEXT) { g.label.show = true; g.label.dx = 0; g.label.dy = 0; }
  JsonObjectConst no = o["note"];                    // {"none":"no sensor","ok":"on load","high_crit":"LEAN",...}
  if (!no.isNull()) {
    static const char* keys[NS_COUNT] = {"none", "idle", "ok", "low_crit", "low_warn", "high_warn", "high_crit"};
    bool any = false;
    for (int i = 0; i < NS_COUNT; i++) { scopy(g.notes[i], no[keys[i]] | "", sizeof g.notes[i]); any |= g.notes[i][0] != 0; }
    g.note.show = any;
    parsePart(o, "note", g.note);
    g.note.text[0] = 0;
  }
  JsonObjectConst po = o["peak_label"];             // {"text":"peak ","font":20,"dy":104,"start":0}
  if (!po.isNull()) {
    g.pk.show = true; scopy(g.pk.text, "peak ", sizeof g.pk.text);
    parsePart(o, "peak_label", g.pk);
    if (po["text"].is<const char*>()) g.pk.show = fbool(po, "show", true);   // "" prefix is fine
    g.pk_start = fnull(po, "start");
  }
  refreshUnits(g);
  return true;
}

static void freeViews(GlView* v, int n) {
  for (int i = 0; i < n; i++) { free(v[i].g); v[i].g = nullptr; v[i].n = 0; }
}

bool GL_Load(const char* json, size_t len, char* err, size_t errlen) {
  if (err && errlen) err[0] = 0;
  char why[96];
#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(len * 2 + 8192);
#endif
  DeserializationError de = deserializeJson(doc, json, len);
  if (de) { snprintf(err, errlen, "not valid JSON: %s", de.c_str()); return false; }
  if (strcmp(doc["format"] | "", "370z-gauge-layout")) { snprintf(err, errlen, "not a 370z gauge layout file"); return false; }
  int ver = doc["version"] | 0;
  if (ver != 1) { snprintf(err, errlen, "layout version %d not supported (this screen reads version 1)", ver); return false; }
  JsonArrayConst views = doc["views"];
  if ((int)views.size() > GL_MAX_VIEWS) { snprintf(err, errlen, "too many views (max %d)", GL_MAX_VIEWS); return false; }

  static GlView nv[GL_MAX_VIEWS];               // static: keeps 1.5 KB off the task stack
  memset(nv, 0, sizeof nv);
  int n = 0, ncustom = 0;
  for (JsonObjectConst vo : views) {
    GlView& v = nv[n];
    const char* id = vo["id"] | "";
    bool okid = id[0] != 0 && strlen(id) < sizeof v.id;
    for (const char* p = id; *p; p++) if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) okid = false;
    if (!okid) { snprintf(err, errlen, "view %d: id must be 1-15 of a-z 0-9 _", n + 1); freeViews(nv, n); return false; }
    for (int k = 0; k < n; k++) if (!strcmp(nv[k].id, id)) { snprintf(err, errlen, "view %d: id '%s' used twice", n + 1, id); freeViews(nv, n); return false; }
    scopy(v.id, id, sizeof v.id);
    scopy(v.name, vo["name"] | id, sizeof v.name);
    v.face = fbool(vo, "face", true);
    v.has_bg = false;
    if (!vo["bg"].isNull()) {
      if (!parseColor(vo["bg"], v.bg) || v.bg.role == GR_DIAL) {
        snprintf(err, errlen, "view %d '%s': bad bg colour (\"#rrggbb\" or a theme colour name)", n + 1, id);
        freeViews(nv, n); return false;
      }
      v.has_bg = true;
    }
    v.enabled = fbool(vo, "enabled", true);
    const char* bi = vo["builtin"] | "";
    if (bi[0]) {
      bool okb = strlen(bi) < sizeof v.builtin;
      for (const char* p = bi; *p; p++) if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) okb = false;
      if (!okb) { snprintf(err, errlen, "view %d: builtin must be 1-15 of a-z 0-9 _", n + 1); freeViews(nv, n); return false; }
      for (int k = 0; k < n; k++) if (!strcmp(nv[k].builtin, bi)) { snprintf(err, errlen, "view %d: built-in view '%s' listed twice", n + 1, bi); freeViews(nv, n); return false; }
      scopy(v.builtin, bi, sizeof v.builtin);
    } else if (++ncustom > GL_MAX_CUSTOM) {
      snprintf(err, errlen, "too many custom views (max %d)", GL_MAX_CUSTOM); freeViews(nv, n); return false;
    }
    v.screens = 0;
    for (JsonVariantConst s : vo["screens"].as<JsonArrayConst>()) { int sid = s | 0; if (sid >= 1 && sid <= 15) v.screens |= (1u << sid); }
    JsonArrayConst ga = vo["gauges"];
    if (v.builtin[0]) ga = JsonArrayConst();     // a built-in view is drawn by the screen code
    if ((int)ga.size() > GL_MAX_GAUGES) { snprintf(err, errlen, "view %d '%s': too many gauges (max %d)", n + 1, id, GL_MAX_GAUGES); freeViews(nv, n); return false; }
    v.n = (uint8_t)ga.size();
    v.g = v.n ? (GlGauge*)glCalloc(v.n, sizeof(GlGauge)) : nullptr;
    if (v.n && !v.g) { snprintf(err, errlen, "out of memory"); freeViews(nv, n); return false; }
    int gi = 0;
    for (JsonObjectConst go : ga) {
      if (!parseGauge(go, v.g[gi], why, sizeof why)) {
        snprintf(err, errlen, "view %d '%s', gauge %d: %s", n + 1, id, gi + 1, why);
        freeViews(nv, n + 1); return false;
      }
      gi++;
    }
    n++;
  }
  // success: swap in
  GL_Destroy();
  freeViews(s_views, s_nviews);
  memcpy(s_views, nv, sizeof nv);
  s_nviews = n;
  return true;
}

int         GL_ViewCount() { return s_nviews; }
const char* GL_ViewId(int v)   { return (v >= 0 && v < s_nviews) ? s_views[v].id : ""; }
const char* GL_ViewName(int v) { return (v >= 0 && v < s_nviews) ? s_views[v].name : ""; }
bool        GL_ViewFace(int v) { return (v >= 0 && v < s_nviews) ? s_views[v].face : true; }
bool GL_ViewBg(int v, lv_color_t* out) {
  if (v < 0 || v >= s_nviews || !s_views[v].has_bg) return false;
  if (out) *out = palOf(s_views[v].bg);
  return true;
}
const char* GL_ViewBuiltin(int v) { return (v >= 0 && v < s_nviews) ? s_views[v].builtin : ""; }
bool        GL_ViewEnabled(int v) { return v >= 0 && v < s_nviews && s_views[v].enabled; }
bool GL_HasBuiltins() {
  for (int v = 0; v < s_nviews; v++) if (s_views[v].builtin[0]) return true;
  return false;
}
bool GL_ViewUsesInput(int v, int input) {
  if (v < 0 || v >= s_nviews) return false;
  for (int i = 0; i < s_views[v].n; i++) if (s_views[v].g[i].input == input) return true;
  return false;
}
// Forget every peak marker and peak-hold label (after a demo: its highs must never look like real readings).
void GL_ResetPeaks() {
  for (int vi = 0; vi < s_nviews; vi++)
    for (int i = 0; i < s_views[vi].n; i++) { GlGauge& g = s_views[vi].g[i]; g.peak_valid = false; g.pk_have = false; g.dirty = true; }
}
bool GL_ViewOnScreen(int v, int sid) {
  if (v < 0 || v >= s_nviews || !s_views[v].enabled) return false;
  return s_views[v].screens == 0 || (sid >= 1 && sid <= 15 && (s_views[v].screens & (1u << sid)));
}
void GL_SetPalette(const GlPalette& p) { s_pal = p; if (s_obj) lv_obj_invalidate(s_obj); }
void GL_SetImages(const GlImage* list, int n) { s_imgs = list; s_nimgs = (list && n > 0) ? (n > 127 ? 127 : n) : 0; }
void GL_SetClock(const GlClock& c) { s_clk = c; }
void GL_SetClockFace(bool analog) { s_clk_analog = analog; }
void GL_SetUnits(const GlUnits& u) {
  s_units = u;
  if (s_built >= 0) {
    GlView& v = s_views[s_built];
    for (int i = 0; i < v.n; i++) { refreshUnits(v.g[i]); v.g[i].dirty = true; }
  }
}

// ------------------------------------------------------------------ alerts ---
// Alert level (0 normal, 1 amber, 2 red) and what the note should say.
static int judge(const GlGauge& g, const float* kv, uint8_t* ns) {
  float v = g.input >= 0 ? kv[g.input] : NAN;
  if (!isfinite(v)) { *ns = NS_NONE; return 0; }
  bool gate = true;                                   // conditions for judging at all
  if (g.when_running && !GL_ENGINE_RUNNING(kv)) gate = false;
  if (g.when_in >= 0) { float w = kv[g.when_in]; if (!(isfinite(w) && cmpOp(g.when_op, w, g.when_v))) gate = false; }
  if (!gate) { *ns = NS_IDLE; return 0; }
#ifdef GL_CAN_BUS
  if (g.status_in >= 0 && isfinite(kv[g.status_in])) {   // e.g. the brain's oil verdict: 1 ok, 2 amber, 3 red
    int st = (int)lroundf(kv[g.status_in]);
    *ns = st >= 3 ? NS_HC : st == 2 ? NS_HW : NS_OK;
    return st >= 3 ? 2 : st == 2 ? 1 : 0;
  }
#endif
  if (isfinite(g.lc) && v <= g.lc) { *ns = NS_LC; return 2; }
  if (isfinite(g.hc) && v >= g.hc) { *ns = NS_HC; return 2; }
  if (isfinite(g.lw) && v <= g.lw) { *ns = NS_LW; return 1; }
  if (isfinite(g.hw) && v >= g.hw) { *ns = NS_HW; return 1; }
  *ns = NS_OK; return 0;
}
// A light's threshold is in the PSI / degC / mph the reading arrives in, unless the light names a unit
// ("units": "bar", "kpa", "f", "kmh"...): then it is compared in that unit.
static bool litOf(const GlGauge& g, float v) {
  if (!isfinite(v)) return false;
  if (g.units != GU_AUTO) v = toDisp(g, v);
  return cmpOp(g.op, v, g.on_value);
}

int GL_TakeoverWanted(const float* kv, int sid) {
  for (int vi = 0; vi < s_nviews; vi++) {
    if (!GL_ViewOnScreen(vi, sid)) continue;
    GlView& v = s_views[vi];
    for (int i = 0; i < v.n; i++) {
      const GlGauge& g = v.g[i];
      if (!g.takeover || g.input < 0) continue;
      uint8_t ns;
      if (judge(g, kv, &ns) == 2) return vi;
    }
  }
  return -1;
}

// ---------------------------------------------------------------- drawing ---
static inline float d2r(float d) { return (d - 90.0f) * 3.14159265f / 180.0f; }
static inline void ptOn(float cx, float cy, float a, float r, lv_coord_t& x, lv_coord_t& y) {
  x = (lv_coord_t)lroundf(cx + r * cosf(d2r(a)));
  y = (lv_coord_t)lroundf(cy + r * sinf(d2r(a)));
}

// band from angle a0 to a1 (our angles: 0 = top, clockwise), centred on radius r
static void band(lv_draw_ctx_t* ctx, float cx, float cy, float r, float w, float a0, float a1,
                 lv_color_t col, lv_opa_t opa) {
  if (a1 - a0 < 0.5f) return;
  lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d);
  d.color = col; d.width = (lv_coord_t)w; d.opa = opa; d.rounded = 0;
  lv_point_t c = {(lv_coord_t)lroundf(cx), (lv_coord_t)lroundf(cy)};
  int s = (int)lroundf(a0 + 270.0f), e = (int)lroundf(a1 + 270.0f);
  if (e - s >= 360) { s = 0; e = 360; } else { s %= 360; e %= 360; }
  if (s == e) return;
  lv_draw_arc(ctx, &d, &c, (uint16_t)lroundf(r + w / 2.0f), (uint16_t)s, (uint16_t)e);
}
static void line(lv_draw_ctx_t* ctx, lv_coord_t x0, lv_coord_t y0, lv_coord_t x1, lv_coord_t y1,
                 int w, lv_color_t col, bool round = true) {
  lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
  d.color = col; d.width = (lv_coord_t)w; d.round_start = round; d.round_end = round;
  lv_point_t a = {x0, y0}, b = {x1, y1};
  lv_draw_line(ctx, &d, &a, &b);
}
static void rect(lv_draw_ctx_t* ctx, lv_coord_t x0, lv_coord_t y0, lv_coord_t x1, lv_coord_t y1,
                 lv_color_t col, lv_coord_t radius, lv_opa_t opa = LV_OPA_COVER) {
  if (x1 < x0 || y1 < y0) return;
  lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
  d.bg_color = col; d.bg_opa = opa; d.radius = radius; d.border_width = 0;
  lv_area_t a = {x0, y0, x1, y1};
  lv_draw_rect(ctx, &d, &a);
}
static void text(lv_draw_ctx_t* ctx, lv_coord_t cx, lv_coord_t cy, const char* t, int size, lv_color_t col) {
  if (!t || !t[0]) return;
  const lv_font_t* f = fontFor(size);
  lv_point_t sz;
  lv_txt_get_size(&sz, t, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_draw_label_dsc_t d; lv_draw_label_dsc_init(&d);
  d.color = col; d.font = f; d.align = LV_TEXT_ALIGN_CENTER;
  lv_area_t a = {(lv_coord_t)(cx - sz.x / 2 - 1), (lv_coord_t)(cy - sz.y / 2),
                 (lv_coord_t)(cx + (sz.x + 1) / 2 + 1), (lv_coord_t)(cy + (sz.y + 1) / 2)};
  lv_draw_label(ctx, &d, &a, t, nullptr);
}

static float fracOf(const GlGauge& g, float v) { return clampf((v - g.vmin) / (g.vmax - g.vmin), 0, 1); }
static float originOf(const GlGauge& g) { return isfinite(g.origin) ? g.origin : g.vmin; }

// The coloured ranges on the track: [from, to, level] in base units.
// z[][2] is the alert level's index: 0 low_crit, 1 low_warn, 2 high_warn, 3 high_crit.
static lv_color_t alertCol(const GlGauge& g, int k) {
  if (g.ac_set & (1u << k)) return colOf(g.c_alert[k]);
  return (k == 0 || k == 3) ? s_pal.crit : s_pal.warn;
}
static int zonesOf(const GlGauge& g, float z[4][3]) {
  int n = 0;
  if (!g.has_alert) return 0;
  if (isfinite(g.lc)) { z[n][0] = g.vmin; z[n][1] = g.lc; z[n][2] = 0; n++; }
  if (isfinite(g.lw)) { z[n][0] = isfinite(g.lc) ? g.lc : g.vmin; z[n][1] = g.lw; z[n][2] = 1; n++; }
  if (isfinite(g.hw)) { z[n][0] = g.hw; z[n][1] = isfinite(g.hc) ? g.hc : g.vmax; z[n][2] = 2; n++; }
  if (isfinite(g.hc)) { z[n][0] = g.hc; z[n][1] = g.vmax; z[n][2] = 3; n++; }
  return n;
}

// Shaded / zone-coloured bands: the colour of the scale at fraction f.
// Lit: the fill colour (or the zone's colour with zone_fill). Unlit: the track
// colour, or with unlit_shade the lit colour mixed into the track.
static lv_color_t zoneColAt(const GlGauge& g, const float z[4][3], int nz, float f, lv_color_t base) {
  for (int k = 0; k < nz; k++) {
    float za = fracOf(g, z[k][0]), zb = fracOf(g, z[k][1]);
    if (f >= za && f <= zb) return alertCol(g, (int)z[k][2]);
  }
  return base;
}
static bool fancyBand(const GlGauge& g) { return g.unlit_shade > 0.0f || g.zone_fill; }
static lv_color_t bandColAt(const GlGauge& g, const float z[4][3], int nz, float f, bool lit) {
  lv_color_t base = colOf(g.c_fill), trk = colOf(g.c_track);
  if (lit) {
    if (g.d_dark) return trk;                                   // flashing: dark phase
    return g.zone_fill ? zoneColAt(g, z, nz, f, colOf(g.c_fill)) : g.d_fill;   // each block keeps its zone colour
  }
  if (g.unlit_shade <= 0.0f) return trk;
  lv_color_t lc = g.zones ? zoneColAt(g, z, nz, f, base) : base;   // dim amber/red in the zones, if shown
  return lv_color_mix(lc, trk, (uint8_t)lroundf(g.unlit_shade * 255.0f));
}
// Split [a, b] at the zone edges and the reading's ends, calling out(a, b, colour).
template <class F> static void bandPieces(const GlGauge& g, const float z[4][3], int nz, float a, float b,
                                          bool lit_ok, float f0, float f1, F out) {
  float cut[12]; int n = 0;
  cut[n++] = a;
  auto add = [&](float x) { if (x > a && x < b && n < 11) cut[n++] = x; };
  for (int k = 0; k < nz; k++) { add(fracOf(g, z[k][0])); add(fracOf(g, z[k][1])); }
  if (lit_ok) { add(f0); add(f1); }
  cut[n++] = b;
  for (int i = 1; i < n; i++) for (int j = i; j > 0 && cut[j] < cut[j - 1]; j--) { float t = cut[j]; cut[j] = cut[j - 1]; cut[j - 1] = t; }
  for (int i = 0; i + 1 < n; i++) {
    float p = cut[i], q = cut[i + 1];
    if (q - p < 1e-5f) continue;
    float m = (p + q) / 2;
    bool lit = lit_ok && m >= f0 && m <= f1;
    out(p, q, bandColAt(g, z, nz, m, lit));
  }
}


// ---- glow helpers ------------------------------------------------------------
static lv_color_t hot(lv_color_t c) { return lv_color_mix(lv_color_white(), c, 90); }   // brighter centre of a lit part

// ---- seven-segment numbers -----------------------------------------------------
// bits: 0 top, 1 upper right, 2 lower right, 3 bottom, 4 lower left, 5 upper left, 6 middle
static uint8_t seg7Mask(char ch) {
  static const uint8_t D[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
  if (ch >= '0' && ch <= '9') return D[ch - '0'];
  switch (ch >= 'a' && ch <= 'z' ? ch - 32 : ch) {
    case '-': return 0x40; case '_': return 0x08;
    case 'A': return 0x77; case 'B': return 0x7C; case 'C': return 0x39; case 'D': return 0x5E;
    case 'E': return 0x79; case 'F': return 0x71; case 'H': return 0x76; case 'L': return 0x38;
    case 'N': return 0x54; case 'O': return 0x5C; case 'P': return 0x73; case 'R': return 0x50;
    case 'S': return 0x6D; case 'U': return 0x3E;
    default: return 0;
  }
}
// One pointed segment along the line P-Q, thickness T, sheared (italic) about y = cy.
static void seg7Poly(lv_draw_ctx_t* ctx, lv_color_t col, lv_opa_t opa, float px, float py, float qx, float qy,
                     float T, float cy, float shear) {
  float dx = qx - px, dy = qy - py, L = sqrtf(dx * dx + dy * dy);
  if (L < 1.0f) return;
  dx /= L; dy /= L;
  const float nx = -dy, ny = dx, h = T / 2, tip = T * 0.16f, sh = tip + h;     // shoulders sit one half-stroke in from the tips
  float xs[6] = {px + dx * tip, px + dx * sh + nx * h, qx - dx * sh + nx * h, qx - dx * tip, qx - dx * sh - nx * h, px + dx * sh - nx * h};
  float ys[6] = {py + dy * tip, py + dy * sh + ny * h, qy - dy * sh + ny * h, qy - dy * tip, qy - dy * sh - ny * h, py + dy * sh - ny * h};
  lv_point_t pts[6];
  for (int i = 0; i < 6; i++) { pts[i].x = (lv_coord_t)lroundf(xs[i] + (cy - ys[i]) * shear); pts[i].y = (lv_coord_t)lroundf(ys[i]); }
  lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
  d.bg_color = col; d.bg_opa = opa; d.border_width = 0;
  lv_draw_polygon(ctx, &d, pts, 6);
}
// Soft glow round a segment: a few wide, faint rounded boxes (no sideways lean: the halo is soft).
static void seg7Halo(lv_draw_ctx_t* ctx, lv_color_t col, float glow, float px, float py, float qx, float qy,
                     float T, float cy, float shear) {
  const float mx = (px + qx) / 2 + (cy - (py + qy) / 2) * shear, my = (py + qy) / 2;
  const bool horiz = fabsf(qx - px) >= fabsf(qy - py);
  const float L = fmaxf(fabsf(qx - px), fabsf(qy - py));
  static const float grow[3] = {1.25f, 0.7f, 0.3f};
  static const float opa[3]  = {0.07f, 0.12f, 0.2f};
  for (int k = 0; k < 3; k++) {
    float e = T * grow[k], hw = (horiz ? L : T) / 2 + e, hh = (horiz ? T : L) / 2 + e;
    rect(ctx, (lv_coord_t)lroundf(mx - hw), (lv_coord_t)lroundf(my - hh), (lv_coord_t)lroundf(mx + hw), (lv_coord_t)lroundf(my + hh),
         col, LV_RADIUS_CIRCLE, (lv_opa_t)lroundf(255.0f * opa[k] * glow));
  }
}
// Fixed-width numbers. The width comes from the widest value the scale can show (digits before the
// point, a minus sign if the scale goes below zero, and the decimals), so the reading does not
// slide about as it goes from 9 to 10. Digits sit in equal cells either side of the decimal
// point, so the point itself never moves; a reading wider than the scale allows spills to the left.
static void numShape(const GlGauge& g, int& intCells, bool& neg, int& dp) {
  dp = decimalsOf(g);
  float a = toDisp(g, g.vmin), b = toDisp(g, g.vmax);
  neg = a < 0.0f || b < 0.0f;
  char t[24]; snprintf(t, sizeof t, "%.*f", dp, (double)fmaxf(fabsf(a), fabsf(b)));
  intCells = 0;
  for (const char* p = t; *p && *p != '.'; p++) if (*p >= '0' && *p <= '9') intCells++;
  if (intCells < 1) intCells = 1;
}
static bool plainNumber(const char* t) {          // digits, one point, a leading minus
  int digits = 0;
  for (const char* p = t; *p; p++) {
    if (*p >= '0' && *p <= '9') digits++;
    else if (*p == '.' || (*p == '-' && p == t)) continue;
    else return false;
  }
  return digits > 0;
}
static void drawNumberFixed(lv_draw_ctx_t* ctx, const GlGauge& g, lv_coord_t cx, lv_coord_t cy) {
  const lv_font_t* f = fontFor(g.number.font);
  int dw = 0;
  for (char c = '0'; c <= '9'; c++) { int w = (int)lv_font_get_glyph_width(f, (uint32_t)c, 0); if (w > dw) dw = w; }
  const int dotw = (int)lv_font_get_glyph_width(f, '.', 0), minw = (int)lv_font_get_glyph_width(f, '-', 0);
  int ic, dp; bool neg; numShape(g, ic, neg, dp);
  const int whole = (neg ? minw : 0) + ic * dw + (dp ? dotw + dp * dw : 0);
  const int anchor = cx - whole / 2 + (neg ? minw : 0) + ic * dw;        // where the integer digits end
  const char* t = g.d_txt;
  const char* dot = strchr(t, '.');
  const int nint = dot ? (int)(dot - t) : (int)strlen(t);
  const bool minus = t[0] == '-';
  const int ndig = nint - (minus ? 1 : 0);
  char s[2] = {0, 0};
  for (int i = 0; i < ndig; i++) {                                        // integer digits, right-aligned on the point
    s[0] = t[nint - 1 - i];
    text(ctx, (lv_coord_t)(anchor - i * dw - dw / 2), cy, s, g.number.font, g.d_num);
  }
  if (minus) { s[0] = '-'; text(ctx, (lv_coord_t)(anchor - ndig * dw - minw / 2), cy, s, g.number.font, g.d_num); }
  if (dot) {
    s[0] = '.'; text(ctx, (lv_coord_t)(anchor + dotw / 2), cy, s, g.number.font, g.d_num);
    for (int i = 0; dot[1 + i]; i++) { s[0] = dot[1 + i]; text(ctx, (lv_coord_t)(anchor + dotw + i * dw + dw / 2), cy, s, g.number.font, g.d_num); }
  }
}
static void drawSeg7(lv_draw_ctx_t* ctx, const GlGauge& g, float cx, float cy) {
  struct Item { uint8_t mask; bool dot; };
  Item it[12]; int n = 0;
  for (const char* p = g.d_txt; *p && n < 12; p++) {
    if (*p == '.' && n > 0 && !it[n - 1].dot) { it[n - 1].dot = true; continue; }
    if (*p == ' ') { it[n].mask = 0; it[n].dot = false; n++; continue; }
    it[n].mask = seg7Mask(*p); it[n].dot = *p == '.'; n++;
  }
  if (!n) return;
  const float H = g.n7_h, W = H * 0.56f, T = g.n7_t ? (float)g.n7_t : fmaxf(3.0f, H * 0.13f);
  const float pitch = W + T * 1.35f, shear = tanf(g.n7_slant * 0.0174533f);
  int want = g.n7_digits;                      // 0 = as wide as the scale needs
  if (!want && g.n_fixed) { int ic, dp; bool ng; numShape(g, ic, ng, dp); want = ic + dp + (ng ? 1 : 0); }
  const int cells = want > n ? want : n;
  const float total = cells * pitch - T * 1.35f;
  const float x0 = cx + g.number.dx - total / 2, y0 = cy + g.number.dy - H / 2, ymid = y0 + H / 2;
  lv_color_t col = g.d_num;
  const bool fixed = g.n7_digits > 0;
  // segment centre lines for a cell at x: a, b, c, d, e, f, g
  auto cl = [&](float x, int s, float& ax, float& ay, float& bx, float& by) {
    const float l = x + T / 2, r = x + W - T / 2, t = y0 + T / 2, m = ymid, b = y0 + H - T / 2;
    switch (s) {
      case 0: ax = l; ay = t; bx = r; by = t; break;
      case 1: ax = r; ay = t; bx = r; by = m; break;
      case 2: ax = r; ay = m; bx = r; by = b; break;
      case 3: ax = l; ay = b; bx = r; by = b; break;
      case 4: ax = l; ay = m; bx = l; by = b; break;
      case 5: ax = l; ay = t; bx = l; by = m; break;
      default: ax = l; ay = m; bx = r; by = m; break;
    }
  };
  auto dotAt = [&](float x, lv_color_t c, lv_opa_t opa, bool halo) {
    float dxp = x + W + T * 0.55f, dyp = y0 + H - T * 0.5f;
    float sx = dxp + (ymid - dyp) * shear;
    if (halo) {
      static const float grow[3] = {1.25f, 0.7f, 0.3f}, o[3] = {0.07f, 0.12f, 0.2f};
      for (int k = 0; k < 3; k++) { float e = T * (0.5f + grow[k]);
        rect(ctx, (lv_coord_t)lroundf(sx - e), (lv_coord_t)lroundf(dyp - e), (lv_coord_t)lroundf(sx + e), (lv_coord_t)lroundf(dyp + e), c, LV_RADIUS_CIRCLE, (lv_opa_t)lroundf(255.0f * o[k] * g.n7_glow)); }
      return;
    }
    rect(ctx, (lv_coord_t)lroundf(sx - T / 2), (lv_coord_t)lroundf(dyp - T / 2), (lv_coord_t)lroundf(sx + T / 2), (lv_coord_t)lroundf(dyp + T / 2), c, 1, opa);
  };
  const int first = cells - n;                 // cells before the text (fixed-width fields are right-aligned)
  // 1. unlit segments (ghost) in every cell of a fixed field
  if (fixed && g.n7_ghost > 0.0f) {
    const lv_opa_t go = (lv_opa_t)lroundf(255.0f * g.n7_ghost);
    for (int c = 0; c < cells; c++) {
      float x = x0 + c * pitch, ax, ay, bx, by;
      for (int s = 0; s < 7; s++) { cl(x, s, ax, ay, bx, by); seg7Poly(ctx, col, go, ax, ay, bx, by, T, ymid, shear); }
      dotAt(x, col, go, false);
    }
  }
  // 2. glow, then the lit segments on top
  lv_color_t core = (g.n7_core && g.n7_glow > 0.0f) ? hot(col) : col;
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < n; i++) {
      float x = x0 + (first + i) * pitch, ax, ay, bx, by;
      for (int s = 0; s < 7; s++) {
        if (!(it[i].mask & (1 << s))) continue;
        cl(x, s, ax, ay, bx, by);
        if (pass == 0) { if (g.n7_glow > 0.0f) seg7Halo(ctx, col, g.n7_glow, ax, ay, bx, by, T, ymid, shear); }
        else seg7Poly(ctx, core, LV_OPA_COVER, ax, ay, bx, by, T, ymid, shear);
      }
      if (it[i].dot) { if (pass == 0) { if (g.n7_glow > 0.0f) dotAt(x, col, LV_OPA_COVER, true); } else dotAt(x, core, LV_OPA_COVER, false); }
    }
  }
}

static void drawDial(lv_draw_ctx_t* ctx, const GlGauge& g, float cx, float cy) {
  // Work in fractions of the scale (0 = min end, 1 = max end) and only turn
  // them into angles when drawing, so a negative sweep (anticlockwise, e.g. a
  // gauge round the bottom with its minimum on the left) needs no special cases.
  const float r = g.r, w = g.width, a0 = g.a_start, sw = g.a_sweep;
  auto ang = [&](float f) { return a0 + sw * f; };
  auto bandF = [&](float fa, float fb, lv_color_t c, lv_opa_t opa) {
    if (fb <= fa) return;
    float x = ang(fa), y = ang(fb);
    band(ctx, cx, cy, r, w, fminf(x, y), fmaxf(x, y), c, opa);
  };
  // second ring: the scale's zones (and the reading if asked) on a band beside the main one
  if (g.ring_w) {
    float rr = r + g.ring_off, rw = g.ring_w;
    float rz[4][3]; int rnz = (g.ring_zones || g.zone_fill) ? zonesOf(g, rz) : 0;
    const bool rcut = g.ring_seg > 1;                  // blocks = one solid ring with even gap lines cut across it (below)
    int rs = rcut ? 1 : (g.ring_seg ? g.ring_seg : 1);
    float rgap = (g.ring_seg && !rcut) ? ((6.0f / rr) * 57.2958f) / fabsf(sw) : 0.0f;
    float q0 = fracOf(g, originOf(g)), q1 = g.d_valid ? g.d_frac : q0;
    if (q1 < q0) { float t = q0; q0 = q1; q1 = t; }
    lv_color_t trk = colOf(g.c_track);
    auto ringF = [&](float fa, float fb, lv_color_t c) {
      if (fb <= fa) return;
      float x = ang(fa), y = ang(fb);
      band(ctx, cx, cy, rr, rw, fminf(x, y), fmaxf(x, y), c, LV_OPA_COVER);
    };
    for (int i = 0; i < rs; i++) {
      float s0 = (float)i / rs + rgap / 2, s1 = (float)(i + 1) / rs - rgap / 2;
      bandPieces(g, rz, rnz, s0, s1, g.ring_fill && g.d_valid, q0, q1, [&](float p, float q, lv_color_t c) {
        float m = (p + q) / 2;
        bool lit = g.ring_fill && g.d_valid && m >= q0 && m <= q1;
        if (!lit) {                                    // unlit: track, with the zones blended in
          lv_color_t zc = trk;
          for (int k = 0; k < rnz && g.ring_zones; k++)
            if (m >= fracOf(g, rz[k][0]) && m <= fracOf(g, rz[k][1])) zc = lv_color_mix(alertCol(g, (int)rz[k][2]), trk, 180);
          c = zc;
        }
        ringF(p, q, c);
      });
    }
  }
  if (g.ring_w && g.ring_seg > 1) {                      // even gaps in the second ring
    float rr = r + g.ring_off, rw = g.ring_w;
    GlColor dc = {GR_DIAL, 0}; lv_color_t gcol = colOf(dc);
    for (int i = 1; i < g.ring_seg; i++) {
      float a = ang((float)i / g.ring_seg);
      lv_coord_t x0, y0, x1, y1;
      ptOn(cx, cy, a, rr + rw / 2 + 1, x0, y0); ptOn(cx, cy, a, rr - rw / 2 - 1, x1, y1);
      line(ctx, x0, y0, x1, y1, 5, gcol, false);
    }
  }
  // ticks and scale numbers, inside the band (drawn first: a band that
  // reaches over them covers their ends, as on the built-in dials)
  // Marks every `step` display units counted from zero (so 0 is always marked when the scale
  // crosses it): -10..20 step 5 gives -10 -5 0 5 10 15 20. Needs at least two and not too many
  // marks (60 marks, 20 numbers), else the even divisions are used.
  float dmin = toDisp(g, g.vmin), dmax = toDisp(g, g.vmax);
  float ustep = g.t_step > 0.0f ? g.t_step / g.t_minor : 0.0f;
  long kmin = 0, kmax = -1;
  bool stepped = false;
  if (ustep > 0.0f && !GL_GEARFMT(g) && dmax > dmin) {
    kmin = (long)ceilf(dmin / ustep - 1e-4f); kmax = (long)floorf(dmax / ustep + 1e-4f);
    stepped = (kmax - kmin) >= 1 && (kmax - kmin) <= 60 && (kmax - kmin) / (long)g.t_minor <= 20;   // not an unreadable crowd
  }
  if (stepped) {
    float ro = r - w / 2 - g.t_inset;
    auto fr = [&](long k) { return clampf(((float)k * ustep - dmin) / (dmax - dmin), 0, 1); };
    for (long k = kmin; k <= kmax; k++) {
      bool major = (k % (long)g.t_minor) == 0;
      float a = ang(fr(k));
      lv_coord_t x0, y0, x1, y1;
      ptOn(cx, cy, a, ro, x0, y0);
      ptOn(cx, cy, a, ro - (major ? g.t_len : (isfinite(g.t_minlen) ? g.t_minlen : g.t_len * 0.6f)), x1, y1);
      line(ctx, x0, y0, x1, y1, major ? 3 : 2, major ? colOf(g.c_tmaj) : colOf(g.c_tmin));
    }
    if (g.t_numbers) {
      const float st = g.t_step;
      const int dp = fabsf(st - roundf(st)) < 1e-4f ? 0 : (fabsf(st * 10 - roundf(st * 10)) < 1e-3f ? 1 : 2);
      float rn = ro - g.t_len - 6 - g.t_font * 0.75f;
      for (long k = kmin; k <= kmax; k++) {
        if ((k % (long)g.t_minor) != 0) continue;
        float f = fr(k);
        if (fabsf(sw) >= 359.0f && f > 0.9999f && fr(kmin) < 0.0001f) continue;   // full circle: last = first
        float v = (float)k * ustep; if (fabsf(v) < 1e-6f) v = 0;
        char b[14]; snprintf(b, sizeof b, "%.*f", dp, (double)v);
        lv_coord_t x, y; ptOn(cx, cy, ang(f), rn, x, y);
        text(ctx, x, y, b, g.t_font, colOf(g.c_scale));
      }
    }
  } else if (g.t_major) {
    float ro = r - w / 2 - g.t_inset;
    int total = g.t_major * g.t_minor;
    for (int i = 0; i <= total; i++) {
      bool major = (i % g.t_minor) == 0;
      float a = ang((float)i / total);
      lv_coord_t x0, y0, x1, y1;
      ptOn(cx, cy, a, ro, x0, y0);
      ptOn(cx, cy, a, ro - (major ? g.t_len : (isfinite(g.t_minlen) ? g.t_minlen : g.t_len * 0.6f)), x1, y1);
      line(ctx, x0, y0, x1, y1, major ? 3 : 2, major ? colOf(g.c_tmaj) : colOf(g.c_tmin));
    }
    if (g.t_numbers) {
      float step = (toDisp(g, g.vmax) - toDisp(g, g.vmin)) / g.t_major;
      int dp = fabsf(step) >= 1.0f || GL_GEARFMT(g) ? 0 : 1;
      float rn = ro - g.t_len - 6 - g.t_font * 0.75f;
      for (int i = 0; i <= g.t_major; i++) {
        if (fabsf(sw) >= 359.0f && i == g.t_major) break;   // full circle: last = first
        float base = g.vmin + (g.vmax - g.vmin) * i / g.t_major;
        char b[12];
        if (GL_GEARFMT(g)) fmtValue(g, base, b, sizeof b);
        else { snprintf(b, sizeof b, "%.*f", dp, (double)toDisp(g, base)); if (!strcmp(b, "-0")) scopy(b, "0", sizeof b); }
        lv_coord_t x, y; ptOn(cx, cy, ang((float)i / g.t_major), rn, x, y);
        text(ctx, x, y, b, g.t_font, colOf(g.c_scale));
      }
    }
  }
  const bool arc = g.type == GT_ARC;
  float f0 = fracOf(g, originOf(g)), f1 = g.d_valid ? g.d_frac : f0;
  if (f1 < f0) { float t = f0; f0 = f1; f1 = t; }
  float z[4][3]; int nz = (g.zones || fancyBand(g)) ? zonesOf(g, z) : 0;
  lv_color_t trk = colOf(g.c_track);
  // Blocks: the band is painted as one solid piece and the gaps are cut across it as straight lines
  // in the face colour. (LVGL arcs only have whole-degree ends, so gaps made by leaving space
  // between separate arcs came out 1 or 2 degrees wide, unevenly.)
  const bool cut = g.segments > 1 && !g.notch;
  const int nsegG = (g.segments && !g.notch) ? g.segments : 1;                              // blocks, for the glow
  const float gapG = (g.segments && !g.notch) ? ((6.0f / r) * 57.2958f) / fabsf(sw) : 0.0f;  // block gap as a fraction
  int nseg = cut ? 1 : nsegG;
  float gapf = cut ? 0.0f : gapG;
  const bool fancy = fancyBand(g);
  // lit stretches of each block (for the glow and the bright centre)
  const bool litOn = arc && g.d_valid && !g.d_dark && f1 > f0;
  auto litSpans = [&](auto out) {
    for (int i = 0; i < nsegG; i++) {
      float s0 = (float)i / nsegG + gapG / 2, s1 = (float)(i + 1) / nsegG - gapG / 2;
      float p = fmaxf(s0, f0), q = fminf(s1, f1);
      if (q > p) out(p, q, g.zone_fill ? zoneColAt(g, z, nz, (p + q) / 2, colOf(g.c_fill)) : g.d_fill);
    }
  };
  if (g.glow > 0.0f && litOn) {                          // soft halo behind the lit blocks
    static const float grow[2] = {0.9f, 0.4f}, op[2] = {0.09f, 0.17f};
    for (int k = 0; k < 2; k++)
      litSpans([&](float p, float q, lv_color_t c) {
        float x = ang(p), y = ang(q);
        band(ctx, cx, cy, r, w * (1.0f + grow[k]), fminf(x, y), fmaxf(x, y), c, (lv_opa_t)lroundf(255.0f * op[k] * g.glow));
      });
  }
  for (int i = 0; i < nseg; i++) {
    float s0 = (float)i / nseg + gapf / 2, s1 = (float)(i + 1) / nseg - gapf / 2;
    if (fancy) {                                        // shaded unlit / zone-coloured fill
      bandPieces(g, z, nz, s0, s1, arc && g.d_valid, f0, f1,
                 [&](float p, float q, lv_color_t c) { bandF(p, q, c, LV_OPA_COVER); });
      continue;
    }
    bandF(s0, s1, trk, LV_OPA_COVER);
    for (int k = 0; k < nz; k++) {                      // amber / red ranges on the track
      float za = fracOf(g, z[k][0]), zb = fracOf(g, z[k][1]);
      bandF(fmaxf(s0, za), fminf(s1, zb), alertCol(g, (int)z[k][2]), arc ? LV_OPA_40 : LV_OPA_70);
    }
    if (arc && g.d_valid) bandF(fmaxf(s0, f0), fminf(s1, f1), g.d_fill, LV_OPA_COVER);   // the reading
  }
  if (g.rounded) {                                      // round ends, like an LVGL arc
    auto cap = [&](float f, lv_color_t c) {
      lv_coord_t x, y; ptOn(cx, cy, ang(f), r, x, y);
      lv_coord_t h = (lv_coord_t)(w / 2);
      rect(ctx, x - h, y - h, x + h, y + h, c, LV_RADIUS_CIRCLE);
    };
    bool litOk = arc && g.d_valid && f1 > f0;
    auto colAt = [&](float f, bool lit) { return fancy ? bandColAt(g, z, nz, f, lit) : (lit ? g.d_fill : trk); };
    cap(0, colAt(0, litOk && f0 <= 0.0f)); cap(1, colAt(1, litOk && f1 >= 1.0f));   // the track's ends
    if (litOk) { cap(f0, colAt(f0, true)); cap(f1, colAt(f1, true)); }               // the reading's ends
  }
  if (g.core && litOn)                                   // brighter centre of the lit part, like a lit lamp (gaps are cut through it below)
    bandPieces(g, z, nz, 0.0f, 1.0f, true, f0, f1, [&](float p, float q, lv_color_t c) {
      float m = (p + q) / 2;
      if (m < f0 || m > f1) return;
      float x = ang(p), y = ang(q);
      band(ctx, cx, cy, r, w * 0.4f, fminf(x, y), fmaxf(x, y), hot(c), LV_OPA_COVER);
    });
  if (cut) {                                             // the gaps between blocks
    GlColor dc = {GR_DIAL, 0}; lv_color_t gcol = colOf(dc);
    for (int i = 1; i < g.segments; i++) {
      float a = ang((float)i / g.segments);
      lv_coord_t x0, y0, x1, y1;
      ptOn(cx, cy, a, r + w / 2 + 1, x0, y0); ptOn(cx, cy, a, r - w / 2 - 1, x1, y1);
      line(ctx, x0, y0, x1, y1, 5, gcol, false);
    }
  }
  if (g.notch && g.segments > 1) {                     // block marks cut across the solid band
    lv_color_t gc = colOf(g.c_gap);
    for (int i = 1; i < g.segments; i++) {
      float a = ang((float)i / g.segments), ro = r + w / 2;
      lv_coord_t x0, y0, x1, y1;
      ptOn(cx, cy, a, ro + g.gap_out, x0, y0); ptOn(cx, cy, a, ro - g.gap_in, x1, y1);
      line(ctx, x0, y0, x1, y1, g.gap_w, gc);
    }
  }
  for (int i = 0; i < g.nmarks; i++) {                 // reference marks (zero, stoich...)
    const GlMark& m = g.marks[i];
    float a = ang(fracOf(g, m.value)), ro = r + w / 2;
    lv_coord_t x0, y0, x1, y1;
    ptOn(cx, cy, a, ro + m.out, x0, y0); ptOn(cx, cy, a, ro - m.in, x1, y1);
    line(ctx, x0, y0, x1, y1, m.w, colOf(m.c));
  }
  if (g.d_peak) {                                       // peak marker across the band
    float a = ang(g.d_peak_frac);
    lv_coord_t x0, y0, x1, y1;
    ptOn(cx, cy, a, r + w / 2 + 3, x0, y0); ptOn(cx, cy, a, r - w / 2 - 3, x1, y1);
    line(ctx, x0, y0, x1, y1, 4, s_pal.text);
  }
}

static void drawNeedle(lv_draw_ctx_t* ctx, const GlGauge& g, float cx, float cy) {
  if (g.d_valid) {
    float a = g.a_start + g.a_sweep * g.d_frac;
    lv_coord_t x0, y0, x1, y1;
    ptOn(cx, cy, a + 180.0f, g.n_tail, x0, y0);
    ptOn(cx, cy, a, g.n_len, x1, y1);
    line(ctx, x0, y0, x1, y1, g.n_w, g.d_fill.full == colOf(g.c_fill).full ? colOf(g.c_needle) : g.d_fill);
  }
  if (g.n_hub > 0) {
    lv_coord_t h = g.n_hub / 2;
    rect(ctx, (lv_coord_t)(cx - h), (lv_coord_t)(cy - h), (lv_coord_t)(cx + h), (lv_coord_t)(cy + h),
         colOf(g.c_needle), LV_RADIUS_CIRCLE);
  }
}

static void drawBar(lv_draw_ctx_t* ctx, const GlGauge& g, float cx, float cy) {
  lv_coord_t x0 = (lv_coord_t)(cx - g.w / 2), y0 = (lv_coord_t)(cy - g.h / 2);
  lv_coord_t x1 = x0 + g.w - 1, y1 = y0 + g.h - 1;
  int len = g.vertical ? g.h : g.w;
  int nseg = g.segments ? g.segments : 1;
  int gap = g.segments ? 4 : 0;
  float f0 = fracOf(g, originOf(g)), f1 = g.d_valid ? g.d_frac : f0;
  if (f1 < f0) { float t = f0; f0 = f1; f1 = t; }
  lv_coord_t rad = g.segments ? 3 : (lv_coord_t)((g.vertical ? g.w : g.h) / 4);
  float z[4][3]; int nz = (g.zones || fancyBand(g)) ? zonesOf(g, z) : 0;
  // lit stretch of block i in pixels along the bar, -1/-2 = none
  auto litRange = [&](int i, int& lo, int& hi) {
    int p0 = len * i / nseg + gap / 2, p1 = len * (i + 1) / nseg - gap / 2 - 1;
    int fa = (int)lroundf(len * f0), fb = (int)lroundf(len * f1) - 1;
    lo = fa > p0 ? fa : p0; hi = fb < p1 ? fb : p1;
    return g.d_valid && !g.d_dark && hi >= lo;
  };
  auto litRect = [&](int lo, int hi, int e, lv_color_t c, lv_opa_t opa, lv_coord_t rd) {
    if (g.vertical) rect(ctx, (lv_coord_t)(x0 - e), (lv_coord_t)(y1 - hi - e), (lv_coord_t)(x1 + e), (lv_coord_t)(y1 - lo + e), c, rd, opa);
    else            rect(ctx, (lv_coord_t)(x0 + lo - e), (lv_coord_t)(y0 - e), (lv_coord_t)(x0 + hi + e), (lv_coord_t)(y1 + e), c, rd, opa);
  };
  auto litCol = [&](int lo, int hi) {
    return g.zone_fill ? zoneColAt(g, z, nz, ((lo + hi) / 2.0f) / len, colOf(g.c_fill)) : g.d_fill;
  };
  if (g.glow > 0.0f) {                                   // soft halo behind the lit blocks
    const int thick = g.vertical ? g.w : g.h;
    static const float grow[2] = {0.6f, 0.28f}, op[2] = {0.09f, 0.17f};
    for (int k = 0; k < 2; k++)
      for (int i = 0; i < nseg; i++) {
        int lo, hi; if (!litRange(i, lo, hi)) continue;
        litRect(lo, hi, (int)lroundf(thick * grow[k]), litCol(lo, hi), (lv_opa_t)lroundf(255.0f * op[k] * g.glow), LV_RADIUS_CIRCLE);
      }
  }
  for (int i = 0; i < nseg; i++) {
    int p0 = len * i / nseg + gap / 2, p1 = len * (i + 1) / nseg - gap / 2 - 1;
    auto seg = [&](int q0, int q1, lv_color_t c, lv_opa_t opa) {
      if (q1 < q0) return;
      if (g.vertical) rect(ctx, x0, (lv_coord_t)(y1 - q1), x1, (lv_coord_t)(y1 - q0), c, rad, opa);
      else            rect(ctx, (lv_coord_t)(x0 + q0), y0, (lv_coord_t)(x0 + q1), y1, c, rad, opa);
    };
    if (fancyBand(g)) {                                 // shaded unlit / zone-coloured fill
      bandPieces(g, z, nz, (float)p0 / len, (float)(p1 + 1) / len, g.d_valid, f0, f1,
                 [&](float a, float b, lv_color_t c) { seg((int)lroundf(a * len), (int)lroundf(b * len) - 1, c, LV_OPA_COVER); });
      continue;
    }
    seg(p0, p1, colOf(g.c_track), LV_OPA_COVER);
    for (int k = 0; k < nz; k++) {
      int za = (int)(len * fracOf(g, z[k][0])), zb = (int)(len * fracOf(g, z[k][1])) - 1;
      seg(za > p0 ? za : p0, zb < p1 ? zb : p1, alertCol(g, (int)z[k][2]), LV_OPA_40);
    }
    if (g.d_valid) {
      int fa = (int)lroundf(len * f0), fb = (int)lroundf(len * f1) - 1;
      seg(fa > p0 ? fa : p0, fb < p1 ? fb : p1, g.d_fill, LV_OPA_COVER);
    }
  }
  if (g.core) {                                          // brighter centre strip of the lit blocks
    const int thick = g.vertical ? g.w : g.h, inset = thick * 3 / 10;
    for (int i = 0; i < nseg; i++) {
      int lo, hi; if (!litRange(i, lo, hi)) continue;
      int a = lo + inset / 2, b = hi - inset / 2; if (b < a) continue;
      if (g.vertical) rect(ctx, (lv_coord_t)(x0 + inset), (lv_coord_t)(y1 - b), (lv_coord_t)(x1 - inset), (lv_coord_t)(y1 - a), hot(litCol(lo, hi)), 3);
      else            rect(ctx, (lv_coord_t)(x0 + a), (lv_coord_t)(y0 + inset), (lv_coord_t)(x0 + b), (lv_coord_t)(y1 - inset), hot(litCol(lo, hi)), 3);
    }
  }
  if (g.d_peak) {
    int p = (int)lroundf(len * g.d_peak_frac);
    if (g.vertical) line(ctx, x0 - 4, (lv_coord_t)(y1 - p), x1 + 4, (lv_coord_t)(y1 - p), 3, s_pal.text);
    else            line(ctx, (lv_coord_t)(x0 + p), y0 - 4, (lv_coord_t)(x0 + p), y1 + 4, 3, s_pal.text);
  }
}

// ---- clock (the built-in clock view's drawing: Gauge.cpp buildClock) ----
static bool clockAnalog(const GlGauge& g) { return g.clk_style == 1 || (g.clk_style == 0 && s_clk_analog); }
static void drawClock(lv_draw_ctx_t* ctx, const GlGauge& g, float cx, float cy) {
  if (clockAnalog(g)) {
    const float r = g.r, ro = r - g.t_inset;
    int total = 12 * (g.t_minor ? g.t_minor : 1);
    for (int i = 0; i < total; i++) {
      bool major = (i % (g.t_minor ? g.t_minor : 1)) == 0;
      float a = i * 360.0f / total;
      float len = major ? g.t_len : (isfinite(g.t_minlen) ? g.t_minlen : g.t_len * 0.4f);
      lv_coord_t x0, y0, x1, y1;
      ptOn(cx, cy, a, ro, x0, y0); ptOn(cx, cy, a, ro - len, x1, y1);
      line(ctx, x0, y0, x1, y1, major ? 5 : 2, major ? colOf(g.c_tmaj) : colOf(g.c_tmin), false);
    }
    if (g.t_numbers) {
      float rn = ro - g.t_len - 8 - g.t_font;
      for (int i = 0; i < 12; i++) {
        char b[4]; snprintf(b, sizeof b, "%d", i == 0 ? 12 : i);
        lv_coord_t x, y; ptOn(cx, cy, i * 30.0f, rn, x, y);
        text(ctx, x, y, b, g.t_font, colOf(g.c_scale));
      }
    }
    if (s_clk.valid) {
      float ah = (s_clk.hour % 12) * 30.0f + s_clk.minute * 0.5f, am = s_clk.minute * 6.0f, as = s_clk.second * 6.0f;
      lv_coord_t x, y, c0 = (lv_coord_t)lroundf(cx), c1 = (lv_coord_t)lroundf(cy);
      ptOn(cx, cy, ah, r * g.hand_h, x, y); line(ctx, c0, c1, x, y, 9, colOf(g.c_text));
      ptOn(cx, cy, am, r * g.hand_m, x, y); line(ctx, c0, c1, x, y, 6, colOf(g.c_text));
      if (g.show_sec) { ptOn(cx, cy, as, r * g.hand_s, x, y); line(ctx, c0, c1, x, y, 2, colOf(g.c_needle)); }
    }
    if (g.n_hub > 0) {
      lv_coord_t h = g.n_hub / 2;
      rect(ctx, (lv_coord_t)(cx - h), (lv_coord_t)(cy - h), (lv_coord_t)(cx + h), (lv_coord_t)(cy + h), colOf(g.c_needle), LV_RADIUS_CIRCLE);
    }
    return;
  }
  // digital: four seven-segment digits and a colon of two squares
  static const uint8_t SEG7[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
  const float k = g.h / 132.0f;
  const int H = g.h, W = (int)lroundf(72 * k), T = g.seg_t;
  const int ccx = (int)lroundf(cx + g.number.dx), top = (int)lroundf(cy + g.number.dy - H / 2.0f);
  const int left[4] = {(int)lroundf(-177 * k), (int)lroundf(-97 * k), (int)lroundf(25 * k), (int)lroundf(105 * k)};
  int dig[4] = {s_clk.hour / 10, s_clk.hour % 10, s_clk.minute / 10, s_clk.minute % 10};
  lv_color_t col = colOf(g.c_text);
  for (int d = 0; d < 4; d++) {
    uint8_t m = s_clk.valid ? SEG7[dig[d] % 10] : 0x40;       // no time: "--:--"
    int x = ccx + left[d], y = top;
    const int sx[7] = {x + T / 2, x + W - T, x + W - T, x + T / 2, x, x, x + T / 2};
    const int sy[7] = {y, y + T / 2, y + H / 2 + T / 2, y + H - T, y + H / 2 + T / 2, y + T / 2, y + H / 2 - T / 2};
    const int sw[7] = {W - T, T, T, W - T, T, T, W - T};
    const int sh[7] = {T, H / 2 - T, H / 2 - T, T, H / 2 - T, H / 2 - T, T};
    for (int s = 0; s < 7; s++)
      if (m & (1 << s)) rect(ctx, (lv_coord_t)sx[s], (lv_coord_t)sy[s], (lv_coord_t)(sx[s] + sw[s] - 1), (lv_coord_t)(sy[s] + sh[s] - 1), col, 3);
  }
  for (int i = 0; i < 2; i++) {
    int y = top + (i ? H * 2 / 3 : H / 3) - T / 2, x = ccx - T / 2;
    rect(ctx, (lv_coord_t)x, (lv_coord_t)y, (lv_coord_t)(x + T - 1), (lv_coord_t)(y + T - 1), col, 3);
  }
}

static void drawImage(lv_draw_ctx_t* ctx, const GlGauge& g, float cx, float cy) {
  if (g.img < 0 || g.img >= s_nimgs || !s_imgs[g.img].img) return;
  const lv_img_dsc_t* im = s_imgs[g.img].img;
  lv_draw_img_dsc_t d; lv_draw_img_dsc_init(&d);
  if (g.has_tint) { d.recolor = colOf(g.c_tint); d.recolor_opa = LV_OPA_COVER; }
  lv_coord_t w = (lv_coord_t)im->header.w, h = (lv_coord_t)im->header.h;
  lv_coord_t x0 = (lv_coord_t)(lroundf(cx) - w / 2), y0 = (lv_coord_t)(lroundf(cy) - h / 2);
  lv_area_t a = {x0, y0, (lv_coord_t)(x0 + w - 1), (lv_coord_t)(y0 + h - 1)};
  lv_draw_img(ctx, &d, &a, im);
}

static void drawGauge(lv_draw_ctx_t* ctx, const GlGauge& g, lv_coord_t ox, lv_coord_t oy) {
  float cx = ox + g.x, cy = oy + g.y;
  switch (g.type) {
    case GT_ARC:    drawDial(ctx, g, cx, cy); break;
    case GT_NEEDLE: drawDial(ctx, g, cx, cy); break;
    case GT_BAR:    drawBar(ctx, g, cx, cy); break;
    case GT_LIGHT: {
      lv_coord_t h = g.size / 2;
      rect(ctx, (lv_coord_t)(cx - h), (lv_coord_t)(cy - h), (lv_coord_t)(cx + h), (lv_coord_t)(cy + h),
           g.d_lit ? colOf(g.c_on) : colOf(g.c_off), g.square ? (lv_coord_t)(g.size / 6) : LV_RADIUS_CIRCLE);
      break;
    }
    case GT_LINE:
      line(ctx, (lv_coord_t)cx, (lv_coord_t)cy, (lv_coord_t)(ox + g.x2), (lv_coord_t)(oy + g.y2), g.width, colOf(g.c_label), false);
      return;
    case GT_IMAGE: drawImage(ctx, g, cx, cy); return;
    case GT_CLOCK: drawClock(ctx, g, cx, cy); break;
    default: break;
  }
  if (g.number.show && g.type != GT_LIGHT && g.type != GT_TEXT && g.type != GT_CLOCK) {
    if (g.n7) drawSeg7(ctx, g, cx, cy);
    else if (g.n_fixed && plainNumber(g.d_txt)) drawNumberFixed(ctx, g, (lv_coord_t)(cx + g.number.dx), (lv_coord_t)(cy + g.number.dy));
    else text(ctx, (lv_coord_t)(cx + g.number.dx), (lv_coord_t)(cy + g.number.dy), g.d_txt, g.number.font, g.d_num);
  }
  if (g.unit.show && g.unit_txt[0])
    text(ctx, (lv_coord_t)(cx + g.unit.dx), (lv_coord_t)(cy + g.unit.dy), g.unit_txt, g.unit.font, colOf(g.c_unit));
  if (g.label.show && g.label_txt[0])
    text(ctx, (lv_coord_t)(cx + g.label.dx), (lv_coord_t)(cy + g.label.dy), g.label_txt, g.label.font, colOf(g.c_label));
  if (g.note.show && g.notes[g.d_note][0])
    text(ctx, (lv_coord_t)(cx + g.note.dx), (lv_coord_t)(cy + g.note.dy), g.notes[g.d_note], g.note.font, s_pal.dim);
  if (g.pk.show && g.d_pk)
    text(ctx, (lv_coord_t)(cx + g.pk.dx), (lv_coord_t)(cy + g.pk.dy), g.d_pktxt, g.pk.font, s_pal.dim);
}

static void draw_cb(lv_event_t* e) {
  if (s_built < 0) return;
  lv_obj_t* o = lv_event_get_target(e);
  lv_draw_ctx_t* ctx = lv_event_get_draw_ctx(e);
  const GlView& v = s_views[s_built];
  for (int i = 0; i < v.n; i++) drawGauge(ctx, v.g[i], o->coords.x1, o->coords.y1);
  // needles last, so a needle passes over lights, numbers and bars rather than under them
  for (int i = 0; i < v.n; i++)
    if (v.g[i].type == GT_NEEDLE) drawNeedle(ctx, v.g[i], (float)(o->coords.x1 + v.g[i].x), (float)(o->coords.y1 + v.g[i].y));
}

// ------------------------------------------------------------ build / run ---
lv_obj_t* GL_Build(int vi, lv_obj_t* parent, uint32_t now) {
  GL_Destroy();
  if (vi < 0 || vi >= s_nviews || !parent || s_views[vi].builtin[0]) return nullptr;
  GlView& v = s_views[vi];
  for (int i = 0; i < v.n; i++) {
    GlGauge& g = v.g[i];
    g.dvalid = false; g.last_t = now; g.peak_valid = false; g.crit_since = 0;
    g.d_valid = false; g.d_peak = false; g.d_lit = false;
    g.d_fill = colOf(g.c_fill); g.d_num = g.has_nodata ? colOf(g.c_nodata) : colOf(g.c_text);
    scopy(g.d_txt, "--", sizeof g.d_txt);
    g.d_note = NS_NONE; g.d_pk = false;
    refreshUnits(g);
    g.dirty = true;
  }
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, 480, 480);
  lv_obj_set_pos(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN_END, nullptr);
  s_obj = o; s_built = vi; s_built_t = now;
  return o;
}

void GL_Destroy() {
  if (s_obj) { lv_obj_del(s_obj); s_obj = nullptr; }
  s_built = -1;
}
int GL_Built() { return s_built; }

void GL_Update(const float* kv, uint32_t now) {
  if (s_built < 0 || !s_obj) return;
  GlView& v = s_views[s_built];
  bool changed = false;
  bool sweeping = (now - s_built_t) < 1400;
  for (int i = 0; i < v.n; i++) {
    GlGauge& g = v.g[i];
    if (g.type == GT_CLOCK) {                        // redraw when the time shown changes
      bool an = clockAnalog(g);
      static const char* DOW[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      // the date / time text: {date} "Sun 25/10/2026", {time} "14:05", {day} "Sun"
      char out[sizeof g.label_txt]; size_t n = 0; out[0] = 0;
      if (g.label.show && (!an || g.label_analog)) {
        for (const char* p = g.label.text; *p && n + 1 < sizeof out; ) {
          char rep[24]; rep[0] = 0; int skip = 0;
          if (!strncmp(p, "{date}", 6)) { skip = 6; if (s_clk.valid) snprintf(rep, sizeof rep, "%s %02d/%02d/%04d", DOW[s_clk.dow % 7], s_clk.day, s_clk.month, s_clk.year); else scopy(rep, "--", sizeof rep); }
          else if (!strncmp(p, "{time}", 6)) { skip = 6; if (s_clk.valid) snprintf(rep, sizeof rep, "%02d:%02d", s_clk.hour, s_clk.minute); else scopy(rep, "--:--", sizeof rep); }
          else if (!strncmp(p, "{day}", 5)) { skip = 5; scopy(rep, s_clk.valid ? DOW[s_clk.dow % 7] : "--", sizeof rep); }
          if (skip) { for (const char* q = rep; *q && n + 1 < sizeof out; q++) out[n++] = *q; p += skip; }
          else out[n++] = *p++;
        }
        out[n] = 0;
      }
      scopy(g.label_txt, out, sizeof g.label_txt);
      uint32_t sg = 23u + (an ? 1u : 2u);
      sg = sg * 31u + (s_clk.valid ? 1u : 0u);
      sg = sg * 31u + (uint32_t)(s_clk.hour * 60 + s_clk.minute);
      if (an && g.show_sec) sg = sg * 31u + (uint32_t)s_clk.second;
      for (const char* p = g.label_txt; *p; p++) sg = sg * 31u + (uint8_t)*p;
      if (g.dirty || sg != g.sig) { g.sig = sg; g.dirty = false; changed = true; }
      continue;
    }
    if (g.type == GT_TEXT || g.type == GT_LINE || g.type == GT_IMAGE) continue;
    float val = kv[g.input];
    if (!isfinite(val)) val = NAN;
    // smoothing (time based, so it doesn't depend on the update rate)
    uint32_t dt = now - g.last_t; g.last_t = now;
    if (isfinite(val)) {
      if (!g.dvalid || g.smooth <= 0.0f) g.disp = val;
      else g.disp += (val - g.disp) * (1.0f - powf(g.smooth, (float)dt / 100.0f));
      g.dvalid = true;
    } else g.dvalid = false;
    // alerts
    uint8_t ns = NS_NONE;
    int lvl = judge(g, kv, &ns);
    g.d_note = ns;
    if (lvl == 2) { if (!g.crit_since) g.crit_since = now | 1; } else g.crit_since = 0;
    int flash = 0;                                   // 0 off, 1 bright phase, 2 dark phase
    if (g.flash && lvl == 2 && now - g.crit_since >= 500) flash = 1 + (int)(((now - g.crit_since) / 200) & 1);
    lv_color_t base = colOf(g.c_fill);
    const int ak = ns == NS_LC ? 0 : ns == NS_LW ? 1 : ns == NS_HW ? 2 : ns == NS_HC ? 3 : -1;   // which alert level is active
    const lv_color_t acol = ak >= 0 ? alertCol(g, ak) : base;
    g.d_fill = flash == 2 ? s_pal.track : ak >= 0 ? acol : base;
    g.d_dark = flash == 2;
    g.d_num  = !g.num_alert ? colOf(g.c_text)
             : flash == 2 ? s_pal.dim : ak >= 0 ? acol : colOf(g.c_text);
    if (!isfinite(val) && g.has_nodata) g.d_num = colOf(g.c_nodata);   // "--" in its own colour
    // position (with the opening sweep)
    if (g.sweep_anim && sweeping && (g.type == GT_ARC || g.type == GT_NEEDLE || g.type == GT_BAR)) {
      // up to full scale in the first half, then settle onto the reading
      float t = (now - s_built_t) / 1400.0f;
      float f = t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f;
      f = f * f * (3 - 2 * f);                       // ease in/out
      float target = t < 0.5f ? 0.0f : (g.dvalid ? fracOf(g, g.disp) : 0.0f);
      g.d_valid = true;
      g.d_frac = target + (1.0f - target) * f;
    } else {
      g.d_valid = g.dvalid;
      g.d_frac = g.dvalid ? fracOf(g, g.disp) : 0;
    }
    // peak hold
    if (g.peak && g.dvalid) {
      if (!g.peak_valid || g.disp >= g.peak_v) { g.peak_v = g.disp; g.peak_t = now; g.peak_valid = true; }
      else if (now - g.peak_t > (uint32_t)(g.peak_s * 1000)) { g.peak_v = g.disp; g.peak_t = now; }
    } else if (!g.dvalid) g.peak_valid = false;
    g.d_peak = g.peak && g.peak_valid && !(g.sweep_anim && sweeping) && fracOf(g, g.peak_v) > g.d_frac + 0.004f;
    g.d_peak_frac = g.peak_valid ? fracOf(g, g.peak_v) : 0;
    // peak label: the highest reading since the screen started (kept across view changes)
    if (g.pk.show && isfinite(val)) {
      if (!g.pk_have) { g.pk_raw = isfinite(g.pk_start) ? fmaxf(g.pk_start, val) : val; g.pk_have = true; }
      else if (val > g.pk_raw) g.pk_raw = val;
    }
    g.d_pk = g.pk.show && g.pk_have && isfinite(val);
    if (g.d_pk) {
      char b[16]; fmtValue(g, g.pk_raw, b, sizeof b);
      snprintf(g.d_pktxt, sizeof g.d_pktxt, "%s%s", g.pk.text, b);
    }
    // text and light
    fmtValue(g, val, g.d_txt, sizeof g.d_txt);
    bool lit = litOf(g, val);
    if (lit && g.blink && ((now / 400) & 1)) lit = false;
    g.d_lit = lit;
    // anything visible changed?
    uint32_t s = 17u;                               // (unsigned: wrap-around is fine)
    s = s * 31u + (g.d_valid ? (uint32_t)lroundf(g.d_frac * 720) : 0xFFFFu);
    s = s * 31u + (g.d_peak ? (uint32_t)lroundf(g.d_peak_frac * 720) : 0xFFFFu);
    s = s * 31u + (uint32_t)g.d_fill.full;
    s = s * 31u + (uint32_t)g.d_num.full;
    s = s * 31u + (g.d_lit ? 1u : 0u);
    s = s * 31u + g.d_note;
    for (const char* p = g.d_txt; *p; p++) s = s * 31u + (uint8_t)*p;
    if (g.d_pk) for (const char* p = g.d_pktxt; *p; p++) s = s * 31u + (uint8_t)*p;
    if (g.dirty || s != g.sig) { g.sig = s; g.dirty = false; changed = true; }
  }
  if (changed) lv_obj_invalidate(s_obj);
}
