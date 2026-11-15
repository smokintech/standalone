#!/bin/bash
# Build the editor's preview renderer from the screens' own code. web/preview.wasm is the standalone gauge (this project's firmware);
# web/preview_can.wasm is the same build made from the full CAN-bus screens' sketch folder.
#   ./build_wasm.sh <lvgl folder> <folder with the screens' lv_conf.h> <screen sketch folder> <ArduinoJson.h folder> <out.wasm>
# Needs Zig (pip install ziglang) - it brings clang and a C library for WebAssembly.
set -e
LV="$1"; CONF="$2"; SK="$3"; AJ="$4"; OUT="$5"
HERE="$(cd "$(dirname "$0")" && pwd)"
OBJ="$(mktemp -d)"
ZCC="python3 -m ziglang cc -target wasm32-wasi -O2 -DLV_CONF_INCLUDE_SIMPLE -I$HERE/stub -I$CONF -I$LV -I$LV/src"
n=0
for f in $(find "$LV/src" -name "*.c"); do
  o="$OBJ/$(echo "$f" | md5sum | cut -c1-12).o"
  $ZCC -w -c "$f" -o "$o" &
  n=$((n+1)); if [ $((n % 8)) -eq 0 ]; then wait; fi
done
wait
# the 370Z car picture the firmware has built in (car_370z.c in the sketch)
if [ -f "$SK/car_370z.c" ]; then $ZCC -w -c "$SK/car_370z.c" -o "$OBJ/car_370z.o"; fi
for f in "$SK/Gauge_Layout.cpp" "$SK/Gauge_Inputs.cpp" "$HERE/preview_core.cpp"; do
  $ZCC -x c++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wno-unused-function -I"$SK" -I"$AJ" $([ -f "$SK/car_370z.c" ] && echo -DPV_SKETCH_HAS_CAR) -c "$f" -o "$OBJ/$(basename "$f").o"
done
python3 -m ziglang cc -target wasm32-wasi -O2 -mexec-model=reactor -Wl,--strip-all "$OBJ"/*.o -o "$OUT"
rm -rf "$OBJ"
echo "built $OUT ($(stat -c %s "$OUT") bytes)"
