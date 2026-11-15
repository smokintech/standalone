// Gauge_Layout.h - custom views described by a layout file (LAYOUT_FORMAT.md).
//
// Portable: LVGL 8.3 + ArduinoJson only, no Arduino calls, so the same code
// runs on the screens, in the PC render test and (later) in the editor's
// preview. Every custom view is ONE LVGL object whose draw callback paints all
// its gauges - so a busy view costs almost nothing from the screens' LVGL heap.
#pragma once
#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>
#include "Gauge_Inputs.h"

struct GlPalette {               // the screen's current theme
  lv_color_t gauge, text, dim, accent, track, tick, tickmaj, dial, warn, crit, ok;
};
struct GlUnits {                 // the screen's own unit settings
  bool temp_f;                   // show temperatures in F
  bool oil_bar;                  // show oil pressure in bar
};

// Parse and check a layout file. On success it replaces the loaded views and
// returns true. On failure the views already loaded stay as they were and err
// says what's wrong and where ("view 2 'boost', gauge 3: unknown input 'bost'").
bool        GL_Load(const char* json, size_t len, char* err, size_t errlen);

int         GL_ViewCount();
const char* GL_ViewId(int v);
const char* GL_ViewName(int v);
bool        GL_ViewFace(int v);
bool        GL_ViewBg(int v, lv_color_t* out);   // the view sets its own background ("bg"): true + the colour
bool        GL_ViewOnScreen(int v, int screen_id);   // screen_id = GA_SCREEN_ID (1..8); false if "enabled": false

// Built-in views listed in the layout file ({"builtin":"oil", ...}): they set the
// swipe order and which screens show them; the screen code draws them itself.
const char* GL_ViewBuiltin(int v);                   // "" for a custom view, else the built-in's key ("oil")
bool        GL_ViewEnabled(int v);
bool        GL_HasBuiltins();                        // the file lists built-in views (else: all built-ins, first)
bool        GL_ViewUsesInput(int v, int input);      // a gauge in custom view v shows this input (GiId)
void        GL_ResetPeaks();                         // forget peak markers and peak-hold labels (after simulated readings)

void        GL_SetPalette(const GlPalette& p);       // call again after a theme change

// Images the layouts can show ("image" gauges name them). Call BEFORE GL_Load;
// the list and the images must stay in memory. The screen code registers its
// own (the 370Z car) and the ones the editor exported into Gauge_Layouts.h.
struct GlImage { const char* name; const lv_img_dsc_t* img; };
void        GL_SetImages(const GlImage* list, int n);

// The time for "clock" gauges (call each update; valid = false shows "--:--"),
// and the screen's own analogue/digital clock setting for clocks set to "auto".
struct GlClock { bool valid; int year, month, day, dow, hour, minute, second; };
void        GL_SetClock(const GlClock& c);
void        GL_SetClockFace(bool analog);
void        GL_SetUnits(const GlUnits& u);

// Create view v on parent (a 480x480 screen). Replaces any view already built.
// Returns the drawing object, or nullptr if v is out of range or a built-in entry.
lv_obj_t*   GL_Build(int v, lv_obj_t* parent, uint32_t now_ms);
void        GL_Destroy();                            // forget the built view (call BEFORE deleting its screen)
int         GL_Built();                              // index of the built view, or -1

// Feed the latest values (kv[GI_COUNT], NaN = no reading) to the built view.
// Call every screen update (~100 ms). Only invalidates when something visible changed.
void        GL_Update(const float* kv, uint32_t now_ms);

// A view on this screen with a "takeover" gauge in red right now, or -1.
// (The caller debounces, and gives the built-in oil/tyre alerts priority.)
int         GL_TakeoverWanted(const float* kv, int screen_id);
