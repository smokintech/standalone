// ---------------------------------------------------------------------------
// oil_ui.cpp - the screens of the 2.1" round display: oil pressure, boost and
// AFR, each as a full-screen dial or as a split screen with two gauges (top and
// bottom, like the oil pressure / oil temperature view on the 370Z gauge screens).
//
// Look and geometry follow Gauge.cpp: 270-degree sweep with the gap at the
// bottom, segmented band, big number in the middle, amber / red past the limits.
// A split screen uses two 160-degree arcs, one over the top and one under the
// bottom, with a divider across the middle.
//
// Controls (touch):
//   swipe LEFT / RIGHT  next / previous screen (the screens are chosen on the phone page)
//   tap                 silence a sounding alert, otherwise switch psi <-> bar (saved)
//   swipe UP            switch the gauge's Wi-Fi on  (the name and address appear on the dial; the password never does)
//   swipe DOWN          switch the gauge's Wi-Fi off
//   hold 5 s, still     offer to reset the Wi-Fi name/password (and nothing else)
// Backlight is set on the phone page.
//
// Besides these dials, a screen can be a DESIGNED view: a layout made in the layout
// editor and uploaded on the phone page. The layout engine (Gauge_Layout.cpp) draws it
// from the same readings; the alarm below works the same on those screens.
//
// The low oil pressure alarm works on every screen: the ring flashes, the buzzer
// sounds, and the gauge jumps to a screen showing oil pressure (or, if none of
// the chosen screens shows it, a banner appears).
//
// LVGL notes carried over from the working gauge firmware:
//   * LV_SPRINTF_USE_FLOAT is 0, so no %f anywhere - numbers are formatted from
//     integers.
//   * Objects are only touched when their value really changes. Every change
//     repaints part of an RGB panel that is already bandwidth-limited.
//   * Changing screen deletes the old page's objects and builds the new page's:
//     it happens only on a swipe or a settings change, never while running.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <math.h>
#include "board_lvgl.h"         // lvgl.h
#include "board_panel.h"        // backlight
#include "board_expander.h"     // buzzer
#include "oil_config.h"
#include "oil_settings.h"
#include "oil_demo.h"
#include "oil_math.h"
#include "oil_sensor.h"
#include "oil_web.h"
#include "oil_layout.h"
#include "Gauge_Layout.h"       // designed views from the layout editor
#include "oil_ui.h"

// ---- palette (same values as the gauge screens' dark theme) ----------------
#define COL_WARN     lv_color_hex(0xf5a623)
#define COL_CRIT     lv_color_hex(0xe2213b)
#define COL_ACCENT   lv_color_hex(0x1db954)
#define COL_RICH     lv_color_hex(0x4aa8ff)
#define COL_DIAL     lv_color_hex(0x0d0d0f)
#define COL_RING     lv_color_hex(0x1a1a1e)
#define COL_TICK     lv_color_hex(0x3a3a42)
#define COL_TICKMAJ  lv_color_hex(0x6a6a76)
#define COL_DIM      lv_color_hex(0x8a8a94)
#define COL_INFO     lv_color_hex(0xc4c4cc)

// ---- geometry (angle 0 = top, 90 = right, 180 = bottom, 270 = left) --------
static const float CX = 240, CY = 240, R = 200;
#define ARC_W      30
#define SEG_GAP_W   6

struct Geo { float start, sweep; int nseg; };
static const Geo GEO_FULL = { 225, 270, 20 };      // one gauge: gap at the bottom
static const Geo GEO_TOP  = { 280, 160, 12 };      // split: left -> over the top -> right
static const Geo GEO_BOT  = { 100, 160, 12 };      // split: right -> under the bottom -> left

static inline float d2r(float d) { return (d - 90.0f) * (float)M_PI / 180.0f; }
static inline void ptOn(float a, float r, float& x, float& y) {
  x = CX + r * cosf(d2r(a)); y = CY + r * sinf(d2r(a));
}

// ---- state -----------------------------------------------------------------
static lv_obj_t *scr, *face, *pg = nullptr, *sensor_lbl, *toast_lbl, *wifi_lbl, *banner_lbl;
static lv_obj_t *dots[OG_MAX_PAGES];
static lv_obj_t *confirm = nullptr;        // "reset Wi-Fi settings?" prompt, only while open
static uint32_t toast_hide_at = 0, confirm_until = 0;

static bool     shown_bar = false;         // unit the current page was built for
static bool     rebuild_page = false;
static int8_t   page_req = 0;              // +1 next / -1 previous screen (set by a swipe, done in Update)
static int8_t   wifi_req = 0;              // +1 switch on, -1 switch off (set by a swipe)

// "Run demo" (oil_demo.h): invented readings on every screen, ends by itself.
static volatile bool    demo_on = false;
static volatile int8_t  demo_req = 0;      // +1 start / -1 stop, from the phone page (done in Update)
static uint32_t demo_t0 = 0;               // when it started
static bool     demo_saved_armed = false;  // the REAL engine-has-run latch, put back when the demo ends
static lv_obj_t* demo_lbl = nullptr;       // DEMO tag, on the top layer so it covers every screen
static int      demo_lbl_s = -1;

static bool     armed = false;             // engine has demonstrably run
static uint32_t crit_since = 0;            // when oil pressure first fell below crit
static bool     alert = false;             // debounced critical oil alert
static bool     acked = false;             // alert silenced by a tap
static bool     busy = false;              // a firmware install owns the banner
static int8_t   sh_banner = -1;            // 1 = low-oil banner shown, 0 = hidden, -1 = unknown

static inline bool useBar() { return g_set.units_bar != 0; }

// ---- the screens in the rotation ---------------------------------------------
static uint8_t rot[OG_MAX_PAGES];          // screen codes in use, in order
static int     rot_n = 0, cur = 0;

// A designed screen is only usable while its view exists in the loaded layout.
static bool pageUsable(uint8_t code) { return !og_page_is_custom(code) || OilLayout_ViewUsable(og_page_custom_view(code)); }

