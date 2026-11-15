// model.js - what a layout means: defaults, units, number formatting, the
// screen palette and the gauge geometry the editor draws handles on.
// Everything here mirrors Gauge_Layout.cpp (setDefaults, unitOf, fmtValue,
// fontFor) so the editor shows the same values the screens use.

export const FONTS = [12, 14, 16, 20, 28, 48];
export const TYPES = ["arc", "needle", "bar", "digital", "light", "text", "line", "clock", "image"];
export const TYPE_NAMES = { arc: "Arc", needle: "Needle dial", bar: "Bar", digital: "Number", light: "Warning light", text: "Text", line: "Line", clock: "Clock", image: "Image" };
export const NO_INPUT = ["text", "line", "clock", "image"];      // gauges that read nothing

// Images "image" gauges can show: name -> {w, h} (the built-in car + the layout's uploads).
export const IMAGES = {};
export function setImageSizes(map) { for (const k of Object.keys(IMAGES)) delete IMAGES[k]; Object.assign(IMAGES, map); }
export const NOTE_STATES = [["none", "No reading"], ["idle", "Not judged (condition off)"], ["ok", "Normal"],
  ["low_crit", "Low red"], ["low_warn", "Low amber"], ["high_warn", "High amber"], ["high_crit", "High red"]];
export const ROLES = ["gauge", "text", "dim", "accent", "track", "tick", "tickmaj", "dial", "warn", "crit", "ok"];
export const ROLE_NAMES = { gauge: "Gauge (theme)", text: "Text (theme)", dim: "Dim grey", accent: "Accent", track: "Track",
  tick: "Tick", tickmaj: "Major tick", dial: "Dial face", warn: "Amber", crit: "Red", ok: "OK green" };
export const ACCENTS = [
  { name: "Green", rgb: 0x1db954 }, { name: "Amber", rgb: 0xf5a623 }, { name: "Red", rgb: 0xe2213b },
  { name: "Ice", rgb: 0x5ad2ff }, { name: "Orange", rgb: 0xff7a18 }, { name: "White", rgb: 0xffffff }, { name: "Black", rgb: 0x000000 }];
export const LIMITS = { views: 12, gauges: 16, text: 23, id: 15 };

export let INPUTS = [];          // from the firmware's own table (preview.wasm)
export let INPUT = {};           // key -> row (with .idx)
export function setInputs(list) {
  INPUTS = list; INPUT = {};
  list.forEach((r, i) => { r.idx = i; INPUT[r.key] = r; });
}
export const CAT_NAMES = { temp: "Temperatures", press: "Pressures", press_kpa: "Pressures", speed: "Speed", none: "Other" };

