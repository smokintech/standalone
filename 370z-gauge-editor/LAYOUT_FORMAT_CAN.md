# Gauge layout format — version 1

A layout file describes **custom views** for the round 480×480 screens. The
screens build them with `Gauge_Layout.cpp`; the editor reads and writes them.
Built-in views (oil, tyres, G-force, clock, …) are drawn by the screen code; the
file can list them to set the swipe order and switch them off (see Built-in views).

- One file holds every custom view: `layouts.json` (pushed through the brain),
  or compiled in as `Gauge_Layouts.h` (exported by the editor into the screen
  sketch).
- Coordinates are pixels on the 480×480 face, origin top-left, **x/y are the
  centre** of a gauge. The visible circle is radius 238 around (240, 240).
- Angles: **0 = 12 o'clock, clockwise**, degrees (90 = 3 o'clock).
- Values, ranges and alert levels are in the input's **base unit**
  (`Gauge_Inputs.h`): °C, psi, kPa, mph, V, %, rpm… Display units only change
  what's printed.
- Colours: a theme role — `gauge`, `text`, `dim`, `accent`, `track`, `tick`,
  `tickmaj`, `dial`, `warn`, `crit`, `ok` — or `"#rrggbb"`. Theme roles follow
  the screen's theme (accent colour, black/white face).
- Fonts: sizes 12, 14, 16, 20, 28, 48 (what the screen firmware has built in);
  any other number uses the nearest smaller one.
- Unknown keys are ignored, so newer editors can add fields older screens skip.
- Anything missing takes the default shown.

```json
{
  "format": "370z-gauge-layout",
  "version": 1,
  "views": [ VIEW, ... ],         // at most 12 custom views (+ built-in entries)
  "images": { NAME: IMAGE, ... }  // uploaded pictures for "image" gauges (optional)
}
```

`images` (editor only - the screens never parse it): `{"logo": {"w": 116, "h": 77,
"data": "<base64>"}}`, the pixels in the screens' own format, RGB565 (low byte
first) + 8-bit alpha, 3 bytes a pixel, w × h ≤ 480 × 480. Export writes the ones
a gauge uses into `Gauge_Layouts.h` as C arrays (`GL_LAYOUT_IMAGES`), and reads
them back from there. `car_370z` is built into the firmware and needs no entry.

## View

| Key | Default | Meaning |
|---|---|---|
| `id` | required | unique, `a-z 0-9 _`, ≤ 15 chars |
| `name` | id | shown in the editor and screen settings |
| `screens` | all | list of screen numbers (`GA_SCREEN_ID`) that offer this view |
| `face` | `true` | draw the standard round dial face (theme colours, red-oil flash works on it) |
| `bg` | - | the view's own background: a theme colour name (not `dial`) or `"#rrggbb"`. Colours the dial face (or the whole screen with `face: false`); anything drawn in `dial` colour in this view (block notches) uses it too. The red-oil flash still works and comes back to it. |
| `gauges` | `[]` | list of GAUGE, drawn in order (later ones on top), at most 16 |
| `enabled` | `true` | `false` = keep the view in the file but show it on no screen |
| `builtin` | — | makes this entry a **built-in view** instead (see below) |

## Built-in views

The screens' own views (oil, tyres, G-force, clock, …) can be listed too, so the
editor can set the swipe order and switch them off per screen:

```json
{ "id": "oil",   "builtin": "oil",   "screens": [1] },
{ "id": "clock", "builtin": "clock", "enabled": false }
```

- `builtin` is the view's name from `GA_VIEW_LIST` in `Gauge_Telemetry.h`, in
  lower case: `oil`, `oiltemp`, `oildual`, `volts`, `tpms`, `speed`, `gforce`,
  `clock`, `afr`, `boost`, `coolant`. The editor reads the list from the sketch.
- `gauges` and `face` are ignored; the screen code draws these views.
- **No built-in entries in the file**: every built-in view comes first, in
  `GA_VIEW_LIST` order, then the custom views. This is how files without them
  behave.