static void buildRotation() {
  const uint8_t was = (rot_n > 0) ? rot[cur] : 0;
  rot_n = 0;
  for (int i = 0; i < OG_MAX_PAGES; i++) if (g_set.pages[i] && pageUsable(g_set.pages[i])) rot[rot_n++] = g_set.pages[i];
  if (rot_n == 0) rot[rot_n++] = og_page_make(OG_PG_OIL, 0);          // settings validation prevents this
  cur = 0;
  for (int i = 0; i < rot_n; i++) if (rot[i] == was) { cur = i; break; }
}

// Does this screen show gauge `gauge` (OG_PG_OIL / BOOST / AFR)? For a designed view: does it use that reading?
static bool pageShows(uint8_t code, uint8_t gauge) {
  if (og_page_is_custom(code)) {
    const int v = og_page_custom_view(code);
    if (gauge == OG_PG_OIL)   return GL_ViewUsesInput(v, GI_oil_psi) || GL_ViewUsesInput(v, GI_oil_status);
    if (gauge == OG_PG_BOOST) return GL_ViewUsesInput(v, GI_boost_psi);
    return GL_ViewUsesInput(v, GI_afr);
  }
  return og_page_top(code) == gauge || og_page_bot(code) == gauge;
}

// ---- designed views ----------------------------------------------------------
static int   custom_view = -1;             // layout view on screen now, -1 = one of the built-in dials
static float kv[GI_COUNT];                 // the readings the layout engine is given (NaN = no reading)

// ---- tick bookkeeping: lv_line does not free its point arrays ---------------
#define MAX_TICK_PTS 128
static lv_point_t* tick_pts[MAX_TICK_PTS];
static int         tick_n = 0;

static void addTick(lv_obj_t* p, float a, float rout, float rin, lv_color_t col, int w) {
  float x0, y0, x1, y1; ptOn(a, rout, x0, y0); ptOn(a, rin, x1, y1);
  lv_point_t* pts = (lv_point_t*)lv_mem_alloc(sizeof(lv_point_t) * 2);
  if (!pts) return;
  pts[0].x = (lv_coord_t)x0; pts[0].y = (lv_coord_t)y0;
  pts[1].x = (lv_coord_t)x1; pts[1].y = (lv_coord_t)y1;
  if (tick_n < MAX_TICK_PTS) tick_pts[tick_n++] = pts;
  else { lv_mem_free(pts); return; }                       // never leak: just skip the decoration
  lv_obj_t* ln = lv_line_create(p);
  lv_line_set_points(ln, pts, 2);
  lv_obj_set_style_line_color(ln, col, 0);
  lv_obj_set_style_line_width(ln, w, 0);
  lv_obj_set_style_line_rounded(ln, true, 0);
}

static void addNum(lv_obj_t* p, float a, int val) {
  float x, y; ptOn(a, R - 66, x, y);
  lv_obj_t* l = lv_label_create(p);
  char t[8]; snprintf(t, sizeof t, "%d", val);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_color(l, COL_DIM, 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
  lv_obj_update_layout(l);
  lv_obj_set_pos(l, (lv_coord_t)(x - lv_obj_get_width(l) / 2),
                    (lv_coord_t)(y - lv_obj_get_height(l) / 2));
}

static void freeTicks() {
  for (int i = 0; i < tick_n; i++) lv_mem_free(tick_pts[i]);
  tick_n = 0;
}

// ---- one gauge on a page -------------------------------------------------------
struct DScale { float lo, hi; int majors, minors; };      // in the displayed unit; minors = divisions per major
struct Fmt   { int32_t step100; int decimals; };         // display step in 1/100 of the unit, digits after the point

static DScale scaleFor(OgChan ch, bool bar) {
  switch (ch) {
    case OG_CH_OIL:   return bar ? DScale{ 0, OG_DIAL_MAX_BAR, 4, 2 } : DScale{ 0, OG_DIAL_MAX_PSI, 6, 2 };
    case OG_CH_BOOST: return bar ? DScale{ OG_BOOST_DIAL_MIN_BAR, OG_BOOST_DIAL_MAX_BAR, 3, 2 }
                                 : DScale{ OG_BOOST_DIAL_MIN_PSI, OG_BOOST_DIAL_MAX_PSI, 3, 3 };
    default:          return DScale{ OG_AFR_DIAL_MIN, OG_AFR_DIAL_MAX, 5, 2 };
  }
}
static Fmt fmtFor(OgChan ch, bool bar) {
  switch (ch) {
    case OG_CH_OIL:   return bar ? Fmt{ 10, 1 } : Fmt{ 100, 0 };
    case OG_CH_BOOST: return bar ? Fmt{ 1, 2 }  : Fmt{ 10, 1 };
    default:          return Fmt{ 10, 1 };
  }
}
static const char* capFor(OgChan ch, bool bar) {
  switch (ch) {
    case OG_CH_OIL:   return bar ? "OIL  BAR" : "OIL  PSI";
    case OG_CH_BOOST: return bar ? "BOOST  BAR" : "BOOST  PSI";
    default:          return "AFR";
  }
}

struct Dial {
  OgChan   ch;
  Geo      geo;
  lv_obj_t *arc, *num, *cap, *stat;      // stat is null on a split screen (the caption shows the status)
  int32_t  sh_val;                       // last displayed value (in display steps), change guard
  uint8_t  sh_level;
  int16_t  sh_status;
  uint16_t sh_angle;
};
static Dial dials[2];
static int  ndials = 0;
static bool split = false;

static lv_obj_t* makeArc(lv_obj_t* parent, const Geo& g) {
  lv_obj_t* a = lv_arc_create(parent);
  lv_obj_set_size(a, (lv_coord_t)(R * 2), (lv_coord_t)(R * 2));
  lv_obj_center(a);
  lv_arc_set_rotation(a, (uint16_t)(((int)(g.start - 90) + 360) % 360));
  lv_arc_set_bg_angles(a, 0, (uint16_t)g.sweep);
  lv_arc_set_angles(a, 0, 0);
  lv_obj_remove_style(a, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(a, ARC_W, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, COL_RING, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, ARC_W, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, COL_ACCENT, LV_PART_INDICATOR);
  return a;
}

static void addSegGaps(lv_obj_t* p, const Geo& g) {
  const float r_out = R + ARC_W / 2.0f + 3, r_in = R - ARC_W / 2.0f - 3;
  for (int i = 1; i < g.nseg; i++) addTick(p, g.start + g.sweep * i / g.nseg, r_out, r_in, COL_DIAL, SEG_GAP_W);
}

static lv_obj_t* makeLabel(lv_obj_t* p, const lv_font_t* font, lv_color_t col, int dy) {
  lv_obj_t* l = lv_label_create(p);
  lv_label_set_text(l, "");
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, col, 0);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, dy);
  return l;
}

