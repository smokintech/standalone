# Layout Editor (standalone oil gauge and full CAN-bus screens)

Design your own screens for the standalone oil gauge (Waveshare ESP32-S3 2.1" round
display), see them exactly as the gauge will draw them, then send them to the gauge
from your phone.

The preview is **the gauge's own drawing code** (LVGL 8.3.10 with the gauge's
`lv_conf.h`, `Gauge_Layout.cpp` and `Gauge_Inputs.cpp`) compiled for the browser
(`web/preview.wasm`). Fonts, colours, alert colours and animation are the real ones. If the
editor says "The screen accepts this layout", the gauge's own checker accepted it.

## Two versions in one

The selector at the top of the page switches between:

- **Standalone oil gauge** (the default): the five readings below, glowing seven-segment digits, lit-lamp arcs, scale marks in steps from zero, a colour for each alert level, and **Download for gauge** (upload it on the gauge's phone page).
- **Full CAN-bus screens**: the original editor for the full gauge - all of its readings (coolant, speed, tyres, gear...), built-in views, clock and image gauges, gear format, the brain's oil verdict, live data from the brain, and **Export to screen code**. It also has all the newer options above (glow, seven-segment digits, scale steps, alert zone colours, fixed-width Number gauges). Its preview is the full screens' own drawing code (`web/preview_can.wasm`), built from the updated engine in `engine-for-can-screens/`. **Your screens only draw those options once that `Gauge_Layout.cpp`/`.h` is copied into the screens' sketch and rebuilt** (see `engine-for-can-screens/README.txt`); Export warns if the sketch's engine is older.

The two don't share layouts: they read different data. Switching asks first and reopens the editor with the other version's own screens. Unsaved changes are kept in this browser for each version (switching back picks them up again), and saved files aren't touched. Files you save are tagged with the version that made them; **Open...** lists only the current version's files (and any older untagged ones), and opening a file made for the other version tells you to switch first.

To start on the CAN-bus version: `python3 editor.py --edition can`. If a page shows the wrong version of the editor, an older copy is probably still running on that port (the new one now says so and stops instead of quietly showing the old one).

The rest of this page is about the standalone gauge. The CAN-bus version is documented in `LAYOUT_FORMAT_CAN.md`; `install-service.sh` and `install-desktop.sh` set it up on a build server or as an app-menu entry.

## Run it

Needs Python 3 only. Everything stays on your computer.

```sh
./run.sh            # Linux / macOS: opens the editor in your browser
```

On **Windows 11**: install Python 3 from python.org (tick "Add python.exe to PATH"), unzip this folder, and double-click `run.bat` (or run `py editor.py` in a terminal). Your browser opens at `http://127.0.0.1:8370`. Closing the terminal window stops it.

Layouts you **Save** go in a `370z-layouts` folder in your user folder (`C:\Users\<you>\370z-layouts` on Windows).

## What you can show

The gauge has five readings, and a view can use any of them:

| Input | What it is | Range |
|---|---|---|
| `oil_psi` | Oil pressure | 0 - 120 psi |
| `oil_status` | Oil alarm state (1 ok, 2 warning, 3 low-oil alarm) | for lights |
| `boost_psi` | Boost | -15 to 30 psi |
| `afr` | Air/fuel ratio | 10 - 20 |
| `engine_running` | Engine has demonstrably run (the oil alarm is armed) | yes / no |

`boost_psi` and `afr` only have a reading if you fit those sensors (INA3221 channels);
otherwise the gauge shows "--" rather than inventing a number. Alert limits you set on a
gauge only change that gauge's colours. The audible low-oil alarm is the gauge's own, set
on its settings page, and works on every screen, designed or not.

Gauge types: arc, needle, bar, number, light, text, line. Readings can be drawn as glowing **seven-segment digits** (Reading style), and arcs and bars have a **lit-lamp look** (glow, bright centre, dim unlit blocks: the *Apply lit-lamp look* button). (Clock and image gauges are
not available on the standalone gauge.) The full list of options is in `LAYOUT_FORMAT.md`;
ignore its mentions of other inputs, built-in views and the sketch export, which belong
to a larger system.

The gauge is always green on black, so those preview controls are fixed. The **psi / bar**
box previews the gauge's units setting.

## On a phone or tablet

Start the editor with `--lan` on a computer (see below) and open its address on your phone's browser. On a narrow screen the page switches to a phone layout: views in a strip across the top, the preview stays on screen while you scroll the properties underneath, and the drag handles are larger for fingers. Tap a gauge to select it, drag it or its handles to move it, and use **Download for gauge** (the file lands in your phone's downloads, ready to upload on the gauge's page).

## Get a design onto the gauge

1. In the editor press **Download for gauge**. It checks the layout the way the gauge will and saves a small `.json` file.
2. Turn on the gauge's Wi-Fi (swipe up or down on the dial), join it, and open `192.168.4.1`.
3. Under **Designed screens**, choose the file and press **Upload layout**. The gauge checks it again and keeps the previous layout if it refuses the new one. No reflash is needed.
4. In **Screens**, pick `Designed: <name>` for any of the four slots (they can be mixed with the normal oil, boost, AFR and split screens) and press **Save settings**.

The layout is kept in the gauge's flash and survives power-off. **Remove** on the same
card deletes it. **Download the layout on the gauge** gives the file back, which you can
open in the editor with **Open...** or **...  > Import**.

## Limits

- At most 12 views and 16 gauges per view; the file must be under 64 KB.
- A view listed for another screen number only (the "On screens" boxes) is not offered on this gauge.
- A view that needs a reading you have not fitted shows "--" for it.
