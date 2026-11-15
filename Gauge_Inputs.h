#pragma once
// ---------------------------------------------------------------------------
// Gauge_Inputs.h - the readings this gauge can show in a designed view.
//
// The layout engine (Gauge_Layout.cpp) and the layout editor both take their list
// of inputs from this table: a layout names an input by its `key`, and the engine
// is handed one number per input (NaN = no reading) every update.
//
// This gauge has five:
//   oil_psi         oil pressure, psi
//   oil_status      the gauge's own oil verdict: 1 normal, 2 amber, 3 red (waits for
//                   the engine to have run, and for the low-pressure delay)
//   boost_psi       boost (manifold pressure above ambient), psi; negative = vacuum
//   afr             air/fuel ratio
//   engine_running  1 once oil pressure has reached the arm level since power-up,
//                   else 0. Layout alerts marked "only when running" use it.
// ---------------------------------------------------------------------------
#include <stdint.h>
#include <string.h>

enum GiCat  : uint8_t { GI_NOCAT = 0, GI_TEMP, GI_PRESS, GI_PRESS_KPA, GI_SPEED };       // how units convert
enum GiKind : uint8_t { GI_NUM = 0, GI_BOOL, GI_GEAR, GI_LIGHTS, GI_OILST };              // how the number is read

struct GiInput {
  const char* key;        // name used in layout files
  const char* path;       // where "live" data finds it (the editor's live mode)
  const char* label;      // default caption
  const char* unit;       // unit text when the engine does not pick one itself
  uint8_t     cat, kind;
  float       def_min, def_max;   // default scale
  int8_t      def_dp;             // default digits after the point
};

enum GiId : int8_t {
  GI_oil_psi = 0,
  GI_oil_status,
  GI_boost_psi,
  GI_afr,
  GI_engine_running,
  GI_COUNT,
  // The engine also knows two other oil-pressure sources from the full 3-screen
  // system. Here there is only one, so they point at it.
  GI_oil_psi_direct = GI_oil_psi,
  GI_oil_psi_ecutek = GI_oil_psi
};

extern const GiInput GI_TABLE[GI_COUNT];

static inline int gi_find(const char* key) {
  if (!key) return -1;
  for (int i = 0; i < GI_COUNT; i++) if (!strcmp(GI_TABLE[i].key, key)) return i;
  return -1;
}

// "Only when running" in a layout alert: this gauge has no RPM, so running means
// the engine has demonstrably run (oil pressure reached the arm level).
#define GL_ENGINE_RUNNING(kv) ((kv)[GI_engine_running] >= 0.5f)