// Ticks and numerals along the gauge's scale; the boost zero and the stoichiometric
// AFR get a brighter, thicker mark so they are easy to find.
static void buildScale(lv_obj_t* p, OgChan ch, const Geo& g) {
  const DScale sc = scaleFor(ch, shown_bar);
  for (int i = 0; i <= sc.majors; i++) {
    const float frac = (float)i / sc.majors, a = g.start + frac * g.sweep;
    const int   val  = (int)lroundf(sc.lo + frac * (sc.hi - sc.lo));
    const bool  zero = (ch == OG_CH_BOOST && val == 0);
    addTick(p, a, R - 26, R - 44, zero ? COL_INFO : COL_TICKMAJ, zero ? 4 : 3);
    addNum(p, a, val);
  }
  const int total = sc.majors * sc.minors;
  for (int i = 0; i <= total; i++) {
    if (i % sc.minors == 0) continue;
    addTick(p, g.start + ((float)i / total) * g.sweep, R - 26, R - 38, COL_TICK, 2);
  }
  if (ch == OG_CH_AFR && g_set.afr_stoich > sc.lo && g_set.afr_stoich < sc.hi)
    addTick(p, g.start + (g_set.afr_stoich - sc.lo) / (sc.hi - sc.lo) * g.sweep, R - 26, R - 44, COL_INFO, 4);
}

static void buildDial(Dial& d, lv_obj_t* p, OgChan ch, const Geo& g, bool isSplit, bool bottom) {
  d.ch = ch; d.geo = g;
  d.sh_val = INT32_MIN; d.sh_level = 255; d.sh_status = -1; d.sh_angle = 0xFFFF;
  buildScale(p, ch, g);
  d.arc = makeArc(p, g);
  addSegGaps(p, g);
  const int numY = !isSplit ? 0 : (bottom ? 70 : -70);
  const int capY = !isSplit ? 70 : (bottom ? 28 : -25);
  d.num = makeLabel(p, &lv_font_montserrat_48, COL_ACCENT, numY);
  lv_label_set_text(d.num, "--");
  d.cap = makeLabel(p, &lv_font_montserrat_20, COL_DIM, capY);
  lv_label_set_text(d.cap, capFor(ch, shown_bar));
  d.stat = nullptr;
  if (!isSplit) d.stat = makeLabel(p, &lv_font_montserrat_20, COL_WARN, -70);
}

static void destroyPage() {
  GL_Destroy();                              // the engine's object lives inside pg: let it go first
  custom_view = -1;
  if (pg) { lv_obj_del(pg); pg = nullptr; }
  freeTicks();
  ndials = 0;
}

