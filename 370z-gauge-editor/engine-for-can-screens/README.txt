Updated layout engine for the full CAN-bus screens
===================================================

Gauge_Layout.cpp / Gauge_Layout.h are your screens' layout engine with the newer drawing options from the
standalone oil gauge added. Nothing was removed: the gear number format and the brain's oil verdict
(alerts.status) are still there (this copy sets GL_CAN_BUS on its first line).

New options (all documented in ../LAYOUT_FORMAT.md; the editor shows them in both versions):
  * glow and bright centre on arcs and bars ("glow", "core"), plus the Apply lit-lamp look button
  * glowing seven-segment digits for the reading ("number": {"style": "seg7", ...})
  * scale marks in steps counted from zero, 0 always marked ("ticks": {"step": 5}, "by_unit.<unit>.step")
  * its own colour for each alert level ("alerts": {"colors": {"low_warn": "#3a8dff"}}): the zone on the
    scale, and the fill and reading while the value is in it
  * Number gauges ("digital") keep their digits still: width from the biggest value on the scale, lined up
    on the decimal point ("number": {"fixed": false} turns it off). The gear display is not affected.
  * arcs/rings with blocks have exactly even gaps (they were 1 or 2 degrees wide, unevenly)

To use it:
  1. Copy both files over the ones in the screens' sketch folder (LVGL_Arduino), keeping CRLF as they are.
  2. Run comp.sh to check it builds, raise GA_VERSION in Gauge_Config.h, then release.sh as usual.
  3. Until then the editor warns when you Export a layout that needs these options (it looks for the new
     engine in the sketch). Layouts that don't use them export and work exactly as before, apart from the
     even arc gaps and the fixed-width Number gauges above, which appear once the new engine is in.

Checked here: it compiles for the ESP32-S3 with no warnings against a rebuilt copy of the screens' input
table (the sketch's own Gauge_Inputs.cpp wasn't available), and the editor's CAN preview was built from it.
The sample and starter layouts render the same as with the old engine except for the even arc gaps. The
screens' own build (comp.sh) is the real test - please run it before releasing.

It uses GI_rpm for "only when running" alerts, as before.
