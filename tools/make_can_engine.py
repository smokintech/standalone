#!/usr/bin/env python3
"""Make the copy of the layout engine for the full CAN-bus screens (adds GL_CAN_BUS: gear format and the
brain's oil verdict) from this project's Gauge_Layout.cpp. Writes CRLF files like the screens' repo.
    python3 tools/make_can_engine.py <output folder>"""
import os, sys
here = os.path.dirname(os.path.abspath(__file__)) + "/.."
out = sys.argv[1] if len(sys.argv) > 1 else "engine-for-can-screens"
os.makedirs(out, exist_ok=True)
cpp = open(here + "/Gauge_Layout.cpp", encoding="utf-8").read().replace("\r\n", "\n")
hdr = open(here + "/Gauge_Layout.h", encoding="utf-8").read().replace("\r\n", "\n")
open(out + "/Gauge_Layout.cpp", "w", newline="", encoding="utf-8").write(
    ("#define GL_CAN_BUS 1   // the full CAN-bus screens (adds the gear format and the brain's oil verdict)\n" + cpp).replace("\n", "\r\n"))
open(out + "/Gauge_Layout.h", "w", newline="", encoding="utf-8").write(hdr.replace("\n", "\r\n"))
print("wrote", out)
