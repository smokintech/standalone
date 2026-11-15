#pragma once
// ---------------------------------------------------------------------------
// oil_layout - designed views (made in the layout editor) kept on the gauge.
//
// A layout file is uploaded from the phone page. It is checked by the layout
// engine (Gauge_Layout.cpp); only a file the engine accepts is kept, in flash,
// and it is loaded again at every start-up. Views in it become extra screens in
// the Screens card.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include "oil_layout_store.h"

void   OilLayout_Begin();                       // find the flash area, load the stored layout (if any)

bool   OilLayout_Available();                   // false = this partition scheme has no room for a layout
bool   OilLayout_Loaded();                      // a layout is active
const char* OilLayout_LoadError();              // why the stored layout could not be used ("" = fine / none)
size_t OilLayout_StoredBytes();                 // size of the stored layout, 0 = none

// Check a layout file and, if the engine accepts it, switch to it and keep it.
// False = rejected (err says why) and nothing changed. `saved` tells whether the
// flash write worked: a layout that is active but not saved is lost at power-off.
bool   OilLayout_Apply(const char* json, size_t len, char* err, size_t errlen, bool* saved);

// Forget the layout (designed views disappear from the Screens card).
bool   OilLayout_Clear(char* err, size_t errlen);

// Copy the stored layout text out (for "download current"). Returns the bytes copied.
size_t OilLayout_ReadStored(uint8_t* dst, size_t cap);

// Can designed view v be a screen on this gauge? (a custom view, switched on, offered to screen 1)
bool   OilLayout_ViewUsable(int v);
