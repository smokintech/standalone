// preview_core.cpp - the screens' own drawing code, compiled to WebAssembly for
// the layout editor's preview. LVGL 8.3.10 with the screens' lv_conf.h, the
// layout engine (Gauge_Layout.cpp) and the input table (Gauge_Inputs.cpp) are
// the very files that run on the gauges, so the preview is what the screen
// will show - pixel for pixel - not an imitation.
//
// The editor (JavaScript) talks to it through the pv_* exports below.
#include "lvgl.h"
#include "Gauge_Layout.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPORT(n) extern "C" __attribute__((export_name(#n)))

static lv_color_t s_fb[480 * 480];          // what the panel would show (RGB565)
static lv_disp_drv_t s_drv;
static lv_obj_t* s_scr = nullptr;
static char s_err[200];
static float s_kv[GI_COUNT];
static uint32_t s_now = 0;
static int s_view = -1;
static int s_accent = 0, s_dark = 1;        // theme: index into the screens' accent list

extern "C" void pv_images_clear();
static void flush(lv_disp_drv_t* d, const lv_area_t* a, lv_color_t* c) {
  for (int y = a->y1; y <= a->y2; y++) for (int x = a->x1; x <= a->x2; x++) s_fb[y * 480 + x] = *c++;
  lv_disp_flush_ready(d);
}

// Gauge.cpp themeApply(), same numbers.
static const uint32_t TH_RGB[7] = {0x1db954, 0xf5a623, 0xe2213b, 0x5ad2ff, 0xff7a18, 0xffffff, 0x000000};
static GlPalette palette() {
  GlPalette p;
  p.accent = lv_color_hex(TH_RGB[s_accent]);
  p.text = p.accent;
  p.gauge = (s_accent == 5 || s_accent == 6) ? lv_color_hex(s_dark ? 0x1db954 : 0x13893a) : p.accent;
  p.ok = p.gauge;
  p.warn = lv_color_hex(0xf5a623); p.crit = lv_color_hex(0xe2213b);
  if (s_dark) { p.dial = lv_color_hex(0x0d0d0f); p.track = lv_color_hex(0x1a1a1e); p.tick = lv_color_hex(0x3a3a42);
                p.tickmaj = lv_color_hex(0x6a6a76); p.dim = lv_color_hex(0x8a8a94); }
  else        { p.dial = lv_color_hex(0xf4f4f6); p.track = lv_color_hex(0xd6d6dc); p.tick = lv_color_hex(0xb2b2ba);
                p.tickmaj = lv_color_hex(0x7a7a84); p.dim = lv_color_hex(0x55555e); }
  return p;
}

EXPORT(pv_init) void pv_init() {
  lv_init();
  static lv_disp_draw_buf_t db;
  static lv_color_t buf[480 * 480];
  lv_disp_draw_buf_init(&db, buf, nullptr, 480 * 480);
  lv_disp_drv_init(&s_drv);
  s_drv.hor_res = 480; s_drv.ver_res = 480; s_drv.flush_cb = flush; s_drv.draw_buf = &db; s_drv.full_refresh = 1;
  lv_disp_drv_register(&s_drv);
  for (float& v : s_kv) v = NAN;
  pv_images_clear();
}

EXPORT(pv_alloc) void* pv_alloc(int n) { return malloc((size_t)n); }
EXPORT(pv_free) void pv_free(void* p) { free(p); }
EXPORT(pv_error) const char* pv_error() { return s_err; }
EXPORT(pv_kv) float* pv_kv() { return s_kv; }
EXPORT(pv_frame) const uint16_t* pv_frame() { return (const uint16_t*)s_fb; }

// Check + load a layout file. 1 = loaded, 0 = rejected (pv_error says why).
EXPORT(pv_load) int pv_load(const char* json, int len) {
  s_err[0] = 0;
  bool ok = GL_Load(json, (size_t)len, s_err, sizeof s_err);
  s_view = -1;                                // GL_Load drops any built view
  return ok ? 1 : 0;
}
EXPORT(pv_view_count) int pv_view_count() { return GL_ViewCount(); }
EXPORT(pv_view_id) const char* pv_view_id(int v) { return GL_ViewId(v); }

EXPORT(pv_theme) void pv_theme(int accent, int dark, int temp_f, int oil_bar) {
  s_accent = (accent >= 0 && accent < 7) ? accent : 0;
  s_dark = dark ? 1 : 0;
  GL_SetPalette(palette());
  GL_SetUnits({temp_f != 0, oil_bar != 0});
}

// Build view v as the screen would (background, face, the view) at time now_ms.
EXPORT(pv_show) int pv_show(int v, int now_ms) {
  s_now = (uint32_t)now_ms;
  lv_obj_t* old = s_scr;
  GL_Destroy();
  lv_obj_t* scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, s_dark ? lv_color_black() : lv_color_white(), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  GlPalette p = palette();
  lv_color_t vbg;
  bool has_bg = GL_ViewBg(v, &vbg);           // the view's own background (Gauge.cpp custOpen)
  if (!GL_ViewFace(v) && has_bg) lv_obj_set_style_bg_color(scr, vbg, 0);
  if (GL_ViewFace(v)) {                       // Gauge.cpp addFace()
    lv_obj_t* f = lv_obj_create(scr);
    lv_obj_set_size(f, 476, 476); lv_obj_center(f);
    lv_obj_set_style_radius(f, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(f, has_bg ? vbg : p.dial, 0);
    lv_obj_set_style_border_color(f, p.track, 0);
    lv_obj_set_style_border_width(f, 3, 0);
    lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE);
  }
  lv_scr_load(scr);
  if (old) lv_obj_del(old);
  s_scr = scr;
  s_view = GL_Build(v, scr, s_now) ? v : -1;
  return s_view >= 0 ? 1 : 0;
}