static void layoutDots() {
  // On a single gauge the dots sit in the gap at the bottom; on a split screen both
  // arcs reach the screen's top and bottom, so they sit in the middle, on the divider.
  const int y = split ? 0 : 218;
  const int n = rot_n > 1 ? rot_n : 0;
  for (int i = 0; i < OG_MAX_PAGES; i++) {
    if (i >= n) { lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_clear_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(dots[i], LV_ALIGN_CENTER, (lv_coord_t)lroundf((i - (n - 1) / 2.0f) * 18.0f), y);
    lv_obj_set_style_bg_color(dots[i], i == cur ? COL_INFO : COL_TICK, 0);
  }
}

static const char* gaugeName(uint8_t g) { return og_pg_name(g); }

static void buildPage() {
  destroyPage();
  shown_bar = useBar();
  const uint8_t code = rot[cur];
  const bool designed = og_page_is_custom(code);
  const uint8_t top = designed ? 0 : og_page_top(code), bot = designed ? 0 : og_page_bot(code);
  split = (bot != 0);

  // the face and background go back to normal; a designed view may change them below
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_color(face, COL_DIAL, 0);
  lv_obj_clear_flag(face, LV_OBJ_FLAG_HIDDEN);

  pg = lv_obj_create(scr);
  lv_obj_remove_style_all(pg);
  lv_obj_set_size(pg, 480, 480);
  lv_obj_set_pos(pg, 0, 0);
  lv_obj_clear_flag(pg, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(pg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_to_index(pg, 1);               // just above the face, behind the labels

  if (designed) {
    const int v = og_page_custom_view(code);
    lv_color_t bgc; const bool has_bg = GL_ViewBg(v, &bgc);
    if (GL_ViewFace(v)) { if (has_bg) lv_obj_set_style_bg_color(face, bgc, 0); }
    else { lv_obj_add_flag(face, LV_OBJ_FLAG_HIDDEN); if (has_bg) lv_obj_set_style_bg_color(scr, bgc, 0); }
    GL_SetUnits({false, useBar()});
    GL_Build(v, pg, millis());
    custom_view = v;
    ndials = 0;
  } else if (!split) {
    ndials = 1;
    buildDial(dials[0], pg, (OgChan)(top - 1), GEO_FULL, false, false);
  } else {
    ndials = 2;
    buildDial(dials[0], pg, (OgChan)(top - 1), GEO_TOP, true, false);
    buildDial(dials[1], pg, (OgChan)(bot - 1), GEO_BOT, true, true);
    static lv_point_t dv[2][2] = { {{(lv_coord_t)(CX - 150), (lv_coord_t)CY}, {(lv_coord_t)(CX - 45), (lv_coord_t)CY}},
                                   {{(lv_coord_t)(CX + 45),  (lv_coord_t)CY}, {(lv_coord_t)(CX + 150), (lv_coord_t)CY}} };
    for (int i = 0; i < 2; i++) {            // the middle is left open for the screen dots
      lv_obj_t* line = lv_line_create(pg);
      lv_line_set_points(line, dv[i], 2);
      lv_obj_set_style_line_color(line, COL_TICKMAJ, 0);
      lv_obj_set_style_line_width(line, 2, 0);
    }
  }
  layoutDots();
  if (split || designed) lv_obj_add_flag(sensor_lbl, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(sensor_lbl, LV_OBJ_FLAG_HIDDEN);

  lv_mem_monitor_t m; lv_mem_monitor(&m);
  if (designed)
    Serial.printf("[ui] screen %d of %d: designed view '%s'  - LVGL heap %u of %u bytes used\n",
                  cur + 1, rot_n, GL_ViewId(custom_view), (unsigned)(m.total_size - m.free_size), (unsigned)m.total_size);
  else
    Serial.printf("[ui] screen %d of %d: %s%s%s  - LVGL heap %u of %u bytes used, %u objects' tick points\n",
                  cur + 1, rot_n, gaugeName(top), split ? " / " : "", split ? gaugeName(bot) : "",
                  (unsigned)(m.total_size - m.free_size), (unsigned)m.total_size, (unsigned)tick_n);
}

// ---- buzzer ----------------------------------------------------------------
static bool buzzer_on = false;
static void buzzer(bool on) {
  if (!g_set.buzzer || demo_on) on = false;                // (never sounds for invented readings)
  if (on != buzzer_on) { buzzer_on = on; board_buzzer(on); }
}

// ---- toast (short message at the top) ---------------------------------------
static void toast(const char* txt, uint32_t ms = 1500) {
  lv_label_set_text(toast_lbl, txt);
  lv_obj_clear_flag(toast_lbl, LV_OBJ_FLAG_HIDDEN);
  toast_hide_at = millis() + ms;
}

static void saveNow() {
  if (!OilSettings_Save()) toast("NOT SAVED", 3000);
}

// ---- reset-Wi-Fi prompt (only exists while open) ----------------------------
static void closeConfirm() {
  if (confirm) { lv_obj_del(confirm); confirm = nullptr; }
}
static void onResetYes(lv_event_t*) {
  closeConfirm();
  OilWeb_FactoryResetWifi();
  toast("WI-FI RESET", 2500);
}
static void onResetNo(lv_event_t*) { closeConfirm(); }

static lv_obj_t* promptButton(lv_obj_t* parent, const char* txt, lv_color_t bg, lv_align_t al, lv_event_cb_t cb) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, 150, 56);
  lv_obj_align(b, al, al == LV_ALIGN_BOTTOM_LEFT ? 18 : -18, -16);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_radius(b, 12, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_obj_center(l);
  return b;
}

static void openConfirm() {
  if (confirm) return;
  confirm = lv_obj_create(scr);
  lv_obj_set_size(confirm, 360, 230);
  lv_obj_center(confirm);
  lv_obj_set_style_radius(confirm, 24, 0);
  lv_obj_set_style_bg_color(confirm, lv_color_hex(0x17171b), 0);
  lv_obj_set_style_border_color(confirm, COL_WARN, 0);
  lv_obj_set_style_border_width(confirm, 3, 0);
  lv_obj_clear_flag(confirm, LV_OBJ_FLAG_SCROLLABLE);     // clickable stays on: it swallows presses meant for the dial

  lv_obj_t* t = lv_label_create(confirm);
  lv_label_set_text(t, "RESET WI-FI SETTINGS?");
  lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(t, COL_WARN, 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t* b = lv_label_create(confirm);
  lv_label_set_text(b, "The Wi-Fi name and password go\nback to the factory values.\nNothing else is changed.");
  lv_obj_set_style_text_font(b, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(b, COL_INFO, 0);
  lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 52);

  promptButton(confirm, "CANCEL", lv_color_hex(0x3a3a42), LV_ALIGN_BOTTOM_LEFT,  onResetNo);
  promptButton(confirm, "RESET",  COL_CRIT,               LV_ALIGN_BOTTOM_RIGHT, onResetYes);
  confirm_until = millis() + 10000;
  Serial.println("[ui] reset Wi-Fi prompt shown");
}

// ---- touch -----------------------------------------------------------------
static int      press_x = -1, press_y = -1;
static bool     press_live = false, long_done = false;
static uint32_t press_t = 0, last_action = 0;

static void onPressed(lv_event_t* e) {
  press_live = true; long_done = false; press_x = press_y = -1; press_t = millis();
  lv_indev_t* indev = lv_event_get_indev(e);
  if (!indev) indev = lv_indev_get_act();
  if (indev) { lv_point_t p; lv_indev_get_point(indev, &p); press_x = p.x; press_y = p.y; }
}

static void wifiSwitch(bool on) {
  if (on) {
    toast("WI-FI STARTING", 4000);
    lv_timer_handler();                          // paint it before the (blocking) start-up
  }
  const char* why = "";
  if (OilWeb_Enable(on, &why)) toast(on ? "WI-FI ON" : "WI-FI OFF");
  else toast(why[0] ? why : "WI-FI ERROR", 3000);
}

static bool rebuild_units = false;               // a unit change rebuilds the page (done in Update)

static void onReleased(lv_event_t* e) {
  if (!press_live) return;
  press_live = false;
  if (confirm || long_done) return;
  uint32_t now = millis();
  if (now - last_action < 300 || press_x < 0) return;
  lv_indev_t* indev = lv_event_get_indev(e);
  if (!indev) indev = lv_indev_get_act();
  if (!indev) return;
  lv_point_t p; lv_indev_get_point(indev, &p);
  int dx = (int)p.x - press_x, dy = (int)p.y - press_y;
  int adx = abs(dx), ady = abs(dy);

  if (ady >= OG_SWIPE_MIN_PX && ady * 2 > adx * 3) {            // vertical: Wi-Fi
    last_action = now;
    wifi_req = (dy < 0) ? 1 : -1;                                // up = on, down = off; done in Update(),
                                                                 // because lv_timer_handler() cannot be re-entered from here
  } else if (adx >= OG_SWIPE_MIN_PX && adx * 2 > ady * 3) {     // horizontal: change screen
    last_action = now;
    page_req = (dx < 0) ? 1 : -1;                                // swipe left = next, right = previous
  } else if (adx <= OG_TAP_MAX_PX && ady <= OG_TAP_MAX_PX) {    // tap
    last_action = now;
    if (alert && !acked) {
      acked = true;                                              // silence it; stays red
      Serial.println("[oil] alert acknowledged");
    } else {
      g_set.units_bar = useBar() ? 0 : 1;
      rebuild_units = true;                                      // done in Update, not here
      saveNow();
      Serial.printf("[ui] units -> %s\n", useBar() ? "bar" : "psi");
    }
  }
}

// A finger held still for OG_LONGPRESS_MS opens the reset prompt. Polled from
// Update(): LVGL's own long-press event is fixed at a fraction of a second.
static void checkLongPress(uint32_t now) {
  if (!press_live || long_done || confirm) return;
  if (now - press_t < OG_LONGPRESS_MS) return;
  long_done = true;                                              // one decision per press
  lv_indev_t* indev = lv_indev_get_next(NULL);
  if (!indev || press_x < 0) return;
  lv_point_t p; lv_indev_get_point(indev, &p);
  if (abs((int)p.x - press_x) <= OG_TAP_MAX_PX && abs((int)p.y - press_y) <= OG_TAP_MAX_PX) {
    press_live = false;
    openConfirm();
  }
}

// ---- build -----------------------------------------------------------------
void OilUI_Begin() {
  board_backlight(g_set.backlight);

  GlPalette pal;                                         // what the layout engine's theme roles mean on this gauge
  pal.gauge = pal.text = pal.accent = pal.ok = COL_ACCENT;
  pal.dim = COL_DIM; pal.track = COL_RING; pal.tick = COL_TICK; pal.tickmaj = COL_TICKMAJ;
  pal.dial = COL_DIAL; pal.warn = COL_WARN; pal.crit = COL_CRIT;
  GL_SetPalette(pal);
  for (int i = 0; i < GI_COUNT; i++) kv[i] = NAN;

  scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  face = lv_obj_create(scr);
  lv_obj_set_size(face, 476, 476);
  lv_obj_center(face);
  lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(face, COL_DIAL, 0);
  lv_obj_set_style_border_color(face, COL_RING, 0);
  lv_obj_set_style_border_width(face, 3, 0);
  lv_obj_clear_flag(face, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(face, LV_OBJ_FLAG_CLICKABLE);   // touches pass through to the screen

  // Wi-Fi name / address, only while Wi-Fi is on. The password is deliberately NEVER shown.
  wifi_lbl = lv_label_create(scr);
  lv_label_set_text(wifi_lbl, "");
  lv_obj_set_style_text_font(wifi_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(wifi_lbl, COL_INFO, 0);
  lv_obj_set_style_text_align(wifi_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(wifi_lbl, LV_ALIGN_CENTER, 0, 140);
  lv_obj_add_flag(wifi_lbl, LV_OBJ_FLAG_HIDDEN);

  sensor_lbl = lv_label_create(scr);
  lv_label_set_text(sensor_lbl, "");
  lv_obj_set_style_text_font(sensor_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(sensor_lbl, COL_TICKMAJ, 0);
  lv_obj_align(sensor_lbl, LV_ALIGN_CENTER, 0, 195);

  // a banner near the top: "UPDATING FIRMWARE", or the low-oil alarm when no
  // gauge on the current screen shows oil pressure
  banner_lbl = lv_label_create(scr);
  lv_label_set_text(banner_lbl, "");
  lv_obj_set_style_text_font(banner_lbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(banner_lbl, COL_WARN, 0);
  lv_obj_align(banner_lbl, LV_ALIGN_CENTER, 0, -110);
  lv_obj_add_flag(banner_lbl, LV_OBJ_FLAG_HIDDEN);

  demo_lbl = lv_label_create(lv_layer_top());       // "DEMO 87": on the top layer, so it is over every screen
  lv_label_set_text(demo_lbl, "DEMO");
  lv_obj_set_style_text_font(demo_lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(demo_lbl, lv_color_hex(0x101010), 0);
  lv_obj_set_style_bg_color(demo_lbl, COL_WARN, 0);
  lv_obj_set_style_bg_opa(demo_lbl, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(demo_lbl, 8, 0);
  lv_obj_set_style_pad_hor(demo_lbl, 10, 0);
  lv_obj_set_style_pad_ver(demo_lbl, 2, 0);
  lv_obj_align(demo_lbl, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_add_flag(demo_lbl, LV_OBJ_FLAG_HIDDEN);

  toast_lbl = lv_label_create(scr);
  lv_obj_set_style_text_font(toast_lbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(toast_lbl, COL_INFO, 0);
  lv_obj_align(toast_lbl, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_add_flag(toast_lbl, LV_OBJ_FLAG_HIDDEN);

  for (int i = 0; i < OG_MAX_PAGES; i++) {          // screen indicator dots
    dots[i] = lv_obj_create(scr);
    lv_obj_remove_style_all(dots[i]);
    lv_obj_set_size(dots[i], 9, 9);
    lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dots[i], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dots[i], COL_TICK, 0);
    lv_obj_clear_flag(dots[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_add_event_cb(scr, onPressed,  LV_EVENT_PRESSED,  NULL);
  lv_obj_add_event_cb(scr, onReleased, LV_EVENT_RELEASED, NULL);

  rot_n = 0; cur = 0;
  buildRotation();
  buildPage();

  lv_scr_load(scr);

  lv_mem_monitor_t m; lv_mem_monitor(&m);               // how much of LVGL's own heap the dial takes
  Serial.printf("[lvgl] heap: %u of %u bytes used (%u%%), largest free block %u\n",
                (unsigned)(m.total_size - m.free_size), (unsigned)m.total_size,
                (unsigned)m.used_pct, (unsigned)m.free_biggest_size);
}

// ---- called by the web page code ---------------------------------------------
bool OilUI_Armed() { return demo_on ? demo_saved_armed : armed; }

void OilUI_DemoSet(bool on) { demo_req = on ? 1 : -1; }          // acted on in OilUI_Update, with the other screen changes
int  OilUI_DemoLeft() {
  if (demo_on) return og_demo_left_s(true, millis() - demo_t0);
  return demo_req > 0 ? (int)(OG_DEMO_MS / 1000) : 0;
}

void OilUI_SettingsChanged(bool full) {
  board_backlight(g_set.backlight);
  if (!full) return;                                    // brightness only: leave the screen alone
  buildRotation();
  rebuild_page = true;                                  // units, limits, screens or curves may have moved
}

// The layout engine is about to swap its views: drop the screen that holds one.
void OilUI_LayoutWillChange() { destroyPage(); }

void OilUI_Toast(const char* text) { toast(text); }

void OilUI_Busy(const char* text) {
  if (text && text[0]) {
    busy = true;
    lv_label_set_text(banner_lbl, text);
    lv_obj_set_style_text_color(banner_lbl, COL_WARN, 0);
    lv_obj_clear_flag(banner_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_timer_handler(); lv_timer_handler();            // paint now: the caller is about to block
  } else {
    busy = false;                                      // Update() redraws the real banner
    lv_obj_add_flag(banner_lbl, LV_OBJ_FLAG_HIDDEN);
    sh_banner = -1;
  }
}

// ---- update ----------------------------------------------------------------
enum Status { ST_NONE = 0, ST_NO_SENSOR, ST_WIRING, ST_NO_DATA, ST_NEEDS_INA,
              ST_OIL_LOW, ST_OIL_ALERT, ST_BOOST_HIGH, ST_AFR_LEAN, ST_AFR_RICH };

static const char* statusText(int s) {
  switch (s) {
    case ST_NO_SENSOR:  return "NO SENSOR";
    case ST_WIRING:     return "CHECK WIRING";
    case ST_NO_DATA:    return "NO DATA";
    case ST_NEEDS_INA:  return "NEEDS INA3221";
    case ST_OIL_LOW:    return "LOW PRESSURE";
    case ST_OIL_ALERT:  return "LOW OIL PRESSURE";
    case ST_BOOST_HIGH: return "HIGH BOOST";
    case ST_AFR_LEAN:   return "LEAN";
    case ST_AFR_RICH:   return "RICH";
  }
  return "";
}

struct ChanState {
  bool     good;
  int32_t  disp;            // value in display steps (see fmtFor), valid when good
  float    shown;           // value in the displayed unit, for the arc
  OgLevel  level;
  int16_t  status;
  lv_color_t scol;
};
static ChanState cs[OG_NCH];

static int faultStatus(OilFault f) {
  switch (f) {
    case OIL_FAULT_NO_SENSOR:                       return ST_NO_SENSOR;
    case OIL_FAULT_WIRING_LOW:
    case OIL_FAULT_WIRING_HIGH:                     return ST_WIRING;
    case OIL_FAULT_NO_DATA:                         return ST_NO_DATA;
    case OIL_FAULT_UNSUPPORTED:                     return ST_NEEDS_INA;
    default:                                        return ST_NO_SENSOR;       // OFF: a screen cannot show it
  }
}

// Value in the unit on screen, as display steps.
static void setValue(ChanState& s, OgChan ch, float v /*psi for oil/boost, AFR for afr*/) {
  const bool bar = shown_bar;
  float x = v;
  if (ch != OG_CH_AFR && bar) x = v / OG_PSI_PER_BAR;
  const Fmt f = fmtFor(ch, bar);
  s.disp  = (int32_t)lroundf(x * 100.0f / f.step100);
  s.shown = x;
}

static void demoStop(const char* why) {
  if (!demo_on) return;
  demo_on = false;
  armed = demo_saved_armed;                  // what the demo "armed" must not carry over: the real latch comes back
  alert = false; acked = false; crit_since = 0; sh_banner = -1;
  buzzer(false);
  GL_ResetPeaks();                           // the demo's highs are not real peaks
  if (demo_lbl) lv_obj_add_flag(demo_lbl, LV_OBJ_FLAG_HIDDEN);
  demo_lbl_s = -1;
  Serial.printf("[demo] stopped (%s)\n", why);
}
static void demoStart(uint32_t now) {
  if (demo_on) { demo_t0 = now ? now : 1; return; }          // asked again: another two minutes
  demo_on = true; demo_t0 = now ? now : 1;
  demo_saved_armed = armed;
  armed = false; alert = false; acked = false; crit_since = 0;   // the demo arms itself as its oil pressure rises
  GL_ResetPeaks();
  Serial.println("[demo] started: the screens show INVENTED readings for up to two minutes");
}

static void updateChannels(uint32_t now) {
  ChanReading ro = OilSensor_Get(OG_CH_OIL), rb = OilSensor_Get(OG_CH_BOOST), ra = OilSensor_Get(OG_CH_AFR);
  if (demo_on) {
    // A real low-oil alarm wins: keep track of whether the engine has REALLY run, and end the demo if it is really low.
    const bool real_good = ro.fault == OIL_FAULT_NONE;
    if (real_good && ro.val >= g_set.arm_psi) demo_saved_armed = true;
    if (og_oil_level(real_good, demo_saved_armed, ro.val) == OG_LV_CRITICAL) demoStop("real low oil pressure");
  }
  if (demo_on) {
    ChanReading d[OG_NCH];
    og_demo_readings(now - demo_t0, d);
    ro = d[OG_CH_OIL]; rb = d[OG_CH_BOOST]; ra = d[OG_CH_AFR];
  }
  const bool good_o = ro.fault == OIL_FAULT_NONE, good_b = rb.fault == OIL_FAULT_NONE, good_a = ra.fault == OIL_FAULT_NONE;

  // --- oil: engine-has-run latch, then the level, then the debounced alert ---
  if (good_o && ro.val >= g_set.arm_psi && !armed) {
    armed = true;
    Serial.printf("[oil] armed (%.1f psi seen) - low-pressure alert is live\n", (double)ro.val);
  }
  const OgLevel lo = og_oil_level(good_o, armed, ro.val);
  if (lo == OG_LV_CRITICAL) {
    if (!crit_since) crit_since = now ? now : 1;
    if (!alert && now - crit_since >= OG_ALERT_DEBOUNCE_MS) {
      alert = true; acked = false;
      Serial.printf("[oil] ALERT: %.1f psi\n", (double)ro.val);
      // jump to a screen that shows oil pressure
      if (!pageShows(rot[cur], OG_PG_OIL)) {
        for (int i = 0; i < rot_n; i++) if (pageShows(rot[i], OG_PG_OIL)) { cur = i; rebuild_page = true; break; }
      }
    }
  } else {
    crit_since = 0;
    if (alert) { alert = false; acked = false; Serial.println("[oil] alert cleared"); }
  }

  ChanState& o = cs[OG_CH_OIL];
  o.good = good_o; o.level = lo; o.scol = COL_WARN;
  if (good_o) setValue(o, OG_CH_OIL, ro.val);
  o.status = !good_o ? faultStatus(ro.fault) : alert ? ST_OIL_ALERT : (lo == OG_LV_WARNING ? ST_OIL_LOW : ST_NONE);
  if (o.status == ST_OIL_ALERT) o.scol = COL_CRIT;

  // --- boost ---
  ChanState& b = cs[OG_CH_BOOST];
  b.good = good_b; b.level = og_boost_level(good_b, rb.val); b.scol = COL_WARN;
  if (good_b) setValue(b, OG_CH_BOOST, rb.val);
  if (!good_b) b.status = faultStatus(rb.fault);
  else if (b.level == OG_LV_CRITICAL) { b.status = ST_BOOST_HIGH; b.scol = COL_CRIT; }
  else if (b.level == OG_LV_WARNING)  { b.status = ST_BOOST_HIGH; }
  else b.status = ST_NONE;

  // --- AFR (the lean limits need the boost reading as the "under load" signal) ---
  ChanState& a = cs[OG_CH_AFR];
  a.good = good_a; a.level = og_afr_level(good_a, ra.val, good_b, rb.val); a.scol = COL_WARN;
  if (good_a) setValue(a, OG_CH_AFR, ra.val);
  if (!good_a) a.status = faultStatus(ra.fault);
  else if (a.level == OG_LV_CRITICAL) { a.status = ST_AFR_LEAN; a.scol = COL_CRIT; }
  else if (a.level == OG_LV_WARNING)  { a.status = ST_AFR_LEAN; }
  else if (a.level == OG_LV_INFO)     { a.status = ST_AFR_RICH; a.scol = COL_RICH; }
  else a.status = ST_NONE;

  // --- the same readings, for designed views (NaN = no reading, never a made-up number) ---
  kv[GI_oil_psi]        = good_o ? ro.val : NAN;
  kv[GI_boost_psi]      = good_b ? rb.val : NAN;
  kv[GI_afr]            = good_a ? ra.val : NAN;
  kv[GI_oil_status]     = !good_o ? NAN : (lo == OG_LV_CRITICAL ? (alert ? 3.0f : 2.0f) : lo == OG_LV_WARNING ? 2.0f : 1.0f);
  kv[GI_engine_running] = armed ? 1.0f : 0.0f;
}

static lv_color_t levelColor(OgLevel l) {
  switch (l) {
    case OG_LV_CRITICAL: return COL_CRIT;
    case OG_LV_WARNING:  return COL_WARN;
    case OG_LV_INFO:     return COL_RICH;
    case OG_LV_FAULT:    return COL_DIM;
    default:             return COL_ACCENT;
  }
}

static void formatValue(char* out, size_t n, int32_t v, int decimals) {
  const int32_t a = v < 0 ? -v : v;
  const char* sg = v < 0 ? "-" : "";
  if (decimals == 0)      snprintf(out, n, "%s%d", sg, (int)a);
  else if (decimals == 1) snprintf(out, n, "%s%d.%d", sg, (int)(a / 10), (int)(a % 10));
  else                    snprintf(out, n, "%s%d.%02d", sg, (int)(a / 100), (int)(a % 100));
}

static void updateDial(Dial& d) {
  const ChanState& s = cs[d.ch];
  const int32_t val = s.good ? s.disp : INT32_MIN;

  if (val != d.sh_val || s.level != d.sh_level) {
    if (val == INT32_MIN) {
      lv_label_set_text(d.num, "--");
      if (d.sh_angle != 0) { lv_arc_set_angles(d.arc, 0, 0); d.sh_angle = 0; }
    } else {
      char t[16]; formatValue(t, sizeof t, val, fmtFor(d.ch, shown_bar).decimals);
      lv_label_set_text(d.num, t);
      const DScale sc = scaleFor(d.ch, shown_bar);
      float frac = (s.shown - sc.lo) / (sc.hi - sc.lo);
      if (frac < 0) frac = 0;
      if (frac > 1) frac = 1;
      const uint16_t ang = (uint16_t)(frac * d.geo.sweep);
      if (ang != d.sh_angle) { lv_arc_set_angles(d.arc, 0, ang); d.sh_angle = ang; }
    }
    if (s.level != d.sh_level) {
      const lv_color_t col = levelColor(s.level);
      lv_obj_set_style_arc_color(d.arc, col, LV_PART_INDICATOR);
      lv_obj_set_style_text_color(d.num, col, 0);
    }
    d.sh_val = val; d.sh_level = s.level;
  }

  if (!busy && s.status != d.sh_status) {
    if (d.stat) {                                       // single gauge: status above the number
      lv_label_set_text(d.stat, statusText(s.status));
      lv_obj_set_style_text_color(d.stat, s.scol, 0);
    } else if (s.status != ST_NONE) {                   // split: the caption shows it
      lv_label_set_text(d.cap, statusText(s.status));
      lv_obj_set_style_text_color(d.cap, s.scol, 0);
    } else {
      lv_label_set_text(d.cap, capFor(d.ch, shown_bar));
      lv_obj_set_style_text_color(d.cap, COL_DIM, 0);
    }
    d.sh_status = s.status;
  }
}

// A designed view can ask to take over the screen (a gauge in it has been red for a
// while). Only views that are in the rotation, never while the oil alarm already has the
// screen, and a swipe away is respected for 5 seconds.
static uint32_t swipe_t = 0, takeover_since = 0;
static void checkTakeover(uint32_t now) {
  if (alert || rot_n < 2) { takeover_since = 0; return; }
  const int v = GL_TakeoverWanted(kv, 1);
  int idx = -1;
  if (v >= 0) for (int i = 0; i < rot_n; i++) if (rot[i] == og_page_custom_make(v)) { idx = i; break; }
  if (idx < 0 || idx == cur) { takeover_since = 0; return; }
  if (!takeover_since) takeover_since = now ? now : 1;
  if (now - takeover_since >= 1500 && (swipe_t == 0 || now - swipe_t >= 5000)) {
    cur = idx; rebuild_page = true; takeover_since = 0;
    Serial.printf("[ui] designed view '%s' took over the screen\n", GL_ViewId(v));
  }
}

void OilUI_Update() {
  uint32_t now = millis();

  if (wifi_req) { bool on = wifi_req > 0; wifi_req = 0; wifiSwitch(on); now = millis(); }
  checkLongPress(now);
  if (confirm && (int32_t)(now - confirm_until) >= 0) { closeConfirm(); Serial.println("[ui] reset prompt timed out"); }

  if (page_req) {
    if (rot_n > 1) { cur = (cur + page_req + rot_n) % rot_n; rebuild_page = true; swipe_t = now ? now : 1; }
    else toast("ONLY ONE SCREEN SET", 1500);
    page_req = 0;
  }
  if (demo_req) { const int8_t r = demo_req; demo_req = 0; if (r > 0) demoStart(now); else demoStop("stopped from the phone page"); }
  if (demo_on && now - demo_t0 >= OG_DEMO_MS) demoStop("time is up");
  if (demo_on && demo_lbl) {                 // DEMO tag with the seconds left
    const int s = og_demo_left_s(true, now - demo_t0);
    if (s != demo_lbl_s) {
      char t[24]; snprintf(t, sizeof t, "DEMO %d", s);
      lv_label_set_text(demo_lbl, t);
      lv_obj_clear_flag(demo_lbl, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(demo_lbl);
      demo_lbl_s = s;
    }
  }
  if (rebuild_units) { rebuild_units = false; rebuild_page = true; }
  if (rebuild_page) {
    rebuild_page = false;
    buildPage();
  }
  if (toast_hide_at && (int32_t)(now - toast_hide_at) >= 0) {
    toast_hide_at = 0; lv_obj_add_flag(toast_lbl, LV_OBJ_FLAG_HIDDEN);
  }

  updateChannels(now);                       // may switch screen on a new oil alert
  if (rebuild_page) { rebuild_page = false; buildPage(); }
  for (int i = 0; i < ndials; i++) updateDial(dials[i]);
  if (custom_view >= 0) GL_Update(kv, now);
  checkTakeover(now);

  // --- banner: the low-oil alarm when this screen does not show oil pressure ---
  if (!busy) {
    const bool want = alert && !pageShows(rot[cur], OG_PG_OIL);
    if ((int8_t)want != sh_banner) {
      if (want) {
        lv_label_set_text(banner_lbl, "LOW OIL PRESSURE");
        lv_obj_set_style_text_color(banner_lbl, COL_CRIT, 0);
        lv_obj_clear_flag(banner_lbl, LV_OBJ_FLAG_HIDDEN);
      } else lv_obj_add_flag(banner_lbl, LV_OBJ_FLAG_HIDDEN);
      sh_banner = (int8_t)want;
    }
  }

  // --- which sensor, small, at the bottom (single-gauge screens) ---
  static char sh_sensor[32] = "";
  const char* nm = OilSensor_Name();
  if (strcmp(nm, sh_sensor) != 0) {
    lv_label_set_text(sensor_lbl, nm);
    strncpy(sh_sensor, nm, sizeof sh_sensor - 1);
  }

  // --- Wi-Fi details while it is on ---
  static int8_t sh_wifi = -1; static char sh_wifi_txt[128] = "";
  const bool wifi = OilWeb_On();
  if (wifi) {
    char t[128];
    snprintf(t, sizeof t, "WI-FI  %s\n192.168.4.1", OilWeb_Ssid());
    if (sh_wifi != 1 || strcmp(t, sh_wifi_txt) != 0) {
      lv_label_set_text(wifi_lbl, t);
      lv_obj_clear_flag(wifi_lbl, LV_OBJ_FLAG_HIDDEN);
      strlcpy(sh_wifi_txt, t, sizeof sh_wifi_txt);
      sh_wifi = 1;
    }
  } else if (sh_wifi != 0) {
    lv_obj_add_flag(wifi_lbl, LV_OBJ_FLAG_HIDDEN);
    sh_wifi_txt[0] = 0; sh_wifi = 0;
  }

  // --- ring flash + buzzer while an unacknowledged oil alert is active ---
  static bool sh_flash = false;
  bool flash_red = alert && (acked || ((now / 400) % 2 == 0));
  if (flash_red != sh_flash) {
    lv_obj_set_style_border_color(face, flash_red ? COL_CRIT : COL_RING, 0);
    lv_obj_set_style_border_width(face, flash_red ? 8 : 3, 0);
    sh_flash = flash_red;
  }
  buzzer(alert && !acked && (now % 800) < 200);
}