- **Any built-in entry in the file**: the `views` list is the swipe order on
  every screen (each screen skips what isn't on it). Built-in views that aren't
  listed are off.
- Alerts never jump to a view a screen doesn't show. Oil pressure goes to `oil`,
  or to `oildual` on a screen that shows that but not `oil`, otherwise it
  doesn't jump. Tyres go to `tpms` if shown. The red face flash on red oil
  follows its own setting whatever views are on.
- If nothing at all is left on a screen, it shows every built-in view rather
  than nothing.
- Up to 12 custom views plus the built-in entries (32 entries in all).

## Gauge — common keys

| Key | Default | Meaning |
|---|---|---|
| `type` | required | `arc`, `needle`, `bar`, `digital`, `light`, `text`, `line`, `clock`, `image` |
| `x`, `y` | 240, 240 | centre |
| `input` | required (not `text`, `line`, `clock`, `image`) | key from `Gauge_Inputs.h`, e.g. `coolant_c`, `oil_psi`, `tyre_fl` |
| `min`, `max` | input's default | scale range (base units) |
| `origin` | `min` | arc/bar fill starts here (e.g. 0 for a boost gauge from −15 to 30) |
| `decimals` | input's default | digits after the point |
| `by_unit` | none | a different scale when the screen shows a given unit: `{"bar": {"min": 0, "max": 8, "major": 4}}` (min/max in that unit, `major` = tick divisions). Otherwise the scale is just converted |
| `units` | `"auto"` | `auto`, or `c`/`f`, `psi`/`bar`/`kpa`, `mph`/`kmh`, `raw`. `auto` = the screen's own C/F and psi/bar settings for temperatures and oil pressure, base unit for everything else |
| `format` | `"number"` | `number`, or `gear` (−1 → R, 0 → N) |
| `colors` | see below | `{ "fill", "track", "text", "label", "needle", "nodata" }` - `nodata`: colour of "--" with no reading (default: the text colour). Also `scale` (scale numbers, `dim`), `ticks` (major ticks, `tickmaj`), `minor_ticks` (`tick`), `unit` (unit text, `dim`) - on a clock: numbers, hour and minute marks |
| `alerts` | none | see Alerts |
| `anim` | see Animation | |

`colors` defaults: fill `gauge`, track `track`, text `text`, label `dim`,
needle `crit`.

### `arc` — segmented or solid dial

| Key | Default | Meaning |
|---|---|---|
| `r` | 200 | radius to the **middle** of the band |
| `width` | 30 | band thickness |
| `start` | 225 | where the scale starts (angle) |
| `sweep` | 270 | how far it runs: positive = clockwise, negative = anticlockwise (e.g. a gauge round the bottom with its minimum on the left) |
| `segments` | 20 | 0 = solid band, else that many blocks |
| `ticks` | `{"major":6,"minor":2,"numbers":true,"font":20,"inset":6,"length":16}` | scale ticks inside the band: `major` divisions (0 = no ticks), `minor` steps per division, numbers on/off, gap from the band (`inset`, negative = reaching under the band), tick `length`, and `minor_length` (default 60 % of `length`) |
| `unlit_shade` | `0` | 0 = unlit blocks in the track colour. 0.05-0.6 = unlit blocks are a dim shade of the colour they'd be when lit (dim amber/red in the alert zones when `zones` is on) - like an analogue / LED gauge. Arcs and bars |
| `zone_fill` | `false` | lit blocks take their zone's colour (green, then amber, then red) instead of the whole fill turning amber/red on an alert. Arcs and bars |
| `ring` | none | a second, thin ring beside the band: `{"offset":-26,"width":10,"segments":0,"zones":true,"fill":false}` - `offset` from the middle of the band (negative = inside), `zones` shows the amber/red ranges, `fill` the reading. Arcs and needles |
| `rounded` | `false` | round ends on the band and on the reading (like an LVGL arc) |
| `gaps` | none | draw the blocks as notches over a solid band instead of separate blocks: `{"width":6,"outer":18,"inner":18,"color":"dial"}` - lines in that colour from `outer` px outside the band to `inner` px into it (how the built-in dials look) |
| `marks` | `[]` | reference lines across the band, up to 4: `{"value":0,"color":"text","width":4,"outer":16,"inner":46}` (px past the band's outer edge / in from it) |
| `zones` | `true` | colour the track where the alert levels are |
| `number` | `{"show":true,"font":48,"dx":0,"dy":0}` | the reading, offset from the centre |
| `label` | `{"text":<input label>,"font":20,"dx":0,"dy":70}` | `text` "" = none |
| `unit` | `{"show":true,"font":16,"dx":0,"dy":-50}` | |

In a label, `{u}` becomes the unit as shown (`°C`, `PSI`, `BAR`…) and `{U}` the
same without the degree sign: `"OIL  {U}"` → `OIL  PSI` / `OIL  BAR`.

### Status note and peak text (arc, needle, bar, digital)

```json
"note": { "font": 16, "dx": 0, "dy": 104,
          "none": "no wideband", "idle": "", "ok": "on load",
          "low_crit": "", "low_warn": "rich", "high_warn": "lean", "high_crit": "LEAN" },
"peak_label": { "text": "peak ", "font": 20, "dx": 0, "dy": 104, "start": 0 }
```

- `note` shows a word for the alert state, in the dim colour: `none` = no reading,
  `idle` = the alert's condition is off (`when` / `when_running`), `ok` = normal,
  then which level it's in. Up to 15 characters each.
- `peak_label` shows the highest reading since the screen started, after `text`,
  formatted like the reading. It's kept when you swipe away and back, and only
  shows while there's a reading. `start` = the lowest it can show (0 on boost,
  so vacuum never counts as a peak).

### `line`

`{"type":"line","x":90,"y":240,"x2":390,"y2":240,"width":2,"color":"tickmaj"}` - a
straight line from (x, y) to (x2, y2).

### `needle` — dial with a pointer

Same keys as `arc` (the band is the track only, `segments` default 0), plus
`needle`: `{"length":180,"width":6,"tail":20,"hub":14}`.

### `bar` — straight bar

| Key | Default | Meaning |
|---|---|---|
| `w`, `h` | 300, 36 | size |
| `vertical` | `false` | fills bottom-to-top if true |
| `segments` | 0 | 0 = solid |
| `number` | `{"show":true,"font":28,"dx":0,"dy":-50}` | |
| `label` | `{"text":<input label>,"font":16,"dx":0,"dy":40}` | |
| `unit` | `{"show":false,...}` | |

### `digital` — big number

| Key | Default |
|---|---|
| `number` | `{"show":true,"font":48,"dx":0,"dy":0}` |
| `label` | `{"text":<input label>,"font":20,"dx":0,"dy":50}` |
| `unit` | `{"show":true,"font":16,"dx":0,"dy":-40}` |

### `light` — warning light

| Key | Default | Meaning |
|---|---|---|
| `size` | 40 | diameter / side |
| `shape` | `"circle"` | or `"square"` |
| `on` | `{"op":">=","value":1}` | lit when *input op value*; ops `> >= < <= == !=` |
| `on_color` | `crit` | |
| `off_color` | `track` | |
| `blink` | `false` | blink while lit |
| `label` | `{"text":<input label>,"font":16,"dx":0,"dy":40}` | |

No reading → drawn as off, never lit.

The threshold is in the gauge's `units` (see above): with `"units":"bar"` an oil light `{"op":"<","value":1}` means below 1 bar; with `auto` it follows the screen's psi/bar setting, and with `psi` it is always psi. Needles are always drawn on top of lights, whatever the order in the file.


### `text` — fixed text

`{"type":"text","x":240,"y":400,"text":"370Z","font":20,"color":"dim"}`

### `clock` — the screen's clock (onboard RTC, set from the brain)

Defaults are the built-in clock view's look.

| Key | Default | Meaning |
|---|---|---|
| `style` | `"auto"` | `auto` (the screen's own analogue/digital CLOCK setting), `analog`, `digital` |
| `r` | 200 | analogue face radius |
| `ticks` | `{"inset":8,"length":26,"minor":1,"numbers":true,"font":20}` | hour marks `inset` px in from `r`, `length` long; `minor` 5 = a mark every minute; `numbers` 1-12 |
| `hands` | `{"hour":0.48,"minute":0.69,"second":0.76,"seconds":true}` | hand lengths × `r`; `seconds` false hides the second hand |
| `needle.hub` | 14 | centre dot size |
| `digit_height` | 132 | digital: seven-segment digits this tall (24 hour, HH:MM) |
| `thickness` | 15 × height/132 | digital: segment thickness |
| `number` | `{"dx":0,"dy":-10}` | digital: where the digits sit |
| `label` | `{"text":"{date}","font":20,"dy":96}` | the date: `{date}` "Sun 25/10/2026", `{time}` "14:05", `{day}` "Sun" |
| `date_on_analog` | false | also show the label on the analogue face |

Colours: `colors.text` hands and digits, `colors.needle` second hand and dot
(`accent`), `colors.label` the date (`dim`). With the RTC never set (year before
2024) it shows "--:--" and no hands.

### `image` — a picture

`{"type":"image","image":"car_370z","x":240,"y":234}`, drawn at its own size
centred on x/y. `image`: `car_370z` (built in, 116×168) or a name from the file's
`images`. `tint`: a colour to recolour it with (for one-colour icons).

## Alerts

```json
"alerts": { "low_crit": 15, "low_warn": 25, "high_warn": null, "high_crit": null,
            "when_running": true, "flash": true, "takeover": true,
            "number": true, "status": "oil_status",
            "when": { "input": "throttle_pct", "op": ">=", "value": 55 } }
```

- `number` (default `true`): turn the reading amber/red too. `false` = only the
  fill changes colour (as on the built-in oil, oil temperature and battery views).
- `status`: a status input (`oil_status`, the brain's oil verdict judged against
  rpm and oil temperature). While it has a reading, it sets the level (ok /
  amber / red); the four levels are only used when it doesn't.
- `when`: only judge while another reading meets a condition (ops `> >= < <= == !=`),
  e.g. AFR only under load. Outside it the level is normal and the note says `idle`.

- Any of the four levels may be `null`/missing. Red if `v <= low_crit` or
  `v >= high_crit`, amber if `v <= low_warn` or `v >= high_warn`.
- `when_running`: only judge with the engine turning (rpm ≥ 400) — for oil
  pressure, which is legitimately 0 when parked.
- Colour: in amber/red the fill and number turn `warn`/`crit` (always on when
  alerts are set). With `zones` the track shows the amber/red ranges.
- `flash`: in red for ≥ 0.5 s, the gauge blinks.
- `takeover`: in red for ≥ 1.5 s, the screen jumps to this view (like the
  built-in oil/tyre alerts; the built-in oil alert always wins, then tyres).
  A swipe away is honoured for 5 s.
- No reading → no alert, ever.

## Animation

```json
"anim": { "smooth": 0.5, "sweep": false, "peak": false, "peak_s": 3 }
```

- `smooth` 0–0.95: 0 = jumps straight to the value, higher = more damped
  (affects the arc/needle/bar, not the printed number).
- `sweep`: when the view opens, the arc/needle runs to full scale and back.
- `peak`: a marker holds the highest value for `peak_s` seconds.

## Limits

12 custom views (plus built-in entries, 32 in all), 16 gauges per view, 23-character texts. Each view is drawn as a
single LVGL object (all gauges are painted in one draw pass), so view size
has almost no effect on the screens' LVGL memory.


## Newer options (need the updated Gauge_Layout.cpp)

Glow and bright centre (`glow`, `core`), seven-segment digits (`number.style: "seg7"`), scale marks in steps from zero (`ticks.step`, `by_unit.<unit>.step`), own colours for alert levels (`alerts.colors`) and fixed-width Number gauges (`number.fixed`) are described in `LAYOUT_FORMAT.md`; they work the same on the full screens once `engine-for-can-screens/Gauge_Layout.cpp` is in the sketch.