// Advance to now_ms with the values in pv_kv() and redraw.
EXPORT(pv_step) void pv_step(int now_ms) {
  uint32_t now = (uint32_t)now_ms;
  uint32_t dt = now - s_now; if (dt > 10000) dt = 10000;
  s_now = now;
  GL_Update(s_kv, now);
  lv_tick_inc(dt);
  lv_timer_handler();
  lv_obj_invalidate(lv_scr_act());            // always repaint - the editor shows every frame
  lv_refr_now(nullptr);
}

EXPORT(pv_takeover) int pv_takeover(int screen_id) { return GL_TakeoverWanted(s_kv, screen_id); }

// ---- images: the car the firmware always has, plus the editor's uploads ----
// (re-registered by the editor before each load: pv_images_clear, pv_image_add...)
#ifdef PV_SKETCH_HAS_CAR
#define PV_HAVE_CAR 1
extern "C" const lv_img_dsc_t car_370z;
#endif
#define PV_MAX_IMG 32
static GlImage s_imgs[PV_MAX_IMG];
static lv_img_dsc_t s_dsc[PV_MAX_IMG];
static char s_names[PV_MAX_IMG][28];
static int s_nimg = 0;
EXPORT(pv_images_clear) void pv_images_clear() {
  for (int i = 0; i < s_nimg; i++) if (s_dsc[i].data && i > 0) free((void*)s_dsc[i].data);
  s_nimg = 0;
#ifdef PV_HAVE_CAR
  s_imgs[0] = {"car_370z", &car_370z}; s_dsc[0].data = nullptr; s_nimg = 1;
#endif
  GL_SetImages(s_imgs, s_nimg);
}
// data: w*h*3 bytes (RGB565 low byte first, then alpha), copied here
EXPORT(pv_image_add) int pv_image_add(const char* name, int w, int h, const uint8_t* data) {
  if (s_nimg >= PV_MAX_IMG || w < 1 || h < 1 || w > 480 || h > 480) return 0;
  int i = s_nimg;
  size_t n = (size_t)w * h * 3;
  uint8_t* d = (uint8_t*)malloc(n); if (!d) return 0;
  memcpy(d, data, n);
  snprintf(s_names[i], sizeof s_names[i], "%s", name);
  memset(&s_dsc[i], 0, sizeof s_dsc[i]);
  s_dsc[i].header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA; s_dsc[i].header.w = (uint32_t)w; s_dsc[i].header.h = (uint32_t)h;
  s_dsc[i].data_size = (uint32_t)n; s_dsc[i].data = d;
  s_imgs[i] = {s_names[i], &s_dsc[i]};
  s_nimg++;
  GL_SetImages(s_imgs, s_nimg);
  lv_img_cache_invalidate_src(nullptr);
  return 1;
}
// the built-in car's size (0 if this build has no car): w << 16 | h
EXPORT(pv_car_size) int pv_car_size() {
#ifdef PV_HAVE_CAR
  return ((int)car_370z.header.w << 16) | (int)car_370z.header.h;
#else
  return 0;
#endif
}

// ---- clock: the browser's time, and the screen's analogue/digital setting ----
EXPORT(pv_clock) void pv_clock(int valid, int y, int mo, int d, int dow, int h, int mi, int sec, int analog) {
  GlClock c = {valid != 0, y, mo, d, dow, h, mi, sec};
  GL_SetClock(c);
  GL_SetClockFace(analog != 0);
}

EXPORT(pv_heap_used) int pv_heap_used() {
  lv_mem_monitor_t m; lv_mem_monitor(&m);
  return (int)(m.total_size - m.free_size);
}

// Size of a text in one of the screen fonts (same font choice as the engine:
// the nearest built-in size at or below). Returns width << 16 | height.
static const lv_font_t* fontAtOrBelow(int size) {
  static const struct { int s; const lv_font_t* f; } fl[] = {
    {48, &lv_font_montserrat_48}, {28, &lv_font_montserrat_28}, {20, &lv_font_montserrat_20},
    {16, &lv_font_montserrat_16}, {14, &lv_font_montserrat_14}, {12, &lv_font_montserrat_12}};
  for (auto& f : fl) if (f.s <= size) return f.f;
  return &lv_font_montserrat_12;
}
EXPORT(pv_text_size) int pv_text_size(const char* t, int font) {
  lv_point_t sz;
  lv_txt_get_size(&sz, t, fontAtOrBelow(font), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return ((int)sz.x << 16) | ((int)sz.y & 0xffff);
}

// The input table as JSON, so the editor's picker always matches the firmware.
EXPORT(pv_inputs) const char* pv_inputs() {
  static char* out = nullptr;
  if (out) return out;
  static const char* cats[] = {"none", "temp", "press", "press_kpa", "speed"};
  static const char* kinds[] = {"num", "bool", "gear", "lights", "oil_status"};
  size_t cap = 256 + GI_COUNT * 220;
  out = (char*)malloc(cap);
  size_t n = (size_t)snprintf(out, cap, "[");
  for (int i = 0; i < GI_COUNT; i++) {
    const GiInput& g = GI_TABLE[i];
    n += (size_t)snprintf(out + n, cap - n,
      "%s{\"key\":\"%s\",\"path\":\"%s\",\"label\":\"%s\",\"unit\":\"%s\",\"cat\":\"%s\",\"kind\":\"%s\",\"min\":%g,\"max\":%g,\"dp\":%d}",
      i ? "," : "", g.key, g.path, g.label, g.unit, cats[g.cat], kinds[g.kind], (double)g.def_min, (double)g.def_max, g.def_dp);
  }
  snprintf(out + n, cap - n, "]");
  return out;
}