// Gauge.cpp themeApply()
export function palette(accent, dark) {
  const a = ACCENTS[accent].rgb;
  const gauge = (accent === 5 || accent === 6) ? (dark ? 0x1db954 : 0x13893a) : a;
  const p = { accent: a, text: a, gauge, ok: gauge, warn: 0xf5a623, crit: 0xe2213b };
  Object.assign(p, dark
    ? { dial: 0x0d0d0f, track: 0x1a1a1e, tick: 0x3a3a42, tickmaj: 0x6a6a76, dim: 0x8a8a94, bg: 0x000000 }
    : { dial: 0xf4f4f6, track: 0xd6d6dc, tick: 0xb2b2ba, tickmaj: 0x7a7a84, dim: 0x55555e, bg: 0xffffff });
  return p;
}
export const hex6 = n => "#" + n.toString(16).padStart(6, "0");
export function colorCss(c, pal) {
  if (typeof c === "string" && /^#[0-9a-fA-F]{6}$/.test(c)) return c;
  return hex6(pal[c] ?? pal.gauge);
}

// ----------------------------------------------------------- defaults ---
// setDefaults() in Gauge_Layout.cpp, as plain objects.
export let EDITION = "gauge";          // "gauge" = standalone oil gauge, "can" = the full CAN-bus screens
export function setEdition(e) { EDITION = e; }
export function defaults(type, inputKey) {
  const inp = INPUT[inputKey];
  const d = {
    type, x: 240, y: 240,
    min: inp ? inp.min : 0, max: inp ? inp.max : 100, origin: null,
    decimals: inp ? inp.dp : 0, units: "auto", format: EDITION === "can" && inp && inp.kind === "gear" ? "gear" : "number",
    colors: { fill: "gauge", track: "track", text: "text", label: "dim", needle: "crit", scale: "dim", ticks: "tickmaj", minor_ticks: "tick", unit: "dim" },
    alerts: { low_crit: null, low_warn: null, high_warn: null, high_crit: null, when_running: false, flash: false, takeover: false,
              number: true, when: null,
              ...(EDITION === "can" ? { status: "" } : {}),
              colors: { low_crit: "crit", low_warn: "warn", high_warn: "warn", high_crit: "crit" } },
    anim: { smooth: 0.5, sweep: false, peak: false, peak_s: 3 },
    r: 200, width: 30, start: 225, sweep: 270, segments: type === "arc" ? 20 : 0, zones: true, rounded: false, gaps: null, unlit_shade: 0, zone_fill: false, glow: 0, core: false, ring: null,
    ticks: { major: 6, minor: 2, numbers: true, font: 20, inset: 6, length: 16, minor_length: null, step: 0 },
    marks: [], by_unit: {}, x2: 340, y2: 240,
    note: { show: false, font: 16, dx: 0, dy: 104, none: "", idle: "", ok: "", low_crit: "", low_warn: "", high_warn: "", high_crit: "" },
    peak_label: null,
    needle: { length: 180, width: 6, tail: 20, hub: 14 },
    w: 300, h: 36, vertical: false,
    size: 40, shape: "circle", on: { op: ">=", value: 1 }, on_color: "crit", off_color: "track", blink: false,
    number: { show: true, font: 48, dx: 0, dy: 0, style: "", height: 64, digits: 0, slant: 8, thickness: 0, ghost: 0.12, glow: 0.7, core: true, fixed: true },
    label: { show: !!(inp && inp.label), text: inp ? inp.label : "", font: 20, dx: 0, dy: 70 },
    unit: { show: true, font: 16, dx: 0, dy: -50 },
    text: "", font: 20, color: "dim",
  };
  if (type === "needle") d.width = 12;
  if (type === "bar") { d.number = { ...d.number, show: true, font: 28, dx: 0, dy: -50 }; d.label.font = 16; d.label.dy = 40; d.unit.show = false; }
  if (type === "digital") { d.label.dy = 50; d.unit.dy = -40; }
  if (type === "light") { d.label.font = 16; d.label.dy = 40; d.number.show = false; d.unit.show = false; }
  if (type === "text") { d.number.show = false; d.unit.show = false; d.label = { show: true, text: "", font: 20, dx: 0, dy: 0 }; }
  if (type === "line") { d.number.show = false; d.unit.show = false; d.label.show = false; d.width = 2; d.color = "tickmaj"; }
  if (type === "clock") {         // Gauge_Layout.cpp setDefaults: the built-in clock's look
    d.number = { show: true, font: 48, dx: 0, dy: -10 }; d.unit.show = false;
    d.label = { show: true, text: "{date}", font: 20, dx: 0, dy: 96 };
    d.ticks = { major: 12, minor: 1, numbers: true, font: 20, inset: 8, length: 26, minor_length: null };
    d.colors.needle = "accent"; d.needle.hub = 14;
    Object.assign(d, { style: "auto", hands: { hour: 0.48, minute: 0.69, second: 0.76, seconds: true }, digit_height: 132, thickness: null, date_on_analog: false });
  }
  if (type === "image") { d.number.show = false; d.unit.show = false; d.label.show = false; Object.assign(d, { image: "car_370z", tint: null }); }
  return d;
}

const isObj = v => v && typeof v === "object" && !Array.isArray(v);
// The gauge as the screen will see it: stored keys over the defaults.
export function eff(g) {
  const d = defaults(g.type, g.input);
  const out = structuredClone(d);
  for (const [k, v] of Object.entries(g)) {
    if (isObj(v) && isObj(out[k])) Object.assign(out[k], v);
    else out[k] = v;
  }
  if (g.alerts && isObj(g.alerts.colors)) out.alerts.colors = { ...d.alerts.colors, ...g.alerts.colors };   // one level deeper than the merge above
  for (const p of ["number", "label", "unit"]) {        // parsePart: "text" sets show
    if (g[p] && typeof g[p].text === "string") out[p].show = g[p].text !== "";
  }
  if (g.type === "text") out.label = { show: true, text: out.text, font: out.font, dx: 0, dy: 0 };
  if (g.note) out.note.show = g.note.show ?? M_NOTE_KEYS.some(k => g.note[k]);
  if (g.ring) out.ring = { show: true, offset: -26, width: 10, segments: 0, zones: true, fill: false, ...g.ring };
  if (g.peak_label) out.peak_label = { show: true, text: "peak ", font: 20, dx: 0, dy: 104, start: null, ...g.peak_label };
  if (g.type === "line" && g.x2 === undefined) out.x2 = out.x + 100;
  if (g.type === "line" && g.y2 === undefined) out.y2 = out.y;
  if (g.type === "clock" && out.thickness == null) out.thickness = Math.round(15 * out.digit_height / 132);
  return out;
}

const M_NOTE_KEYS = ["none", "idle", "ok", "low_crit", "low_warn", "high_warn", "high_crit"];

// Read / write a dotted key ("alerts.high_warn") on the stored gauge.
export function getp(o, path) { return path.split(".").reduce((a, k) => (a == null ? undefined : a[k]), o); }
export function setp(o, path, v) {
  const ks = path.split("."); let a = o;
  // walk (and create) the containers on the way; an existing ARRAY (marks) must stay an array - "marks.0.value" used to turn it into {}
  for (let i = 0; i < ks.length - 1; i++) {
    const k = ks[i];
    if (!(a[k] && typeof a[k] === "object")) a[k] = /^\d+$/.test(ks[i + 1]) ? [] : {};
    a = a[k];
  }
  if (v === undefined) delete a[ks.at(-1)]; else a[ks.at(-1)] = v;
  // drop emptied objects
  if (ks.length > 1 && isObj(a) && !Object.keys(a).length) setp(o, ks.slice(0, -1).join("."), undefined);
}

// ------------------------------------------------------------- units ---
export function unitOf(e, units) {
  const inp = INPUT[e.input]; if (!inp) return "raw";
  const u = e.units;
  switch (inp.cat) {
    case "temp": if (u === "c" || u === "f") return u; return u === "auto" && units.temp_f ? "f" : "c";
    case "press":
      if (["psi", "bar", "kpa"].includes(u)) return u;
      if (u === "auto" && units.oil_bar && ["oil_psi", "oil_psi_direct", "oil_psi_ecutek"].includes(e.input)) return "bar";
      return "psi";
    case "press_kpa": return ["psi", "bar", "kpa"].includes(u) ? u : "kpa";
    case "speed": return u === "kmh" ? "kmh" : "mph";
    default: return "raw";
  }
}
export function unitChoices(inputKey) {
  const c = INPUT[inputKey]?.cat;
  if (c === "temp") return ["auto", "c", "f"];
  if (c === "press" || c === "press_kpa") return ["auto", "psi", "bar", "kpa"];
  if (c === "speed") return ["auto", "mph", "kmh"];
  return ["auto"];
}
export const UNIT_NAMES = { auto: "Screen setting", c: "°C", f: "°F", psi: "psi", bar: "bar", kpa: "kPa", mph: "mph", kmh: "km/h", raw: "as is" };
export function toDisp(e, v, units) {
  const inp = INPUT[e.input]; if (!inp) return v;
  const u = unitOf(e, units);
  switch (inp.cat) {
    case "temp": return u === "f" ? v * 9 / 5 + 32 : v;
    case "press": return u === "bar" ? v / 14.5038 : u === "kpa" ? v * 6.89476 : v;
    case "press_kpa": return u === "psi" ? v * 0.145038 : u === "bar" ? v / 100 : v;
    case "speed": return u === "kmh" ? v * 1.609344 : v;
    default: return v;
  }
}
// display value -> base value (for alert boxes typed in display units)
export function fromDisp(e, v, units) {
  const inp = INPUT[e.input]; if (!inp) return v;
  const u = unitOf(e, units);
  switch (inp.cat) {
    case "temp": return u === "f" ? (v - 32) * 5 / 9 : v;
    case "press": return u === "bar" ? v * 14.5038 : u === "kpa" ? v / 6.89476 : v;
    case "press_kpa": return u === "psi" ? v / 0.145038 : u === "bar" ? v * 100 : v;
    case "speed": return u === "kmh" ? v / 1.609344 : v;
    default: return v;
  }
}
export function unitLabel(e, units) {
  const inp = INPUT[e.input]; if (!inp) return "";
  return { c: "°C", f: "°F", psi: "PSI", bar: "BAR", kpa: "kPa", mph: "MPH", kmh: "KM/H" }[unitOf(e, units)] ?? inp.unit;
}
// "{u}" -> unit as shown ("°C"), "{U}" -> without the degree sign ("C")
export function labelOf(e, units) {
  const t = e.label.text || "";
  if (!/\{[uU]\}/.test(t)) return t;
  const u = unitLabel(e, units);
  return t.replace("{u}", u).replace("{U}", u.replace(/^°/, ""));
}
// The scale in use for the screen's units (a by_unit entry if one matches), base units.
export function scaleOf(e, units) {
  const sc = (e.by_unit || {})[unitOf(e, units)];
  if (sc && Number.isFinite(sc.min) && Number.isFinite(sc.max) && sc.max > sc.min)
    return { min: fromDisp(e, sc.min, units), max: fromDisp(e, sc.max, units), major: sc.major || e.ticks.major, own: true };
  return { min: e.min, max: e.max, major: e.ticks.major, own: false };
}
export function decimalsOf(e, units) {
  let d = e.decimals;
  if (INPUT[e.input]?.cat === "press" && unitOf(e, units) === "bar" && d === 0) d = 1;
  return d;
}
export function fmtValue(e, v, units) {
  if (!Number.isFinite(v)) return "--";
  if (e.format === "gear") { const g = Math.round(v); return g < 0 ? "R" : g === 0 ? "N" : String(g); }
  let s = toDisp(e, v, units).toFixed(decimalsOf(e, units));
  if (/^-0(\.0+)?$/.test(s)) s = s.slice(1);
  return s;
}

// ---------------------------------------------------------- geometry ---
export const norm360 = a => ((a % 360) + 360) % 360;
// Our angle convention: 0 = 12 o'clock, clockwise.
export function polar(cx, cy, a, r) { const t = a * Math.PI / 180; return [cx + r * Math.sin(t), cy - r * Math.cos(t)]; }
export function angleOf(cx, cy, x, y) { return norm360(Math.atan2(x - cx, -(y - cy)) * 180 / Math.PI); }
// Is angle a on the scale of a dial (start, sweep may be negative)?
export function onSweep(e, a) {
  if (Math.abs(e.sweep) >= 360) return true;
  const d = e.sweep >= 0 ? norm360(a - e.start) : norm360(e.start - a);
  return d <= Math.abs(e.sweep);
}
// SVG path of a band from angle a0 to a1 (a0 < a1 in our convention).
export function bandPath(cx, cy, r, w, a0, a1) {
  if (a1 - a0 >= 359.9) a1 = a0 + 359.9;
  const ro = r + w / 2, ri = Math.max(1, r - w / 2), big = a1 - a0 > 180 ? 1 : 0;
  const [x0, y0] = polar(cx, cy, a0, ro), [x1, y1] = polar(cx, cy, a1, ro);
  const [x2, y2] = polar(cx, cy, a1, ri), [x3, y3] = polar(cx, cy, a0, ri);
  return `M${x0} ${y0}A${ro} ${ro} 0 ${big} 1 ${x1} ${y1}L${x2} ${y2}A${ri} ${ri} 0 ${big} 0 ${x3} ${y3}Z`;
}
export function sweepRange(e) { return e.sweep >= 0 ? [e.start, e.start + e.sweep] : [e.start + e.sweep, e.start]; }

// Text boxes (number / label / unit) of a gauge, using real LVGL font metrics.
export function partBoxes(e, measure, units, value) {
  const out = [];
  const add = (part, text, font) => {
    const p = e[part];
    if (!text) return;
    const [w, h] = measure(text, font);
    out.push({ part, x: e.x + p.dx, y: e.y + p.dy, w: Math.max(w, 10), h: Math.max(h, 10), text });
  };
  if (e.type === "text") { const [w, h] = measure(e.text || " ", e.font); return [{ part: "body", x: e.x, y: e.y, w: Math.max(w, 16), h: Math.max(h, 12) }]; }
  if (e.type === "line" || e.type === "image") return [];
  if (e.type === "clock") {
    const an = clockAnalog(e, units);
    if (!an) { const k = e.digit_height / 132; out.push({ part: "number", x: e.x + e.number.dx, y: e.y + e.number.dy, w: Math.round(354 * k), h: e.digit_height, text: "88:88" }); }
    if (e.label.show && (!an || e.date_on_analog)) add("label", clockText(e.label.text, new Date()), e.label.font);
    return out;
  }
  if (e.number.show && e.type !== "light") add("number", fmtValue(e, value, units), e.number.font);
  if (e.unit.show) add("unit", unitLabel(e, units), e.unit.font);
  if (e.label.show && e.label.text) add("label", labelOf(e, units), e.label.font);
  if (e.note && e.note.show) {
    const longest = M_NOTE_KEYS.map(k => e.note[k] || "").sort((a, b) => b.length - a.length)[0];
    if (longest) add("note", longest, e.note.font);
  }
  if (e.peak_label && e.peak_label.show !== false) add("peak_label", (e.peak_label.text || "") + fmtValue(e, Number.isFinite(value) ? value : e.max, units), e.peak_label.font);
  return out;
}

// Clocks on "auto" follow the screen's analogue/digital setting.
export const clockAnalog = (e, units) => e.style === "analog" || (e.style !== "digital" && units.clock_analog !== false);
const DOW = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
const p2 = n => String(n).padStart(2, "0");
export function clockText(t, d) {
  return String(t || "").replace("{date}", `${DOW[d.getDay()]} ${p2(d.getDate())}/${p2(d.getMonth() + 1)}/${d.getFullYear()}`)
    .replace("{time}", `${p2(d.getHours())}:${p2(d.getMinutes())}`).replace("{day}", DOW[d.getDay()]);
}
export function imageSize(name) { return IMAGES[name] || null; }

// What the "body" of a gauge covers, for hit testing and the selection outline.
export function bodyShape(e, units = {}) {
  switch (e.type) {
    case "clock": return clockAnalog(e, units) ? { kind: "circle", x: e.x, y: e.y, r: e.r } : null;
    case "image": { const s = imageSize(e.image) || [40, 40]; return { kind: "rect", x: e.x - Math.floor(s[0] / 2), y: e.y - Math.floor(s[1] / 2), w: s[0], h: s[1] }; }
    case "arc": case "needle": return { kind: "dial", x: e.x, y: e.y, r: e.r, w: e.width };
    case "bar": return { kind: "rect", x: e.x - e.w / 2, y: e.y - e.h / 2, w: e.w, h: e.h };
    case "light": return { kind: "rect", x: e.x - e.size / 2, y: e.y - e.size / 2, w: e.size, h: e.size, round: e.shape !== "square" };
    case "line": return { kind: "line", x: e.x, y: e.y, x2: e.x2, y2: e.y2, w: e.width };
    default: return null;                       // digital / text: their text boxes are the body
  }
}

// The inputs a view reads (so the data panel can offer them).
export function inputsOfView(view) {
  const s = new Set();
  for (const g of view.gauges || []) {
    if (g.input) s.add(g.input);
    if (g.alerts && g.alerts.when_running) s.add("rpm");
    if (g.alerts && g.alerts.when && g.alerts.when.input) s.add(g.alerts.when.input);   // e.g. throttle for AFR
    if (g.alerts && g.alerts.status) s.add(g.alerts.status);                         // the brain's oil verdict
  }
  return [...s].filter(k => INPUT[k]);
}

export function slugId(name, taken) {
  let base = (name || "view").toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_+|_+$/g, "").slice(0, LIMITS.id) || "view";
  let id = base, n = 2;
  while (taken.includes(id)) { const suf = "_" + n++; id = base.slice(0, LIMITS.id - suf.length) + suf; }
  return id;
}
