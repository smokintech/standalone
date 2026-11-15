// data.js - where the preview's readings come from: demo motion, hand-set
// values, the brain (live, through editor.py) or a recording (_decoded.csv
// from the brain app's Record tab). Values are turned into the engine's kv[]
// exactly as the screens do it (Gauge_Telemetry.cpp).
import { INPUTS, INPUT } from "./model.js";

// one value from the brain's JSON (flattened "a.b" keys) -> kv number
function conv(inp, v) {
  if (v === undefined || v === null || v === "") return NaN;
  switch (inp.kind) {
    case "bool":
      if (v === true || v === "true") return 1;
      if (v === false || v === "false") return 0;
      return Number.isFinite(+v) ? (+v !== 0 ? 1 : 0) : NaN;
    case "lights": return v === "head" ? 2 : v === "side" ? 1 : v === "off" ? 0 : NaN;
    case "oil_status": return v === "crit" ? 3 : v === "warn" ? 2 : v === "ok" ? 1 : NaN;
    default: { const n = typeof v === "number" ? v : Number(v); return Number.isFinite(n) ? n : NaN; }
  }
}
export function kvFromFlat(flat, kv) {
  INPUTS.forEach((inp, i) => {
    const key = inp.path[0] === "@" ? inp.path.slice(1) : inp.path;   // @g_lat: the screen's own IMU; use the logged one if any
    kv[i] = conv(inp, flat[key]);
  });
}
export function flatten(o, pre = "", out = {}) {
  for (const [k, v] of Object.entries(o || {})) {
    const key = pre ? pre + "." + k : k;
    if (v !== null && typeof v === "object" && !Array.isArray(v)) flatten(v, key, out); else out[key] = v;
  }
  return out;
}

// --------------------------------------------------------------- demo ---
// Smooth made-up motion within each input's range, so animation and alert
// colours can be seen without a car. Clearly labelled "demo" in the UI.
export function demoValues(t, ranges, kv) {
  kv.fill(NaN);
  const s = t / 1000;
  INPUTS.forEach((inp, i) => {
    const r = ranges[inp.key]; if (!r) return;
    const ph = i * 1.7, w = 0.35 + (i % 5) * 0.07;
    const f = 0.5 + 0.5 * Math.sin(s * w + ph) * Math.min(1, 0.6 + 0.4 * Math.sin(s * 0.13 + ph));
    switch (inp.kind) {
      case "bool": kv[i] = Math.sin(s * 0.7 + ph) > 0.3 ? 1 : 0; break;
      case "gear": kv[i] = Math.round(-1 + 7 * f); break;
      case "lights": kv[i] = Math.floor(3 * f) % 3; break;
      case "oil_status": kv[i] = 1 + (Math.floor(3 * f) % 3); break;
      default: kv[i] = r[0] + (r[1] - r[0]) * f;
    }
  });
  if (ranges.rpm === undefined && INPUT.rpm) kv[INPUT.rpm.idx] = 900 + 2500 * (0.5 + 0.5 * Math.sin(s * 0.5));
}

// --------------------------------------------------------------- csv ----
function splitCsvLine(l) {
  const out = []; let cur = "", q = false;
  for (let i = 0; i < l.length; i++) {
    const c = l[i];
    if (q) { if (c === '"') { if (l[i + 1] === '"') { cur += '"'; i++; } else q = false; } else cur += c; }
    else if (c === '"') q = true;
    else if (c === ",") { out.push(cur); cur = ""; }
    else cur += c;
  }
  out.push(cur);
  return out;
}
// -> { t: [seconds from start], kv: [Float32Array], marks: [{t, text}], cols, span, name }
export function parseRecording(text, name) {
  const lines = text.split(/\r?\n/).filter(l => l.length);
  if (lines.length < 2) throw new Error("no rows in that file");
  const head = splitCsvLine(lines[0]);
  const ei = head.indexOf("epoch");
  if (ei < 0) throw new Error("not a brain recording (no 'epoch' column) - use the _decoded.csv file");
  const mi = head.indexOf("mark");
  const t = [], kvs = [], marks = [];
  let t0 = null;
  for (let n = 1; n < lines.length; n++) {
    const c = splitCsvLine(lines[n]);
    const ep = Number(c[ei]); if (!Number.isFinite(ep)) continue;
    if (t0 === null) t0 = ep;
    const flat = {};
    head.forEach((h, k) => { if (c[k] !== undefined && c[k] !== "") flat[h] = c[k]; });
    const kv = new Float32Array(INPUTS.length); kvFromFlat(flat, kv);
    t.push(ep - t0); kvs.push(kv);
    if (mi >= 0 && c[mi]) marks.push({ t: ep - t0, text: c[mi] });
  }
  if (!t.length) throw new Error("no rows with a time in that file");
  const present = INPUTS.filter((inp, i) => kvs.some(kv => Number.isFinite(kv[i]))).map(i => i.key);
  return { t, kv: kvs, marks, present, span: t[t.length - 1], name };
}
export function recordingAt(rec, s) {     // the row in force at s seconds
  let lo = 0, hi = rec.t.length - 1;
  if (s <= rec.t[0]) return rec.kv[0];
  while (lo < hi) { const m = (lo + hi + 1) >> 1; if (rec.t[m] <= s) lo = m; else hi = m - 1; }
  return rec.kv[lo];
}
