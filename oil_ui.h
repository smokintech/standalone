#pragma once
// Screen layer: one or two gauges per screen (oil / boost / AFR), swipe to change screen.
void OilUI_Begin();
void OilUI_Update();              // call every ~100 ms from loop()
bool OilUI_Armed();               // has the engine demonstrably run (oil alert live)?
void OilUI_SettingsChanged(bool full = true);   // after the phone page saved settings: rebuild the screens
                                                // (full = false: only the backlight changed, nothing to redraw)
void OilUI_LayoutWillChange();    // the layout engine is about to swap its views: drop the screen that holds one
void OilUI_DemoSet(bool on);      // "Run demo": invented readings on every screen for up to two minutes (see oil_demo.h)
int  OilUI_DemoLeft();            // seconds of demo left, 0 = not running
void OilUI_Toast(const char* text);
void OilUI_Busy(const char* text);   // text on the banner while a firmware install blocks the loop ("" = clear)
