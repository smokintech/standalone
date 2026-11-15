// editor.js - the 370Z gauge layout editor.
import * as M from "./model.js";
import { Preview } from "./preview.js";
import * as D from "./data.js";
import { convertBuiltin, CONVERTIBLE, NEEDS_MEDIA } from "./convert.js";

const $ = s => document.querySelector(s);
const PV = new Preview();
const EDITION_KEY = "370z-layout-editor.edition";
const PREFS_BASE = "370z-layout-editor.prefs";
const prefsKey = () => PREFS_BASE + (CAN ? ".can" : "");
// Two versions of the editor in one: "gauge" = the standalone oil gauge, "can" = the full CAN-bus screens.
// Each has its own preview engine, readings and features, and keeps its own unsaved work in this browser.
let ED = "gauge";
let CAN = false;
const EDITION_NAMES = { gauge: "the standalone oil gauge", can: "the full CAN-bus screens" };
const autosaveKey = () => "370z-layout-editor.autosave" + (CAN ? ".can" : "");

const S = {
  doc: { format: "370z-gauge-layout", version: 1, views: [] },
  file: null, dirty: false,
  vi: 0, gi: -1,
  hist: [], hi: -1,
  theme: { accent: 0, dark: 1, temp_f: 0, oil_bar: 0, screen: 1, clock: 1 },
  src: "demo", manual: {}, live: { on: false, ok: false, err: "", age: null, flat: null, timer: 0 },
  rec: null, play: { on: false, pos: 0, speed: 1, last: 0, loop: true },
  kv: null, snap: true, clip: null, err: null,
  drag: null, hover: -1, guides: [], dragNote: null,
  settings: null, openSec: {},
  builtins: { ok: false, views: [], supported: false, error: "not read yet" },   // from the screen code
  styles: [],                                   // favourite looks (editor.py keeps them)
};

// ================================================================ helpers ===
function h(tag, attrs, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k === "class") e.className = v;
    else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
    else if (k === "style") e.style.cssText = v;
    else if (v === true) e.setAttribute(k, "");
    else e.setAttribute(k, v);
  }
  for (const c of kids.flat()) if (c !== null && c !== undefined && c !== false) e.append(c instanceof Node ? c : document.createTextNode(String(c)));
  return e;
}
const round = (v, dp = 3) => Number.isFinite(v) ? Math.round(v * 10 ** dp) / 10 ** dp : v;
const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
let toastT = 0;
function toast(msg, bad = false, ms = 4200) {
  const t = $("#toast"); t.innerHTML = msg; t.className = bad ? "bad" : ""; t.hidden = false;
  clearTimeout(toastT); toastT = setTimeout(() => t.hidden = true, ms);
}
const asDoc = d => typeof d === "string" ? JSON.parse(d) : d;
async function api(path, body) {
  const r = await fetch(path, body === undefined ? {} : { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  const ct = r.headers.get("Content-Type") || "";
  const out = ct.includes("json") ? await r.json() : await r.text();
  if (!r.ok) throw new Error((out && out.error) || r.statusText);
  return out;
}
const view = () => S.doc.views[S.vi];
const isBuiltin = v => !!(v && v.builtin);
const customCount = () => S.doc.views.filter(v => !v.builtin).length;
const builtinInfo = key => S.builtins.views.find(b => b.key === key);
const cap = t => t ? t[0].toUpperCase() + t.slice(1) : t;
const gauge = () => (view() && S.gi >= 0) ? view().gauges[S.gi] : null;
const units = () => ({ temp_f: !!S.theme.temp_f, oil_bar: !!S.theme.oil_bar, clock_analog: S.theme.clock !== 0 });
const pal = () => M.palette(S.theme.accent, S.theme.dark);
const now = () => Math.floor(performance.now());

// ================================================================ document ===
function normalise(doc) {
  if (!doc || doc.format !== "370z-gauge-layout") throw new Error("not a 370Z gauge layout file");
  if (doc.version !== 1) throw new Error("layout version " + doc.version + " - this editor writes version 1");
  if (doc.edition && doc.edition !== ED)
    throw new Error(`this layout was made for ${EDITION_NAMES[doc.edition] || doc.edition}. Switch the editor to that version first (the selector at the top).`);
  delete doc.edition;                       // only used to tell the two versions' files apart
  doc.views = Array.isArray(doc.views) ? doc.views : [];
  for (const v of doc.views) v.gauges = Array.isArray(v.gauges) ? v.gauges : [];
  if (!CAN) for (const v of doc.views) for (const g of v.gauges) {      // options the standalone gauge doesn't have (no CAN bus)
    delete g.format;
    if (g.alerts) delete g.alerts.status;
  }
  return doc;
}
function setDoc(doc, file, { dirty = false, keepHist = false } = {}) {
  S.doc = normalise(doc); S.file = file; S.vi = 0; S.gi = -1;
  if (!keepHist) { S.hist = []; S.hi = -1; }
  pushHist(); S.dirty = dirty;
  compile(true); renderAll();
}
function pushHist() {
  const s = JSON.stringify(S.doc);
  if (S.hist[S.hi] && S.hist[S.hi].doc === s) return false;
  S.hist = S.hist.slice(0, S.hi + 1);
  S.hist.push({ doc: s, vi: S.vi, gi: S.gi });
  if (S.hist.length > 300) S.hist.shift();
  S.hi = S.hist.length - 1;
  return true;
}
function undoRedo(d) {
  const n = S.hi + d; if (n < 0 || n >= S.hist.length) return;
  S.hi = n; const st = S.hist[n];
  S.doc = JSON.parse(st.doc); S.vi = Math.min(st.vi, S.doc.views.length - 1); S.gi = st.gi;
  if (!view() || S.gi >= view().gauges.length) S.gi = -1;
  S.dirty = true; compile(false); renderAll(); autosave();
}
function autosave() {
  try { localStorage.setItem(autosaveKey(), JSON.stringify({ doc: S.doc, file: S.file, dirty: S.dirty, at: Date.now() })); } catch (e) { /* storage off: fine */ }
}
function savePrefs() {
  try { localStorage.setItem(prefsKey(), JSON.stringify({ theme: S.theme, snap: S.snap, src: S.src, openSec: S.openSec })); } catch (e) { /* ignore */ }
}

// Something changed. commit = an undo step (end of a drag, a field left, a click).
function changed({ commit = true, props = true, fresh = false } = {}) {
  compile(fresh);
  if (commit) { if (pushHist()) { S.dirty = true; autosave(); } }
  renderViews(); renderTitle();
  if (props) renderProps(); else syncProps();
  renderOverlay();
  if (S.src === "manual") renderDataBody();
}

// Hand the layout to the screen's own engine. fresh = play the opening sweep.
function compile(fresh) {
  if (!S.kv) return;
  syncImages();
  const err = PV.load(JSON.stringify({ ...S.doc, images: undefined }));   // the screen never sees the image data
  S.err = err;
  showCurrent(fresh);
  renderStatus();
}
function showCurrent(fresh) {
  PV.theme(S.theme.accent, S.theme.dark, S.theme.temp_f, S.theme.oil_bar);
  const t = now();
  if (view() && !S.err) PV.show(S.vi, fresh ? t : t - 1600);   // not fresh: as if open for a while (no sweep)
  else if (!view()) PV.show(-1, t);
  frame(true);
}

// ================================================================ preview loop ===
const ctx = $("#screen").getContext("2d");
const img = ctx.createImageData(480, 480);
let lastFrame = 0, lastSlow = 0;
function readValues(t) {
  const kv = S.kv;
  switch (S.src) {
    case "demo": D.demoValues(t, demoRanges(), kv); break;
    case "manual":
      kv.fill(NaN);
      for (const [k, v] of Object.entries(S.manual)) if (M.INPUT[k]) kv[M.INPUT[k].idx] = v;
      break;
    case "live":
      if (S.live.flat && S.live.ok) D.kvFromFlat(S.live.flat, kv); else kv.fill(NaN);
      break;
    case "replay":
      if (S.rec) {
        if (S.play.on) {
          S.play.pos += (t - S.play.last) / 1000 * S.play.speed;
          if (S.play.pos > S.rec.span) { if (S.play.loop) S.play.pos = 0; else { S.play.pos = S.rec.span; S.play.on = false; renderDataBody(); } }
        }
        S.play.last = t;
        kv.set(D.recordingAt(S.rec, S.play.pos));
      } else kv.fill(NaN);
      break;
    default: kv.fill(NaN);
  }
}
function demoRanges() {     // each input moves across the range of the first gauge that shows it
  const r = {};
  for (const v of S.doc.views) for (const g of v.gauges) {
    for (const k of [g.alerts?.when?.input, g.alerts?.status]) {      // inputs the alerts look at
      if (k && M.INPUT[k] && !r[k]) r[k] = [M.INPUT[k].min, M.INPUT[k].max];
    }
    if (!g.input || r[g.input] || !M.INPUT[g.input]) continue;
    const e = M.eff(g);
    const lo = Math.min(e.min, e.max), hi = Math.max(e.min, e.max);
    r[g.input] = [lo - (hi - lo) * 0.03, hi + (hi - lo) * 0.03];
  }
  return r;
}
function frame(force) {
  const t = now();
  if (!force && t - lastFrame < 30) return;
  lastFrame = t;
  readValues(t);
  PV.setValues(S.kv);
  PV.clock(new Date(), S.theme.clock !== 0);
  PV.step(t);
  PV.paint(ctx, img);
  if (t - lastSlow > 250 || force) { lastSlow = t; slowTick(); }
}
function loop() { frame(false); requestAnimationFrame(loop); }
function slowTick() {
  // what the alerts would do on the chosen screen
  const tv = PV.takeover(S.theme.screen);
  const note = $("#alertNote");
  if (tv >= 0 && S.doc.views[tv]) note.textContent = `⚠ Alert takeover: screen ${S.theme.screen} would jump to “${S.doc.views[tv].name || S.doc.views[tv].id}”`;
  else note.textContent = "";
  if (S.src === "replay" && S.rec) {
    const sl = $("#recPos"); if (sl && !sl.matches(":active")) sl.value = S.play.pos;
    const tl = $("#recTime"); if (tl) tl.textContent = fmtTime(S.play.pos) + " / " + fmtTime(S.rec.span);
  }
  if (S.src === "demo" || S.src === "live" || S.src === "replay") renderOverlay();   // number boxes follow the text width
}
const fmtTime = s => { s = Math.max(0, s); const m = Math.floor(s / 60); return m + ":" + String(Math.floor(s % 60)).padStart(2, "0"); };

// ================================================================ title / status ===
function renderTitle() {
  $("#fileName").textContent = S.file || "untitled (not saved yet)";
  $("#dirty").className = S.dirty ? "on" : "";
  document.title = (S.dirty ? "• " : "") + (S.file || "untitled") + " - 370Z Layout Editor";
  $("[data-act=undo]").disabled = S.hi <= 0;
  $("[data-act=redo]").disabled = S.hi >= S.hist.length - 1;
}
function renderStatus() {
  const v = $("#valid");
  if (S.err) { v.className = "bad"; v.textContent = "✗ The screen would reject this layout: " + S.err; }
  else {
    v.className = "ok";
    const nv = customCount(), ng = view() ? view().gauges.length : 0;
    v.textContent = `✓ The screen accepts this layout · ${nv} of ${M.LIMITS.views} custom views` +
      (view() && !isBuiltin(view()) ? ` · this view ${ng} of ${M.LIMITS.gauges} gauges` : "");
  }
}

// ================================================================ views list ===
function screensText(v) { return v.screens && v.screens.length ? "screen " + v.screens.join(", ") : "all screens"; }
function renderViews() {
  const ol = $("#viewList"); ol.innerHTML = "";
  $("#viewCount").textContent = `${customCount()}/${M.LIMITS.views}`;
  S.doc.views.forEach((v, i) => {
    const off = v.enabled === false;
    const bi = isBuiltin(v), info = bi ? builtinInfo(v.builtin) : null;
    const where = off ? "off on every screen" : screensText(v);
    ol.append(h("li", { class: (i === S.vi ? "sel" : "") + (off ? " off" : "") + (bi ? " builtin" : ""), onclick: () => selectView(i) },
      h("div", { class: "vn" }, bi ? h("span", { class: "btag" }, "BUILT-IN") : null, v.name || (info && cap(info.name)) || v.id),
      h("div", { class: "vm" }, bi ? where : `${(v.gauges || []).length} gauge${(v.gauges || []).length === 1 ? "" : "s"} · ${where}`,
        !bi && v.face === false ? " · no face" : "",
        bi && S.builtins.ok && !info ? h("span", { class: "err" }, " · not in the screen code") : null)));
  });
  $("#emptyMsg").hidden = S.doc.views.length > 0;
  $("[data-act=addview]").disabled = customCount() >= M.LIMITS.views;
  document.querySelectorAll("[data-add]").forEach(b => b.disabled = isBuiltin(view()));
  renderBuiltinBanner();
}
// Offer to bring the built-in views into the layout (read from the screen code
// on the server), so they can be switched off per screen and reordered.
function renderBuiltinBanner() {
  const box = $("#builtinBanner"); box.innerHTML = "";
  const B = S.builtins;
  if (!B.ok) { if (B.error) box.append(h("div", { class: "note" }, "Built-in views: " + B.error)); return; }
  const listed = new Set(S.doc.views.filter(isBuiltin).map(v => v.builtin));
  const missing = B.views.filter(b => !listed.has(b.key));
  if (!missing.length) return;
  if (!B.supported) {
    box.append(h("div", { class: "note" }, "The screen code on the server can't switch built-in views off yet - update Gauge.cpp, Gauge_Layout.* and Gauge_Telemetry.h first."));
    return;
  }
  if (!listed.size) {
    box.append(h("div", { class: "note" }, `The screens also have ${B.views.length} built-in views (oil, tyres, clock…). Import them to switch them off per screen or change the swipe order.`),
      h("button", { class: "small primary", onclick: importBuiltins }, "Import built-in views"));
  } else {
    box.append(h("div", { class: "note" }, `${missing.length} built-in view${missing.length > 1 ? "s" : ""} in the screen code ${missing.length > 1 ? "aren't" : "isn't"} in this layout, so ${missing.length > 1 ? "they're" : "it's"} off: ${missing.map(b => cap(b.name)).join(", ")}.`),
      h("button", { class: "small", onclick: importBuiltins }, "Add " + (missing.length > 1 ? "them" : "it")));
  }
}
function importBuiltins() {
  const listed = new Set(S.doc.views.filter(isBuiltin).map(v => v.builtin));
  const ids = S.doc.views.map(v => v.id);
  const add = S.builtins.views.filter(b => !listed.has(b.key)).map(b => {
    const id = ids.includes(b.key) ? M.slugId("b_" + b.key, ids) : b.key; ids.push(id);
    return { id, builtin: b.key, name: cap(b.name).slice(0, 23), gauges: [] };
  });
  if (!add.length) return;
  // First import: built-ins go first, in the screen code's order - exactly the
  // swipe order the screens have now. Later additions go to the end, switched off.
  if (!listed.size) S.doc.views.unshift(...add);
  else S.doc.views.push(...add.map(v => ({ ...v, enabled: false })));
  S.vi = 0; S.gi = -1;
  changed({ fresh: true });
  toast(listed.size ? "Added - they start switched off." : `Imported ${add.length} built-in views from the screen code. The order is the swipe order; untick a view or a screen to take it off.`, false, 6000);
}
function selectView(i) {
  if (i === S.vi && S.gi < 0) return;
  S.vi = i; S.gi = -1;
  showCurrent(true); renderViews(); renderProps(); renderOverlay(); renderStatus(); renderDataBody();
}

const VIEW_TEMPLATES = {
  blank: { name: "New view", gauges: [] },
  arc: { name: "Arc gauge", gauges: [{ type: "arc", input: "coolant_c", alerts: { high_warn: 105, high_crit: 115 }, anim: { smooth: 0.5, sweep: true } }] },
  needle: { name: "Needle dial", gauges: [{ type: "needle", input: "oil_psi", ticks: { major: 6, minor: 4 }, number: { dy: 95 }, label: { dy: 140 }, unit: { dy: 58 }, anim: { smooth: 0.6, sweep: true } }] },
  digital: { name: "Big number", gauges: [{ type: "digital", input: "speed_mph" }] },
  dual: { name: "Two arcs", gauges: [
    { type: "arc", input: "coolant_c", start: 280, sweep: 160, segments: 12, ticks: { major: 0 }, number: { dy: -80 }, label: { font: 16, dy: -130 }, unit: { dy: -40 } },
    { type: "arc", input: "oil_temp_c", start: 260, sweep: -160, segments: 12, ticks: { major: 0 }, number: { dy: 80 }, label: { font: 16, dy: 130 }, unit: { show: false } }] },
  bar: { name: "Bars", gauges: [
    { type: "bar", input: "throttle_pct", y: 200, segments: 20 },
    { type: "bar", input: "inj_duty_pct", y: 320 }] },
};
function addView() {
  if (customCount() >= M.LIMITS.views) return toast(`The screens take at most ${M.LIMITS.views} custom views.`, true);
  dialog("Add a view", [
    h("div", { class: "tpl" }, ...Object.entries({ blank: "Blank", arc: "Arc gauge", needle: "Needle dial", digital: "Big number", dual: "Two arcs (top + bottom)", bar: "Two bars" })
      .map(([k, n]) => h("button", { type: "button", onclick: () => { closeDialog(); makeView(k); } }, n))),
  ], [["Cancel", null]]);
}
function makeView(tpl) {
  const t = structuredClone(VIEW_TEMPLATES[tpl]);
  const v = { id: M.slugId(t.name, S.doc.views.map(v => v.id)), name: t.name, gauges: t.gauges };
  S.doc.views.splice(S.vi + 1, 0, v);
  S.vi = Math.min(S.vi + 1, S.doc.views.length - 1); S.gi = -1;
  changed({ fresh: true });
}
function viewOp(op) {
  const v = view(); if (!v) return;
  const vs = S.doc.views;
  if (op === "up" && S.vi > 0) { [vs[S.vi - 1], vs[S.vi]] = [vs[S.vi], vs[S.vi - 1]]; S.vi--; }
  else if (op === "down" && S.vi < vs.length - 1) { [vs[S.vi + 1], vs[S.vi]] = [vs[S.vi], vs[S.vi + 1]]; S.vi++; }
  else if (op === "dup") {
    if (isBuiltin(v)) return toast("A built-in view can't be duplicated - it's drawn by the screen code.", true);
    if (customCount() >= M.LIMITS.views) return toast(`The screens take at most ${M.LIMITS.views} custom views.`, true);
    const c = structuredClone(v); c.name = (v.name || v.id).slice(0, 18) + " copy"; c.id = M.slugId(c.name, vs.map(x => x.id));
    vs.splice(S.vi + 1, 0, c); S.vi++; S.gi = -1;
  } else if (op === "del") {
    if (isBuiltin(v)) {                              // stays listed (so it stays off), just switched off
      if (v.enabled === false) return toast("Already off on every screen. Tick “Switched on” to bring it back.");
      v.enabled = false; changed();
      return toast(`“${v.name || v.id}” is off on every screen now (and won't take the screen over for an alert).`);
    }
    return dialog("Delete view", [h("p", {}, `Delete “${v.name || v.id}” and its ${v.gauges.length} gauges? (Undo brings it back.)`)],
      [["Cancel", null], ["Delete", () => { vs.splice(S.vi, 1); S.vi = Math.max(0, Math.min(S.vi, vs.length - 1)); S.gi = -1; changed({ fresh: true }); }, "danger"]]);
  } else return;
  changed();
}

// ================================================================ gauges ===
const NEW_GAUGE = {
  arc: () => ({ type: "arc", input: "coolant_c" }),
  needle: () => ({ type: "needle", input: "oil_psi", ticks: { major: 6, minor: 4 }, number: { dy: 95 }, label: { dy: 140 }, unit: { dy: 58 } }),
  bar: () => ({ type: "bar", input: "throttle_pct", w: 260, h: 30 }),
  digital: () => ({ type: "digital", input: "speed_mph" }),
  light: () => ({ type: "light", input: "handbrake", size: 36, label: { text: "P", font: 14, dy: 32 } }),
  text: () => ({ type: "text", text: "TEXT", font: 20, color: "dim" }),
  line: () => ({ type: "line", x: 140, y: 300, x2: 340, y2: 300, width: 2, color: "tickmaj" }),
  clock: () => ({ type: "clock" }),
  image: () => ({ type: "image", image: "car_370z", x: 240, y: 234 }),
};
function addGauge(type) {
  if (!view()) { makeView("blank"); }
  const v = view();
  if (isBuiltin(v)) return toast("This is a built-in view - the screen code draws it. Add gauges to a custom view (+ View).", true);
  if (v.gauges.length >= M.LIMITS.gauges) return toast(`A view takes at most ${M.LIMITS.gauges} gauges.`, true);
  const g = NEW_GAUGE[type]();
  // place smaller items off-centre so they don't land on top of a dial's number
  if (v.gauges.length && (type === "light" || type === "text")) { g.x = 240; g.y = type === "light" ? 110 : 400; }
  if (v.gauges.length && type === "bar") g.y = 330;
  if (type === "image" && S.lastImage && M.IMAGES[S.lastImage]) g.image = S.lastImage;
  const paired = (type === "arc" || type === "needle") ? pairDial(v, g) : null;
  v.gauges.push(g); S.gi = v.gauges.length - 1;
  changed();
  if (paired) toast(`Paired with the ${M.TYPE_NAMES[paired.type].toLowerCase()} already there: same reading and scale, `
    + `its own reading, unit, label and scale hidden so nothing shows twice. Change any of it under <b>Show</b> and <b>Reading</b>.`, false, 7000);
}
// A second dial dropped on the centre of an existing one is almost always the
// same reading drawn two ways (e.g. a needle over a coloured arc): match it up
// and hide the duplicate texts. Returns the dial it paired with, or null.
function pairDial(v, g) {
  const base = [...v.gauges].reverse().find(o => (o.type === "arc" || o.type === "needle") && M.eff(o).x === 240 && M.eff(o).y === 240);
  if (!base) return null;
  const e = M.eff(base);
  Object.assign(g, { input: e.input, min: e.min, max: e.max, start: e.start, sweep: e.sweep, decimals: e.decimals });
  if (base.units) g.units = base.units;
  if (base.format) g.format = base.format;
  if (base.alerts) g.alerts = structuredClone(base.alerts);       // both turn amber / red together
  g.number = { show: false }; g.unit = { show: false }; g.label = { text: "" };
  g.ticks = { major: 0 };
  const inner = e.r - e.width / 2;                                 // inside edge of the existing band
  if (g.type === "needle") {
    // thin ring just inside the band; the needle reaches across the band
    g.r = Math.max(20, Math.round(inner - 4)); g.width = 2; g.zones = false;
    g.needle = { length: Math.max(20, Math.round(e.r + e.width / 2 - 2)), width: 6, tail: 20, hub: 14 };
    if (base.anim) g.anim = structuredClone(base.anim);
    // the needle's hub sits on the centre: move the existing reading below it
    if (base.type === "arc" && e.number.show && Math.abs(e.number.dy) < 40 && Math.abs(e.number.dx) < 40) {
      M.setp(base, "number.dy", 95); M.setp(base, "unit.dy", 58); M.setp(base, "label.dy", 140);
    }
  } else {
    // a slimmer arc inside the existing dial's band
    g.r = Math.max(20, Math.round(inner - 14)); g.width = 16; g.segments = e.segments || 20;
    if (base.anim) g.anim = structuredClone(base.anim);
  }
  return base;
}
function gaugeOp(op) {
  const v = view(), g = gauge(); if (!g) return;
  const gs = v.gauges, i = S.gi;
  if (op === "del") { gs.splice(i, 1); S.gi = -1; }
  else if (op === "dup") {
    if (gs.length >= M.LIMITS.gauges) return toast(`A view takes at most ${M.LIMITS.gauges} gauges.`, true);
    const c = structuredClone(g); c.x = M.eff(g).x + 20; c.y = M.eff(g).y + 20; gs.splice(i + 1, 0, c); S.gi = i + 1;
  }
  else if (op === "front" && i < gs.length - 1) { gs.push(gs.splice(i, 1)[0]); S.gi = gs.length - 1; }
  else if (op === "back" && i > 0) { gs.unshift(gs.splice(i, 1)[0]); S.gi = 0; }
  else return;
  changed();
}
function selectGauge(i) { if (S.gi === i) return; S.gi = i; renderProps(); renderOverlay(); }

// Changing what a gauge reads: take the new input's range/decimals/label
// unless the user set their own, and drop alerts that were in other units.
function setInput(g, key) {
  const old = M.INPUT[g.input], nu = M.INPUT[key];
  const oldDef = M.defaults(g.type, g.input);
  g.input = key;
  const sameUnits = old && nu && old.unit === nu.unit && old.cat === nu.cat;
  if (!sameUnits) {
    delete g.min; delete g.max; delete g.origin; delete g.decimals; delete g.units; delete g.alerts;
    if (g.on && nu && nu.kind !== "num") delete g.on;
  }
  if (g.label && (g.label.text === undefined || g.label.text === oldDef.label.text)) delete g.label.text;
  if (g.label && !Object.keys(g.label).length) delete g.label;
  delete g.format;
}

// ================================================================ properties panel ===
let binds = [];
function syncProps() { for (const b of binds) b(); }
function section(id, title, hint, ...body) {
  const open = S.openSec[id] !== false;
  const d = h("details", { class: "psec", open }, h("summary", {}, title, hint ? h("span", { class: "hint" }, hint) : null), h("div", { class: "pbody" }, ...body));
  d.addEventListener("toggle", () => { S.openSec[id] = d.open; savePrefs(); });
  return d;
}
const field = (label, ...ctl) => h("div", { class: "f" }, h("span", {}, label), h("span", { class: "ctl" }, ...ctl));

// value controls bound to a path on the stored gauge (or view)
function numCtl(obj, path, { min, max, step = 1, disp = false, unit = "", nullable = false, width } = {}) {
  const inp = h("input", { type: "number", min, max, step, style: width ? `width:${width}px` : null });
  const getEff = () => {
    const e = obj.type ? M.eff(obj) : obj;
    let v = M.getp(e, path);
    if (v === undefined) v = M.getp(obj, path);
    return v;
  };
  const show = () => {
    if (document.activeElement === inp) return;
    const v = getEff();
    if (v === null || v === undefined || !Number.isFinite(+v)) { inp.value = ""; return; }
    inp.value = disp ? round(M.toDisp(M.eff(obj), v, units()), 3) : round(v, 3);
  };
  const read = () => {
    if (inp.value === "") return nullable ? null : undefined;
    let v = Number(inp.value); if (!Number.isFinite(v)) return undefined;
    if (min !== undefined && !disp) v = Math.max(min, v);
    if (max !== undefined && !disp) v = Math.min(max, v);
    return disp ? round(M.fromDisp(M.eff(obj), v, units()), 4) : v;
  };
  inp.addEventListener("input", () => { const v = read(); if (v !== undefined) { M.setp(obj, path, v); changed({ commit: false, props: false }); } });
  inp.addEventListener("change", () => { const v = read(); if (v !== undefined || nullable) M.setp(obj, path, v === undefined ? null : v); inp.blur(); changed(); });
  binds.push(show); show();
  return unit ? [inp, h("span", { class: "u" }, unit)] : [inp];
}
function rangeCtl(obj, path, min, max, step, fmt) {
  const inp = h("input", { type: "range", min, max, step });
  const out = h("span", { class: "u", style: "min-width:44px" });
  const show = () => { const v = M.getp(M.eff(obj), path); inp.value = v; out.textContent = fmt ? fmt(v) : v; };
  inp.addEventListener("input", () => { M.setp(obj, path, Number(inp.value)); out.textContent = fmt ? fmt(+inp.value) : inp.value; changed({ commit: false, props: false }); });
  inp.addEventListener("change", () => changed({ props: false }));
  binds.push(show); show();
  return [inp, out];
}
function checkCtl(obj, path, text, { rebuild = false } = {}) {
  const inp = h("input", { type: "checkbox" });
  const show = () => { inp.checked = !!M.getp(obj.type ? M.eff(obj) : obj, path); };
  inp.addEventListener("change", () => { M.setp(obj, path, inp.checked); changed({ props: rebuild }); });
  binds.push(show); show();
  return text ? h("label", { class: "tog" }, inp, text) : inp;
}
function selectCtl(obj, path, options, { rebuild = true, onset, rebuildAfter = false } = {}) {
  const sel = h("select", {}, ...options.map(o => Array.isArray(o) ? h("option", { value: o[0] }, o[1]) : o));
  const show = () => { const v = M.getp(obj.type ? M.eff(obj) : obj, path); sel.value = String(v); };
  sel.addEventListener("change", () => {
    const raw = sel.value; const v = /^-?\d+(\.\d+)?$/.test(raw) ? Number(raw) : raw === "true" ? true : raw === "false" ? false : raw;
    if (onset) onset(v); else M.setp(obj, path, v);
    changed({ props: rebuild || rebuildAfter });
  });
  binds.push(show); show();
  return sel;
}
function textCtl(obj, path, { max = M.LIMITS.text, placeholder, onset } = {}) {
  const inp = h("input", { type: "text", maxlength: max, placeholder });
  const show = () => { if (document.activeElement !== inp) inp.value = M.getp(obj.type ? M.eff(obj) : obj, path) ?? ""; };
  inp.addEventListener("input", () => { (onset || ((v) => M.setp(obj, path, v)))(inp.value); changed({ commit: false, props: false }); });
  inp.addEventListener("change", () => { changed({ props: false }); });
  binds.push(show); show();
  return inp;
}
const fontCtl = (obj, path) => selectCtl(obj, path, M.FONTS.map(f => [f, f + " px"]), { rebuild: false });
function colorCtl(obj, path, roles = M.ROLES) {
  const sw = h("span", { class: "swatch" });
  const sel = h("select", {}, ...roles.map(r => h("option", { value: r }, M.ROLE_NAMES[r])), h("option", { value: "custom" }, "Custom colour…"));
  const pick = h("input", { type: "color" });
  const show = () => {
    const v = M.getp(M.eff(obj), path);
    const custom = typeof v === "string" && v[0] === "#";
    sel.value = custom ? "custom" : v; pick.hidden = !custom;
    if (custom) pick.value = v;
    sw.style.background = M.colorCss(v, pal());
  };
  sel.addEventListener("change", () => {
    if (sel.value === "custom") { M.setp(obj, path, M.colorCss(M.getp(M.eff(obj), path), pal())); }
    else M.setp(obj, path, sel.value);
    changed({ props: false });
  });
  pick.addEventListener("input", () => { M.setp(obj, path, pick.value); sw.style.background = pick.value; changed({ commit: false, props: false }); });
  pick.addEventListener("change", () => changed({ props: false }));
  binds.push(show); show();
  return [sw, sel, pick];
}

function inputOptions(includeNone) {
  const groups = [["Temperatures", r => r.cat === "temp" && !r.key.startsWith("tyre")],
                  ["Pressures", r => (r.cat === "press" || r.cat === "press_kpa") && !r.key.startsWith("tyre")],
                  ["Tyres", r => r.key.startsWith("tyre")],
                  ["Engine & driving", r => r.kind !== "bool" && r.kind !== "lights" && r.kind !== "oil_status" && (r.cat === "none" || r.cat === "speed") && r.path[0] !== "@"],
                  ["Lights & switches", r => r.kind === "bool" || r.kind === "lights" || r.kind === "oil_status"],
                  ["G-force (screen's own sensor)", r => r.path[0] === "@"]];
  const used = new Set();
  const out = includeNone ? [h("option", { value: "" }, "-")] : [];
  for (const [name, f] of groups) {
    const rows = M.INPUTS.filter(r => !used.has(r.key) && f(r));
    rows.forEach(r => used.add(r.key));
    if (rows.length) out.push(h("optgroup", { label: name }, ...rows.map(r => h("option", { value: r.key }, `${r.label || r.key}  (${r.key}${r.unit ? ", " + r.unit : ""})`))));
  }
  const rest = M.INPUTS.filter(r => !used.has(r.key));
  if (rest.length) out.push(h("optgroup", { label: "Other" }, ...rest.map(r => h("option", { value: r.key }, r.label || r.key))));
  return out;
}
function inputHint(key) {
  const r = M.INPUT[key]; if (!r) return "";
  const bits = { bool: "on/off (1 = on)", lights: "0 off · 1 side · 2 head", oil_status: "1 ok · 2 amber · 3 red", gear: "-1 R · 0 N · 1-6" };
  return (bits[r.kind] ? bits[r.kind] + " · " : "") + (CAN ? `brain value “${r.path}”` : `reading “${r.key}”`) + (r.path[0] === "@" ? " (from the screen's own motion sensor)" : "");
}

function renderProps() {
  binds = [];
  const P = $("#props"); const keep = P.scrollTop; P.innerHTML = "";
  const v = view();
  if (!v) { P.append(h("div", { class: "pbody", style: "padding:16px" }, h("p", { class: "note" }, "No views. Add one with + View."))); return; }
  const g = gauge();
  if (isBuiltin(v)) renderBuiltinProps(P, v);
  else if (!g) renderViewProps(P, v); else renderGaugeProps(P, v, g);
  P.scrollTop = keep;
}

function renderBuiltinProps(P, v) {
  const info = builtinInfo(v.builtin);
  const on = h("input", { type: "checkbox" }); on.checked = v.enabled !== false;
  on.addEventListener("change", () => { if (on.checked) delete v.enabled; else v.enabled = false; changed(); });
  const scr = v.screens || [];
  const chip = (n, label) => {
    const cb = h("input", { type: "checkbox", disabled: v.enabled === false });
    cb.checked = n === 0 ? !scr.length : scr.includes(n);
    cb.addEventListener("change", () => {
      let s = (v.screens || []).slice();
      if (n === 0) s = []; else s = cb.checked ? [...new Set([...s, n])].sort() : s.filter(x => x !== n);
      if (s.length) v.screens = s; else delete v.screens;
      changed();
    });
    return h("label", {}, cb, label);
  };
  const alertNote = { oil: "Oil-pressure alert: jumps here on screens that show this view; on a screen without it, to the oil split view if that's on, otherwise it doesn't jump. The red face flash follows its own setting either way.",
                      oildual: "Oil-pressure alert: jumps here on screens that show this view but not the oil pressure dial.",
                      tpms: "Tyre alert: only jumps here on screens that show this view." }[v.builtin];
  P.append(h("div", { class: "ghead" }, h("h3", {}, "Built-in view: " + (v.name || v.id))));
  P.append(section("bview", "Built-in view", v.builtin,
    h("p", { class: "note" }, info ? `“${cap(info.name)}” - drawn by the screen code itself, so it can't be edited here. You can choose where it appears in the swipe order (▲▼ on the left) and which screens show it.`
      : `The screen code on the server has no built-in view called “${v.builtin}”, so the screens will skip this entry.`),
    field("Switched on", h("label", { class: "tog" }, on, "Show this view")),
    field("On screens", h("div", { class: "chips" }, chip(0, "All"), chip(1, "1"), chip(2, "2"), chip(3, "3"))),
    alertNote ? h("div", { class: "note" }, alertNote) : null,
    h("div", { class: "note" }, "A screen never jumps to a view it doesn't show.")));
  // turn it into an editable custom view
  const can = CONVERTIBLE.includes(v.builtin);
  P.append(section("convert", "Edit it", null,
    !can ? h("p", { class: "note" }, "This one can't be converted yet - it needs a gauge type the editor doesn't have (the G-force ball).")
    : !S.builtins.convert ? h("p", { class: "note" }, "To edit it, the screen code on the server needs the latest Gauge_Layout.cpp / Gauge_Layout.h (the layout engine that can redraw the built-in views).")
    : h("div", {},
        h("p", { class: "note" }, "Makes an editable custom view that redraws this one exactly - same scale, the limits from your Gauge_Config.h, same alert colours - and switches this built-in view off on the same screens. Undo puts it back."),
        h("button", { class: "primary", onclick: () => convertView(v) }, "Convert to an editable view"))));
}
function convertView(v) {
  if (customCount() >= M.LIMITS.views) return toast(`The screens take at most ${M.LIMITS.views} custom views - delete one first.`, true);
  if (NEEDS_MEDIA.includes(v.builtin) && !S.builtins.media)
    return toast("To edit this one, the screen code on the server needs the latest Gauge.cpp and Gauge_Layout.cpp / .h (the engine with clock and image gauges).", true, 8000);
  let rec;
  try { rec = convertBuiltin(v.builtin, S.builtins.config || {}); }
  catch (e) { return toast("Couldn't convert: " + esc(e.message), true); }
  const ids = S.doc.views.map(x => x.id);
  const nv = { id: M.slugId("my_" + v.builtin, ids), name: rec.name, gauges: rec.gauges };
  const stockNote = rec.stock.length ? ` <b>Couldn't read ${rec.stock.map(esc).join(", ")} from the sketch, so the stock value${rec.stock.length > 1 ? "s were" : " was"} used</b> - check the alert levels in the gauge's settings.` : "";
  if (v.screens) nv.screens = [...v.screens];
  const i = S.doc.views.indexOf(v);
  S.doc.views.splice(i + 1, 0, nv);
  v.enabled = false;                              // the copy takes its place in the swipe order
  S.vi = i + 1; S.gi = -1;
  changed({ fresh: true });
  toast(`“${esc(nv.name)}” is now an editable view, in the built-in's place in the swipe order. The built-in one is switched off (tick it back on, or Undo, to return).` + stockNote, !!stockNote, stockNote ? 15000 : 7000);
}

function renderViewProps(P, v) {
  const ids = S.doc.views.filter(x => x !== v).map(x => x.id);
  const idInp = h("input", { type: "text", maxlength: M.LIMITS.id, value: v.id, pattern: "[a-z0-9_]+" });
  const idNote = h("div", { class: "note" });
  const checkId = () => {
    const s = idInp.value;
    const ok = /^[a-z0-9_]{1,15}$/.test(s) && !ids.includes(s);
    idNote.textContent = ok ? "Used by the screen to remember which view was showing." : (ids.includes(s) ? "Another view already uses that id." : "1-15 characters: a-z, 0-9 and _ only.");
    idNote.style.color = ok ? "" : "#ff8b98";
    return ok;
  };
  idInp.addEventListener("input", () => { idInp.value = idInp.value.toLowerCase().replace(/[^a-z0-9_]/g, "_"); checkId(); });
  idInp.addEventListener("change", () => { if (checkId()) { v.id = idInp.value; changed({ props: false }); } else { toast("Id not changed: " + esc(idNote.textContent), true); idInp.value = v.id; checkId(); } });
  checkId();
  const nameInp = textCtl(v, "name", { max: 23, onset: val => { v.name = val; } });

  const scr = v.screens || [];
  const chip = (n, label) => {
    const cb = h("input", { type: "checkbox" });
    cb.checked = n === 0 ? !scr.length : scr.includes(n);
    cb.addEventListener("change", () => {
      let s = (v.screens || []).slice();
      if (n === 0) s = [];
      else { s = cb.checked ? [...new Set([...s, n])].sort() : s.filter(x => x !== n); }
      if (s.length) v.screens = s; else delete v.screens;
      changed();
    });
    return h("label", {}, cb, label);
  };
  const faceCb = h("input", { type: "checkbox" }); faceCb.checked = v.face !== false;
  faceCb.addEventListener("change", () => { if (faceCb.checked) delete v.face; else v.face = false; changed({ props: false }); });

  P.append(h("div", { class: "ghead" }, h("h3", {}, "View: " + (v.name || v.id))));
  P.append(section("view", "View", null,
    field("Name", nameInp),
    field("Id", idInp), idNote,
    field("On screens", h("div", { class: "chips" }, chip(0, "All"), chip(1, "1"), chip(2, "2"), chip(3, "3"))),
    h("div", { class: "note" }, CAN ? "Screen numbers are each screen's GA_SCREEN_ID. A view only on screen 2 is skipped when swiping on the others." : "This gauge is screen 1. Leave this on All, or tick 1. A view ticked only for another screen is not offered on this gauge."),
    field("Dial face", h("label", { class: "tog" }, faceCb, "Draw the round dial face")),
    field("Background", ...bgCtl(v)),
    h("div", { class: "note" }, "The face colour for this view (or the whole screen with no dial face). Block notches drawn in the “Dial face” colour follow it, so they still look cut out."),
    field("Switched on", (() => { const cb = h("input", { type: "checkbox" }); cb.checked = v.enabled !== false;
      cb.addEventListener("change", () => { if (cb.checked) delete v.enabled; else v.enabled = false; changed(); });
      return h("label", { class: "tog" }, cb, "Show this view (untick to keep it but take it off every screen)"); })()),
  ));
  const list = h("ol", { class: "glist" }, ...v.gauges.map((g, i) => {
    const r = M.INPUT[g.input];
    return h("li", { onclick: () => selectGauge(i) }, h("b", {}, M.TYPE_NAMES[g.type] || g.type),
      g.type === "text" ? `“${g.text || ""}”` : g.type === "image" ? (g.image || "?") : (g.type === "line" || g.type === "clock") ? "" : (r ? r.label || r.key : g.input || "?"), h("span", { class: "t" }, "#" + (i + 1)));
  }));
  P.append(section("glist", "Gauges in this view", `${v.gauges.length}/${M.LIMITS.gauges}`,
    v.gauges.length ? list : h("p", { class: "note" }, "None yet. Use the Add buttons above the screen."),
    h("div", { class: "note" }, "Later gauges are drawn on top. Tap a gauge on the screen to edit it; drag to move it.")));
  P.append(stylesSection(v));
  P.append(section("help", "Keys", null, h("div", { class: "note" },
    "Arrows move 5 px (Shift = 1 px) · drag with Alt held to skip snapping · Delete removes · Ctrl+D duplicates · Ctrl+C / Ctrl+V copy a gauge between views · Ctrl+Z / Ctrl+Y undo / redo · Ctrl+S saves · Esc deselects")));
}

// The view's own background colour ("bg"): a theme colour or #rrggbb.
function bgCtl(v) {
  const sw = h("span", { class: "swatch" });
  const sel = h("select", {}, h("option", { value: "" }, "Theme (normal face)"),
    ...M.ROLES.filter(r => r !== "dial").map(r => h("option", { value: r }, M.ROLE_NAMES[r])), h("option", { value: "custom" }, "Custom colour…"));
  const pick = h("input", { type: "color" });
  const show = () => {
    const c = v.bg, custom = typeof c === "string" && c[0] === "#";
    sel.value = c == null ? "" : custom ? "custom" : c;
    pick.hidden = !custom; if (custom) pick.value = c;
    sw.style.background = c == null ? M.hex6(v.face === false ? pal().bg : pal().dial) : M.colorCss(c, pal());
  };
  sel.addEventListener("change", () => {
    if (sel.value === "") delete v.bg;
    else if (sel.value === "custom") v.bg = v.bg && v.bg[0] === "#" ? v.bg : "#202a3a";
    else v.bg = sel.value;
    show(); changed({ props: false });
  });
  pick.addEventListener("input", () => { v.bg = pick.value; sw.style.background = pick.value; changed({ commit: false, props: false }); });
  pick.addEventListener("change", () => changed({ props: false }));
  binds.push(show); show();
  return [sw, sel, pick];
}

// ---------------------------------------------------------------- favourite looks ---
// A look = the view's background + dial face setting, and per gauge type the
// look of the first gauge of that type (colours, band, blocks, ticks, fonts) -
// never positions, ranges, inputs or alert levels. Applying it to another view
// restyles every gauge of those types there.
const LOOK_DIAL = ["colors", "width", "segments", "zones", "rounded", "gaps", "unlit_shade", "zone_fill", "glow", "core", "ring",
  "ticks.font", "ticks.numbers", "ticks.inset", "ticks.length", "ticks.minor_length", "ticks.step", "ticks.minor",
  "number.font", "number.style", "number.height", "number.digits", "number.slant", "number.thickness", "number.ghost", "number.glow", "number.core", "number.fixed", "label.font", "unit.font", "note.font", "peak_label.font"];
const LOOK = {
  arc: LOOK_DIAL, needle: [...LOOK_DIAL, "needle"],
  bar: ["colors", "segments", "unlit_shade", "zone_fill", "glow", "core", "number.font", "number.style", "number.height", "number.digits", "number.slant", "number.thickness", "number.ghost", "number.glow", "number.core", "number.fixed", "label.font", "unit.font"],
  digital: ["colors", "number.font", "number.style", "number.height", "number.digits", "number.slant", "number.thickness", "number.ghost", "number.glow", "number.core", "number.fixed", "label.font", "unit.font"],
  light: ["colors", "shape", "on_color", "off_color", "blink", "label.font"],
  text: ["font", "color"], line: ["width", "color"],
  clock: ["colors", "ticks.font", "ticks.numbers", "ticks.length", "ticks.inset", "ticks.minor", "label.font", "thickness", "needle"],
  image: ["tint"],
};
const OPTIONAL_PARTS = ["note", "peak_label"];        // only restyled where the gauge has one
function lookOf(v) {
  const look = { face: v.face !== false, types: {} };
  if (v.bg != null) look.bg = v.bg;
  for (const g of v.gauges || []) {
    if (look.types[g.type] || !LOOK[g.type]) continue;
    const t = {};
    for (const p of LOOK[g.type]) { const x = M.getp(g, p); if (x !== undefined) t[p] = structuredClone(x); }
    look.types[g.type] = t;
  }
  return look;
}
function applyLook(v, look) {
  if (look.bg != null) v.bg = look.bg; else delete v.bg;
  if (look.face === false) v.face = false; else delete v.face;
  let n = 0;
  for (const g of v.gauges || []) {
    const t = look.types && look.types[g.type];
    if (!t) continue;
    for (const p of LOOK[g.type]) {
      const [top] = p.split(".");
      if (p.includes(".") && OPTIONAL_PARTS.includes(top) && !g[top]) continue;
      M.setp(g, p, t[p] === undefined ? undefined : structuredClone(t[p]));
    }
    n++;
  }
  return n;
}
function lookSummary(look) {
  const types = Object.keys(look.types || {}).map(t => (M.TYPE_NAMES[t] || t).toLowerCase());
  return [look.bg != null ? "background" : null, look.face === false ? "no face" : null, ...types].filter(Boolean).join(" · ") || "background only";
}
function lookSwatches(look) {
  const cols = [look.bg != null ? look.bg : "dial"];
  for (const t of Object.values(look.types || {})) {
    if (t.colors) for (const c of Object.values(t.colors)) if (typeof c === "string") cols.push(c);
    if (typeof t.color === "string") cols.push(t.color);
  }
  return h("span", { class: "lsw" }, ...[...new Set(cols)].slice(0, 6).map(c => h("i", { style: `background:${M.colorCss(c, pal())}` })));
}
async function loadStyles() {
  try { S.styles = (await api("/api/styles")).styles || []; } catch (e) { S.styles = []; }
}
async function storeStyles(list) {
  await api("/api/styles", { styles: list });
  S.styles = list;
}
function stylesSection(v) {
  const saveLook = () => {
    const inp = h("input", { type: "text", maxlength: 40, value: (v.name || v.id) + " look" });
    dialog("Save this view's look", [
      h("p", { class: "note" }, "Keeps the background and, for each kind of gauge in this view, its colours, band, blocks, ticks and fonts - not positions, scales, inputs or alert levels."),
      inp], [["Cancel", null], ["Save", async () => {
        const name = inp.value.trim(); if (!name) return false;
        const list = S.styles.filter(x => x.name !== name);
        list.unshift({ name, look: lookOf(v), at: Date.now() });
        try { await storeStyles(list); toast(`Saved “${esc(name)}” to your favourite looks.`); renderProps(); }
        catch (e) { toast("Couldn't save it: " + esc(e.message), true); return false; }
      }, "primary"]]);
  };
  const apply = st => {
    const n = applyLook(v, st.look);
    changed();
    toast(`Applied “${esc(st.name)}” to this view` + (n ? ` (${n} gauge${n > 1 ? "s" : ""}).` : " - its background only; there are no gauges of the same kinds here.") + " Undo takes it back.");
  };
  const del = st => dialog("Remove favourite", [h("p", {}, `Remove “${st.name}” from your favourite looks?`)],
    [["Cancel", null], ["Remove", async () => {
      try { await storeStyles(S.styles.filter(x => x !== st)); renderProps(); } catch (e) { toast("Couldn't remove it: " + esc(e.message), true); }
    }, "danger"]]);
  const rows = S.styles.map(st => h("div", { class: "lrow" },
    lookSwatches(st.look),
    h("span", { class: "ln" }, h("b", {}, st.name), h("span", { class: "muted" }, lookSummary(st.look))),
    h("button", { class: "small primary", onclick: () => apply(st), title: "Restyle this view with it" }, "Apply"),
    h("button", { class: "small icon", onclick: () => del(st), title: "Remove from favourites" }, "✕")));
  return section("looks", "Favourite looks", S.styles.length ? String(S.styles.length) : null,
    h("button", { class: "small", onclick: saveLook }, "★ Save this view's look"),
    rows.length ? h("div", { class: "looks" }, ...rows) : h("p", { class: "note" }, "None saved yet. Style a view the way you like it, save its look, then apply it to other views in one click."));
}

function renderLineProps(P, g) {
  P.append(h("div", { class: "ghead" },
    h("button", { class: "small icon", title: "Back to the view", onclick: () => { S.gi = -1; renderProps(); renderOverlay(); } }, "←"),
    h("h3", {}, "Line"),
    h("button", { class: "small", onclick: () => gaugeOp("dup"), title: "Ctrl+D" }, "Duplicate"),
    h("button", { class: "small", onclick: () => gaugeOp("front") }, "Front"),
    h("button", { class: "small", onclick: () => gaugeOp("back") }, "Back"),
    h("button", { class: "small danger", onclick: () => gaugeOp("del") }, "Delete")));
  P.append(section("line", "Line", "px on the 480×480 screen",
    h("div", { class: "pair" }, field("From x", ...numCtl(g, "x", { min: -100, max: 580, width: 72 })), field("y", ...numCtl(g, "y", { min: -100, max: 580, width: 72 }))),
    h("div", { class: "pair" }, field("To x", ...numCtl(g, "x2", { min: -100, max: 580, width: 72 })), field("y", ...numCtl(g, "y2", { min: -100, max: 580, width: 72 }))),
    field("Thickness", ...numCtl(g, "width", { min: 1, max: 40, width: 72 })),
    field("Colour", ...colorCtl(g, "color")),
    h("div", { class: "note" }, "Drag the line to move it, or the handles at its ends.")));
}

// ---------------------------------------------------------------- clock ---
function gaugeHead(P, title) {
  P.append(h("div", { class: "ghead" },
    h("button", { class: "small icon", title: "Back to the view", onclick: () => { S.gi = -1; renderProps(); renderOverlay(); } }, "←"),
    h("h3", {}, title),
    h("button", { class: "small", onclick: () => gaugeOp("dup"), title: "Ctrl+D" }, "Duplicate"),
    h("button", { class: "small", onclick: () => gaugeOp("front"), title: "Draw on top" }, "Front"),
    h("button", { class: "small", onclick: () => gaugeOp("back"), title: "Draw underneath" }, "Back"),
    h("button", { class: "small danger", onclick: () => gaugeOp("del"), title: "Delete" }, "Delete")));
}
function renderClockProps(P, g) {
  const e = M.eff(g), an = M.clockAnalog(e, units());
  gaugeHead(P, "Clock");
  P.append(section("clock", "Clock", "the screen's own clock (RTC, set from the brain)",
    field("Face", selectCtl(g, "style", [["auto", `Screen setting (now ${S.theme.clock !== 0 ? "analogue" : "digital"})`], ["analog", "Analogue"], ["digital", "Digital"]])),
    h("div", { class: "note" }, "“Screen setting” follows the CLOCK switch on each screen's settings pages - preview it with the clock choice above the screen."),
    h("div", { class: "pair" }, field("Centre x", ...numCtl(g, "x", { min: -100, max: 580, width: 72 })), field("y", ...numCtl(g, "y", { min: -100, max: 580, width: 72 })))));
  if (an) P.append(section("clockana", "Analogue face", null,
    field("Radius", ...numCtl(g, "r", { min: 20, max: 240, width: 72, unit: "px" })),
    field("Hour marks", ...numCtl(g, "ticks.length", { min: 2, max: 60, width: 72, unit: "px long" })),
    field("…in from the edge", ...numCtl(g, "ticks.inset", { min: -60, max: 120, width: 72, unit: "px" })),
    field("Minute marks", (() => { const cb = h("input", { type: "checkbox" }); cb.checked = (e.ticks.minor || 1) > 1;
      cb.addEventListener("change", () => { M.setp(g, "ticks.minor", cb.checked ? 5 : undefined); changed(); }); return h("label", { class: "tog" }, cb, "a mark every minute"); })()),
    field("Numbers", checkCtl(g, "ticks.numbers", "1-12"), fontCtl(g, "ticks.font")),
    h("div", { class: "pair" }, field("Hour hand", ...numCtl(g, "hands.hour", { min: 0.05, max: 1.2, step: 0.01, width: 64, unit: "× r" })),
      field("Minute", ...numCtl(g, "hands.minute", { min: 0.05, max: 1.2, step: 0.01, width: 64, unit: "× r" }))),
    h("div", { class: "pair" }, field("Second hand", checkCtl(g, "hands.seconds", "")), field("length", ...numCtl(g, "hands.second", { min: 0.05, max: 1.2, step: 0.01, width: 64, unit: "× r" }))),
    field("Centre dot", ...numCtl(g, "needle.hub", { min: 0, max: 80, width: 72, unit: "px" }))));
  else P.append(section("clockdig", "Digital face", "24 hour, like the built-in clock",
    field("Digit height", ...numCtl(g, "digit_height", { min: 20, max: 300, width: 72, unit: "px" })),
    field("Segment thickness", ...numCtl(g, "thickness", { min: 2, max: 60, width: 72, unit: "px" })),
    h("div", { class: "note" }, "Drag the digits to move the clock; the square handle sizes them.")));
  P.append(section("clockdate", "Date", null,
    field("Show", checkCtl(g, "label.show", "")), field("Text", textCtl(g, "label.text")),
    h("div", { class: "note" }, "{date} = Sun 25/10/2026, {time} = 14:05, {day} = Sun - mix with your own text."),
    field("Font", fontCtl(g, "label.font")),
    field("Analogue face", checkCtl(g, "date_on_analog", "show it there too (the built-in clock doesn't)")),
    h("div", { class: "note" }, "Drag the date on the screen to move it.")));
  P.append(section("clockcol", "Colours", null,
    field(an ? "Hour + minute hands" : "Digits", ...colorCtl(g, "colors.text")),
    an ? field("Second hand + dot", ...colorCtl(g, "colors.needle")) : null,
    field("Date", ...colorCtl(g, "colors.label")),
    an ? field("Hour marks", ...colorCtl(g, "colors.ticks")) : null,
    an && (e.ticks.minor || 1) > 1 ? field("Minute marks", ...colorCtl(g, "colors.minor_ticks")) : null,
    an && e.ticks.numbers ? field("Numbers", ...colorCtl(g, "colors.scale")) : null));
}

// ---------------------------------------------------------------- images ---
// Uploaded images live in the layout file (doc.images: name -> {w, h, data})
// in the screens' own pixel format: RGB565 (low byte first) + 8-bit alpha per
// pixel, base64. Export writes them into Gauge_Layouts.h as C arrays.
const CAR = "car_370z";
function b64(bytes) { let s = ""; for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000)); return btoa(s); }
function unb64(s) { const b = atob(s), out = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) out[i] = b.charCodeAt(i); return out; }
function toScreenPixels(id) {             // ImageData -> RGB565 LE + alpha
  const d = id.data, out = new Uint8Array(id.width * id.height * 3);
  for (let i = 0, j = 0; i < d.length; i += 4, j += 3) {
    const v = ((d[i] >> 3) << 11) | ((d[i + 1] >> 2) << 5) | (d[i + 2] >> 3);
    out[j] = v & 255; out[j + 1] = v >> 8; out[j + 2] = d[i + 3];
  }
  return out;
}
function thumbCanvas(im, max = 48) {       // a small picture of a stored image
  const c = h("canvas", { class: "thumb" });
  const bytes = unb64(im.data), w = im.w, hh = im.h;
  const full = document.createElement("canvas"); full.width = w; full.height = hh;
  const id = full.getContext("2d").createImageData(w, hh);
  for (let i = 0, j = 0; j < bytes.length; i += 4, j += 3) {
    const v = bytes[j] | (bytes[j + 1] << 8), r = v >> 11, gg = (v >> 5) & 63, b = v & 31;
    id.data[i] = (r << 3) | (r >> 2); id.data[i + 1] = (gg << 2) | (gg >> 4); id.data[i + 2] = (b << 3) | (b >> 2); id.data[i + 3] = bytes[j + 2];
  }
  full.getContext("2d").putImageData(id, 0, 0);
  const k = Math.min(1, max / Math.max(w, hh)); c.width = Math.max(1, Math.round(w * k)); c.height = Math.max(1, Math.round(hh * k));
  c.getContext("2d").drawImage(full, 0, 0, c.width, c.height);
  return c;
}
let imgSig = null;
function syncImages() {
  const imgs = S.doc.images || {};
  const sig = Object.entries(imgs).map(([k, v]) => `${k}:${v.w}x${v.h}:${(v.data || "").length}:${(v.data || "").slice(-40)}`).join("|");
  if (sig === imgSig) return;
  imgSig = sig;
  const list = [];
  for (const [name, v] of Object.entries(imgs)) {
    try { const bytes = unb64(v.data); if (bytes.length === v.w * v.h * 3) list.push({ name, w: v.w, h: v.h, bytes }); } catch (e) { /* skip a broken one */ }
  }
  PV.setImages(list);
  const sizes = {}; const car = PV.carSize(); if (car) sizes[CAR] = car;
  for (const im of list) sizes[im.name] = [im.w, im.h];
  M.setImageSizes(sizes);
}
function imagesUsed() {
  const used = new Set();
  for (const v of S.doc.views) for (const g of v.gauges || []) if (g.type === "image" && g.image) used.add(g.image);
  return used;
}
function uploadImage(g) {
  const file = h("input", { type: "file", accept: "image/png,image/jpeg,image/webp,image/gif" });
  const nameIn = h("input", { type: "text", maxlength: 24, placeholder: "name (a-z 0-9 _)" });
  const wIn = h("input", { type: "number", min: 1, max: 480, style: "width:72px" }), hIn = h("input", { type: "number", min: 1, max: 480, style: "width:72px" });
  const lock = h("input", { type: "checkbox", checked: true });
  const prev = h("canvas", { class: "upprev", width: 200, height: 200 });
  const info = h("div", { class: "note" }, "PNG with transparency works best (the background shows through). JPEG has none.");
  let src = null;
  const car = PV.carSize();
  const draw = () => {
    const c = prev.getContext("2d"); c.fillStyle = M.hex6(pal().dial); c.fillRect(0, 0, 200, 200);
    if (!src) return;
    const w = +wIn.value || 1, hh = +hIn.value || 1, k = Math.min(1, 190 / Math.max(w, hh));
    c.imageSmoothingQuality = "high";
    c.drawImage(src, (200 - w * k) / 2, (200 - hh * k) / 2, w * k, hh * k);
    info.textContent = `${w} × ${hh} px on the screen · ${Math.round(w * hh * 3 / 1024)} KB of firmware space.` + (w * hh * 3 > 200 * 1024 ? " That's big - think about making it smaller." : "");
  };
  const fit = (mw, mh) => { if (!src) return; const k = Math.min(mw / src.naturalWidth, mh / src.naturalHeight, 1e9); wIn.value = Math.max(1, Math.round(src.naturalWidth * k)); hIn.value = Math.max(1, Math.round(src.naturalHeight * k)); draw(); };
  wIn.addEventListener("input", () => { if (lock.checked && src) hIn.value = Math.max(1, Math.round(+wIn.value * src.naturalHeight / src.naturalWidth)); draw(); });
  hIn.addEventListener("input", () => { if (lock.checked && src) wIn.value = Math.max(1, Math.round(+hIn.value * src.naturalWidth / src.naturalHeight)); draw(); });
  file.addEventListener("change", () => {
    const f = file.files[0]; if (!f) return;
    const im = new Image();
    im.onload = () => {
      src = im;
      if (!nameIn.value) nameIn.value = f.name.replace(/\.[^.]+$/, "").toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_+|_+$/g, "").slice(0, 24) || "image";
      fit(Math.min(480, im.naturalWidth), Math.min(480, im.naturalHeight));
    };
    im.onerror = () => toast("Couldn't read that image.", true);
    im.src = URL.createObjectURL(f);
  });
  draw();
  dialog("Upload an image", [
    file, h("div", { class: "upbox" }, prev, h("div", {},
      field("Name", nameIn),
      h("div", { class: "pair" }, field("Width", wIn), field("Height", hIn)),
      h("label", { class: "tog" }, lock, "keep its shape"),
      h("div", { class: "row" },
        car ? h("button", { type: "button", class: "small", onclick: () => fit(car[0], car[1]) }, `Fit the car's size (${car[0]}×${car[1]})`) : null,
        h("button", { type: "button", class: "small", onclick: () => src && fit(src.naturalWidth > 480 || src.naturalHeight > 480 ? 480 : src.naturalWidth, src.naturalWidth > 480 || src.naturalHeight > 480 ? 480 : src.naturalHeight) }, "Original size")))),
    info,
    h("div", { class: "note" }, "A car picture the same size as the built-in one puts its wheels where the tyre numbers already are. Any size works - drag and resize the numbers to suit."),
  ], [["Cancel", null], ["Add", () => {
    if (!src) { toast("Choose an image file first.", true); return false; }
    const name = nameIn.value.trim();
    if (!/^[a-z0-9_]{1,24}$/.test(name)) { toast("Name: 1-24 of a-z, 0-9 and _.", true); return false; }
    if (name === CAR) { toast("That name is the built-in car - pick another.", true); return false; }
    const w = Math.round(+wIn.value), hh = Math.round(+hIn.value);
    if (!(w >= 1 && w <= 480 && hh >= 1 && hh <= 480)) { toast("Width and height: 1-480 px.", true); return false; }
    const c = document.createElement("canvas"); c.width = w; c.height = hh;
    const cx = c.getContext("2d"); cx.imageSmoothingQuality = "high"; cx.drawImage(src, 0, 0, w, hh);
    const bytes = toScreenPixels(cx.getImageData(0, 0, w, hh));
    if (!S.doc.images) S.doc.images = {};
    const replacing = !!S.doc.images[name];
    S.doc.images[name] = { w, h: hh, data: b64(bytes) };
    S.lastImage = name;
    if (g) g.image = name;
    changed();
    toast(replacing ? `Replaced “${esc(name)}” - every image gauge showing it uses the new one.` : `Added “${esc(name)}”.`);
  }, "primary"]]);
}
function renderImageProps(P, g) {
  gaugeHead(P, "Image");
  const imgs = S.doc.images || {}, used = imagesUsed(), car = PV.carSize();
  const choice = (name, label, thumb) => h("button", { type: "button", class: "imgpick" + (g.image === name ? " on" : ""), title: name,
    onclick: () => { g.image = name; S.lastImage = name; changed(); } }, thumb || h("span", { class: "thumb" }), h("span", {}, label));
  const items = [];
  if (car) items.push(choice(CAR, `370Z car (built in) ${car[0]}×${car[1]}`, null));
  for (const [n, im] of Object.entries(imgs)) items.push(choice(n, `${n} ${im.w}×${im.h}`, thumbCanvas(im)));
  if (!M.IMAGES[g.image]) items.push(h("div", { class: "warnbox" }, `“${g.image}” isn't in this layout - pick another or upload it.`));
  P.append(section("image", "Image", null,
    h("div", { class: "imglist" }, ...items),
    h("div", { class: "row" }, h("button", { class: "small primary", onclick: () => uploadImage(g) }, "Upload an image…"),
      imgs[g.image] ? h("button", { class: "small", onclick: () => uploadImage(g), title: "Upload with the same name to replace it" }, "Replace this one…") : null),
    h("div", { class: "pair" }, field("Centre x", ...numCtl(g, "x", { min: -100, max: 580, width: 72 })), field("y", ...numCtl(g, "y", { min: -100, max: 580, width: 72 }))),
    (() => { const cb = h("input", { type: "checkbox" }); cb.checked = g.tint != null;
      cb.addEventListener("change", () => { if (cb.checked) g.tint = "accent"; else delete g.tint; changed(); });
      return field("Tint", h("label", { class: "tog" }, cb, "recolour it (for one-colour icons)")); })(),
    g.tint != null ? field("Tint colour", ...colorCtl(g, "tint")) : null,
    h("div", { class: "note" }, "Images are built into the firmware: each costs width × height × 3 bytes of the update slot (the car is 57 KB).")));
  const unused = Object.keys(imgs).filter(n => !used.has(n));
  if (unused.length) P.append(section("imgclean", "Unused images", String(unused.length),
    h("p", { class: "note" }, `${unused.join(", ")} - kept in the layout file but not shown anywhere, so not exported.`),
    h("button", { class: "small", onclick: () => { for (const n of unused) delete S.doc.images[n]; if (!Object.keys(S.doc.images).length) delete S.doc.images; changed(); } }, "Remove them")));
}

function renderGaugeProps(P, v, g) {
  if (g.type === "line") return renderLineProps(P, g);
  if (g.type === "clock") return renderClockProps(P, g);
  if (g.type === "image") return renderImageProps(P, g);
  const e = M.eff(g);
  const t = g.type;
  const dial = t === "arc" || t === "needle";
  const scaled = dial || t === "bar";
  const ul = M.unitLabel(e, units());
  const head = h("div", { class: "ghead" },
    h("button", { class: "small icon", title: "Back to the view", onclick: () => { S.gi = -1; renderProps(); renderOverlay(); } }, "←"),
    h("h3", {}, (M.TYPE_NAMES[t] || t) + (t === "text" ? "" : " · " + (M.INPUT[g.input]?.label || g.input))),
    h("button", { class: "small", onclick: () => gaugeOp("dup"), title: "Ctrl+D" }, "Duplicate"),
    h("button", { class: "small", onclick: () => gaugeOp("front"), title: "Draw on top" }, "Front"),
    h("button", { class: "small", onclick: () => gaugeOp("back"), title: "Draw underneath" }, "Back"),
    h("button", { class: "small danger", onclick: () => gaugeOp("del"), title: "Delete" }, "Delete"));
  P.append(head);

  // type can be switched between the ones that share a reading
  const typeSel = selectCtl(g, "type", M.TYPES.filter(x => !M.NO_INPUT.includes(x)).map(x => [x, M.TYPE_NAMES[x]]), {
    onset: nt => { g.type = nt; if (nt === "light" && !g.input) g.input = "handbrake"; } });

  if (t !== "text") {
    const inSel = h("select", { style: "width:100%" }, ...inputOptions(false));
    inSel.value = g.input;
    inSel.addEventListener("change", () => { setInput(g, inSel.value); changed(); });
    const rows = [field("Type", typeSel), field("Reads", inSel), h("div", { class: "note" }, inputHint(g.input))];
    const kind = M.INPUT[g.input]?.kind;
    if (t !== "light") {
      const uc = M.unitChoices(g.input);
      if (uc.length > 1) rows.push(field("Units", selectCtl(g, "units", uc.map(u => [u, M.UNIT_NAMES[u] + (u === "auto" ? ` (now ${ul})` : "")]))));
      if (CAN && (kind === "gear" || kind === "num")) rows.push(field("Show as", selectCtl(g, "format", [["number", "Number"], ["gear", "Gear (R, N, 1-6)"]])));
      if (e.format !== "gear") rows.push(field("Decimals", selectCtl(g, "decimals", [0, 1, 2, 3].map(n => [n, n]), { rebuild: false })));
    }
    if (scaled) {
      rows.push(h("div", { class: "pair" },
        field("Scale from", ...numCtl(g, "min", { disp: true, step: "any", width: 76 })),
        field("to", ...numCtl(g, "max", { disp: true, step: "any", width: 76, unit: ul }))));
      if (t !== "needle") {
        const has = g.origin !== undefined && g.origin !== null;
        const cb = h("input", { type: "checkbox" }); cb.checked = has;
        cb.addEventListener("change", () => { if (cb.checked) g.origin = 0; else delete g.origin; changed(); });
        rows.push(field("Fill from", h("label", { class: "tog" }, cb, has ? "" : "the start of the scale"), ...(has ? numCtl(g, "origin", { disp: true, step: "any", unit: ul }) : [])));
        if (has) rows.push(h("div", { class: "note" }, "The fill grows either way from this value - e.g. 0 on a boost gauge that runs from vacuum to boost."));
      }
      if (e.min >= e.max) rows.push(h("div", { class: "warnbox" }, "The scale end must be above its start."));
      // scale marks: evenly spread from the start of the scale, or every N counted from zero so 0 is always marked
      if (dial) {
        const stepOn = Number(e.ticks.step) > 0;
        const ms = h("select", {}, h("option", { value: "0" }, "Even divisions"), h("option", { value: "1" }, "Steps from zero"));
        ms.value = stepOn ? "1" : "0";
        ms.addEventListener("change", () => {
          if (ms.value === "1") {
            const d = Math.abs(M.toDisp(e, e.max, units()) - M.toDisp(e, e.min, units())), raw = d / 6;
            const nice = [0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000].reduce((a, b) => Math.abs(b - raw) < Math.abs(a - raw) ? b : a);
            M.setp(g, "ticks.step", nice);
          } else M.setp(g, "ticks.step", undefined);
          changed();
        });
        rows.push(field("Scale marks", ms));
        if (stepOn) {
          rows.push(field("Step", ...numCtl(g, "ticks.step", { min: 0.01, max: 100000, step: "any", width: 72, unit: M.unitLabel(e, units()) })));
          rows.push(h("div", { class: "note" }, "Marked and numbered every step, counted from zero: -10 to 20 with step 5 gives -10, -5, 0, 5 ... 20. If the screen shows another unit with its own scale (below), give that unit its own step there."));
        } else rows.push(field("Divisions", ...numCtl(g, "ticks.major", { min: 0, max: 20, width: 72 }), h("span", { class: "u" }, "0 = no ticks")));
      }
      // own scale when the screen shows another unit (e.g. 0-8 bar instead of 0-120 psi converted)
      const baseU = { temp: "c", press: "psi", press_kpa: "kpa", speed: "mph" }[M.INPUT[g.input]?.cat];
      for (const u of M.unitChoices(g.input).filter(u => u !== "auto" && u !== baseU)) {
        const sc = (g.by_unit || {})[u];
        const cb = h("input", { type: "checkbox" }); cb.checked = !!sc;
        cb.addEventListener("change", () => {
          if (cb.checked) {
            const ee = M.eff(g), conv = x => round(M.toDisp({ ...ee, units: u }, x, units()), 1);
            M.setp(g, "by_unit." + u, { min: conv(ee.min), max: conv(ee.max), major: ee.ticks.major });
          } else M.setp(g, "by_unit." + u, undefined);
          changed();
        });
        rows.push(field("In " + M.UNIT_NAMES[u], h("label", { class: "tog" }, cb, sc ? "" : "its own scale (else converted)")));
        if (sc) rows.push(h("div", { class: "pair" },
          field("from", ...numCtl(g, `by_unit.${u}.min`, { step: "any", width: 68 })),
          field("to", ...numCtl(g, `by_unit.${u}.max`, { step: "any", width: 68, unit: M.UNIT_NAMES[u] }))),
          dial ? field("divisions", ...numCtl(g, `by_unit.${u}.major`, { min: 0, max: 20, width: 68 })) : null,
          dial ? field("step", ...numCtl(g, `by_unit.${u}.step`, { min: 0, step: "any", width: 68, nullable: true }), h("span", { class: "u" }, "empty = even divisions")) : null);
      }
    }
    P.append(section("reading", "Reading", M.INPUT[g.input]?.key, ...rows));
  }

  // what this gauge draws, all in one place (several gauges sharing a centre
  // would otherwise print the same reading twice)
  if (t !== "text") {
    const chip = (label, isOn, set) => {
      const cb = h("input", { type: "checkbox" }); cb.checked = isOn;
      cb.addEventListener("change", () => { set(cb.checked); changed(); });
      return h("label", {}, cb, label);
    };
    const ee = M.eff(g), chips = [];
    if (t !== "light") chips.push(chip("Reading", ee.number.show, on => M.setp(g, "number.show", on)));
    if (t !== "light") chips.push(chip("Unit", ee.unit.show, on => M.setp(g, "unit.show", on)));
    chips.push(chip("Label", ee.label.show && !!ee.label.text, on => {
      if (on) { M.setp(g, "label.text", undefined); M.setp(g, "label.show", undefined); if (!M.eff(g).label.text) M.setp(g, "label.text", "LABEL"); }
      else M.setp(g, "label.text", "");
    }));
    if (dial) {
      chips.push(chip("Ticks", ee.ticks.major > 0, on => M.setp(g, "ticks.major", on ? 6 : 0)));
      if (ee.ticks.major > 0) chips.push(chip("Scale numbers", ee.ticks.numbers, on => M.setp(g, "ticks.numbers", on)));
    }
    P.append(section("show", "Show", null, h("div", { class: "chips" }, ...chips),
      h("div", { class: "note" }, "Two gauges on the same reading (e.g. a needle over an arc): show the texts on one only.")));
  }

  // position & size
  const pos = [h("div", { class: "pair" }, field("X", ...numCtl(g, "x", { min: 0, max: 480, width: 72 })), field("Y", ...numCtl(g, "y", { min: 0, max: 480, width: 72 })))];
  if (dial) {
    pos.push(h("div", { class: "pair" }, field("Radius", ...numCtl(g, "r", { min: 20, max: 240, width: 72 })), field("Thickness", ...numCtl(g, "width", { min: 2, max: 80, width: 72 }))));
    const dirSel = h("select", {}, h("option", { value: "1" }, "Clockwise"), h("option", { value: "-1" }, "Anticlockwise"));
    dirSel.value = e.sweep < 0 ? "-1" : "1";
    dirSel.addEventListener("change", () => { const s = Math.abs(M.eff(g).sweep); g.sweep = dirSel.value === "-1" ? -s : s; changed(); });
    const swInp = h("input", { type: "number", min: 10, max: 360, step: 5, value: Math.abs(e.sweep), style: "width:72px" });
    const swSet = commit => { let s = Math.max(10, Math.min(360, Number(swInp.value) || 10)); g.sweep = (M.eff(g).sweep < 0 ? -1 : 1) * s; changed({ commit, props: false }); };
    swInp.addEventListener("input", () => swSet(false)); swInp.addEventListener("change", () => swSet(true));
    binds.push(() => { if (document.activeElement !== swInp) swInp.value = Math.abs(M.eff(g).sweep); });
    pos.push(h("div", { class: "pair" }, field("Starts at", ...numCtl(g, "start", { step: 5, width: 72, unit: "°" })), field("Runs", swInp, h("span", { class: "u" }, "°"))));
    pos.push(field("Direction", dirSel));
    pos.push(h("div", { class: "note" }, "Angles: 0° = 12 o'clock, 90° = 3 o'clock. Drag the round handles at the ends of the arc on the screen, the square one to change the radius, the small one for thickness."));
  }
  if (t === "bar") {
    pos.push(h("div", { class: "pair" }, field("Width", ...numCtl(g, "w", { min: 4, max: 480, width: 72 })), field("Height", ...numCtl(g, "h", { min: 4, max: 480, width: 72 }))));
    pos.push(field("Fills", selectCtl(g, "vertical", [["false", "Left to right"], ["true", "Bottom to top"]], {
      onset: val => { const ee = M.eff(g); if (val !== ee.vertical) { g.vertical = val; const w = ee.w, hh = ee.h; g.w = hh; g.h = w; } } })));
  }
  if (t === "light") {
    pos.push(h("div", { class: "pair" }, field("Size", ...numCtl(g, "size", { min: 6, max: 400, width: 72 })), field("Shape", selectCtl(g, "shape", [["circle", "Round"], ["square", "Square"]], { rebuild: false }))));
  }
  P.append(section("pos", "Position & size", "px on the 480×480 screen", ...pos));

  // look
  if (scaled) {
    const look = [];
    look.push(field("Blocks", ...numCtl(g, "segments", { min: 0, max: 60, width: 72 }), h("span", { class: "u" }, "0 = solid")));
    look.push(field("Alert zones", checkCtl(g, "zones", "Show the amber/red ranges on the track")));
    if (dial) {
      look.push(field("Ends", checkCtl(g, "rounded", "Rounded ends on the band and the reading")));
      // a second ring beside the band showing the alert zones (and the reading if wanted)
      const ring = !!(g.ring && g.ring.show !== false);
      const rcb = h("input", { type: "checkbox" }); rcb.checked = ring;
      rcb.addEventListener("change", () => { if (rcb.checked) g.ring = { offset: -26, width: 10, segments: 0, zones: true, fill: false }; else delete g.ring; changed(); });
      look.push(field("Second ring", h("label", { class: "tog" }, rcb, "A thin ring beside the band (the “double line”)")));
      if (ring) {
        look.push(h("div", { class: "pair" }, field("Distance", ...numCtl(g, "ring.offset", { min: -200, max: 200, width: 64 })),
          field("Thickness", ...numCtl(g, "ring.width", { min: 1, max: 80, width: 64 }))));
        look.push(field("Ring blocks", ...numCtl(g, "ring.segments", { min: 0, max: 60, width: 64 }), h("span", { class: "u" }, "0 = solid")));
        look.push(field("Ring shows", checkCtl(g, "ring.zones", "Alert zones"), checkCtl(g, "ring.fill", "The reading")));
        look.push(h("div", { class: "note" }, "Distance is from the middle of the band: negative = inside it, positive = outside. The zones need alert levels."));
      }
      const notch = !!e.gaps;
      look.push(field("Block style", selectCtl(g, "gaps", [["sep", "Separate blocks"], ["notch", "Notches over a solid band"]], {
        onset: val => { if (val === "notch") g.gaps = { width: 6, outer: 18, inner: 18, color: "dial" }; else delete g.gaps; } })));
      binds.pop(); // the select above shows "sep"/"notch", not the stored object
      const bs = look[look.length - 1].querySelector("select"); bs.value = notch ? "notch" : "sep";
      if (notch) {
        look.push(h("div", { class: "pair" }, field("Notch width", ...numCtl(g, "gaps.width", { min: 1, max: 20, width: 64 })),
          field("Colour", ...colorCtl(g, "gaps.color"))));
        look.push(h("div", { class: "pair" }, field("Out past band", ...numCtl(g, "gaps.outer", { min: -100, max: 100, width: 64 })),
          field("Into band", ...numCtl(g, "gaps.inner", { min: -100, max: 200, width: 64 }))));
      }
      look.push(field("Minor marks", ...numCtl(g, "ticks.minor", { min: 1, max: 10, width: 72 }), h("span", { class: "u" }, "per division / step")));
      look.push(field("Scale numbers", checkCtl(g, "ticks.numbers", "Show"), fontCtl(g, "ticks.font")));
      look.push(h("div", { class: "pair" }, field("Tick gap", ...numCtl(g, "ticks.inset", { min: -60, max: 120, width: 72 })), field("Tick length", ...numCtl(g, "ticks.length", { min: 2, max: 60, width: 72 }))));
      look.push(field("Minor length", ...numCtl(g, "ticks.minor_length", { min: 1, max: 60, width: 72, nullable: true }), h("span", { class: "u" }, "empty = 60 % of the tick length")));
      look.push(h("div", { class: "note" }, "Divisions or steps are set under Scale. Minor = small marks between the numbered ones."));
    }
    if (t === "needle") {
      look.push(h("div", { class: "pair" }, field("Needle length", ...numCtl(g, "needle.length", { min: 10, max: 240, width: 72 })), field("Width", ...numCtl(g, "needle.width", { min: 1, max: 30, width: 72 }))));
      look.push(h("div", { class: "pair" }, field("Tail", ...numCtl(g, "needle.tail", { min: 0, max: 120, width: 72 })), field("Hub", ...numCtl(g, "needle.hub", { min: 0, max: 80, width: 72 }))));
    }
    P.append(section("look", "Look", null, ...look));
  }
  // reference marks across the band (zero on a boost gauge, stoich on AFR...)
  if (dial) {
    const marks = e.marks || [];
    const rows = marks.map((m, i) => h("div", { class: "markrow" },
      field("At", ...numCtl(g, `marks.${i}.value`, { disp: true, step: "any", width: 70, unit: ul }),
        h("button", { class: "small danger", onclick: () => { g.marks.splice(i, 1); if (!g.marks.length) delete g.marks; changed(); } }, "Remove")),
      h("div", { class: "pair" }, field("Colour", ...colorCtl(g, `marks.${i}.color`)), field("Width", ...numCtl(g, `marks.${i}.width`, { min: 1, max: 20, width: 56 }))),
      h("div", { class: "pair" }, field("Out past band", ...numCtl(g, `marks.${i}.outer`, { min: -100, max: 100, width: 56 })), field("Into dial", ...numCtl(g, `marks.${i}.inner`, { min: -100, max: 200, width: 56 })))));
    P.append(section("marks", "Marks", marks.length ? String(marks.length) : "none", ...rows,
      marks.length < 4 ? h("button", { class: "small", onclick: () => {
        const ee = M.eff(g); (g.marks = g.marks || []).push({ value: round((ee.min + ee.max) / 2, 1), color: "text", width: 4, outer: 16, inner: 46 }); changed(); } }, "+ Add a mark") : null,
      h("div", { class: "note" }, "A line across the band at a value - like the zero line on boost or stoich on AFR.")));
  }
  if (t === "light") {
    const kind = M.INPUT[g.input]?.kind, lcat = M.INPUT[g.input]?.cat;
    // The value is typed in the light's own unit: the reading's usual one (PSI, °C, mph) unless one is chosen here.
    const baseU = { temp: "c", press: "psi", press_kpa: "kpa", speed: "mph" }[lcat];
    const lightUnits = M.unitChoices(g.input).filter(u => u !== "auto");
    const curU = () => { const u = M.eff(g).units; return u === "auto" || !lightUnits.includes(u) ? baseU : u; };
    const unitSel = lightUnits.length > 1 && baseU ? selectCtl(g, "units",
      [["auto", M.UNIT_NAMES[baseU] + " (usual)"], ...lightUnits.filter(u => u !== baseU).map(u => [u, M.UNIT_NAMES[u]])],
      { rebuild: false, onset: nu => {                      // keep the same real threshold when the unit changes
        const from = curU(), to = nu === "auto" ? baseU : nu, v = Number(M.eff(g).on.value);
        if (from !== to && Number.isFinite(v)) {
          const base = M.fromDisp({ input: g.input, units: from }, v, {});
          M.setp(g, "on.value", round(M.toDisp({ input: g.input, units: to }, base, {}), to === "bar" ? 3 : 1));
        }
        M.setp(g, "units", nu);
      }, rebuildAfter: true }) : null;
    P.append(section("light", "Light", null,
      field("Lit when", selectCtl(g, "on.op", [">=", ">", "<=", "<", "==", "!="].map(o => [o, "value " + o]), { rebuild: false }),
        ...numCtl(g, "on.value", { step: "any", width: 76 })),
      unitSel ? field("Value in", unitSel) : null,
      h("div", { class: "note" }, kind === "bool" ? "For an on/off input, “value >= 1” = lit when on." : inputHint(g.input) + ". No reading = never lit."),
      field("Blink", checkCtl(g, "blink", "Blink while lit")),
      field("Lit colour", ...colorCtl(g, "on_color")),
      field("Off colour", ...colorCtl(g, "off_color"))));
  }

  // texts
  const part = (p, name, withText) => {
    const rows = [];
    if (withText) {
      rows.push(field(name, textCtl(g, p + ".text", { placeholder: "(none)", onset: val => { if (!g[p]) g[p] = {}; g[p].text = val; delete g[p].show; } })));
    } else rows.push(field(name, checkCtl(g, p + ".show", "Show", { rebuild: true })));
    if (M.eff(g)[p].show) rows.push(h("div", { class: "pair" }, field("Size", fontCtl(g, p + ".font")),
      h("span", { class: "ctl", style: "display:flex;gap:6px;align-items:center" }, h("span", { class: "u" }, "x"), ...numCtl(g, p + ".dx", { min: -480, max: 480, width: 64 }), h("span", { class: "u" }, "y"), ...numCtl(g, p + ".dy", { min: -480, max: 480, width: 64 }))));
    return rows;
  };
  if (t === "text") {
    P.append(section("texts", "Text", null,
      field("Text", textCtl(g, "text", { onset: val => { g.text = val; } })),
      field("Size", fontCtl(g, "font")),
      field("Colour", ...colorCtl(g, "color"))));
  } else {
    const rows = [];
    if (t !== "light") rows.push(...part("number", "Reading", false));
    if (t !== "light" && t !== "clock" && M.eff(g).number.show) {
      // seven-segment readout (like a glowing LED / VFD display)
      const seg = M.eff(g).number.style === "seg7";
      const st = h("select", {}, h("option", { value: "" }, "Normal text"), h("option", { value: "seg7" }, "Seven-segment, glowing"));
      st.value = seg ? "seg7" : "";
      st.addEventListener("change", () => {
        if (st.value === "seg7") { g.number = { ...(g.number || {}), style: "seg7", height: (g.number && g.number.height) || 64, digits: (g.number && g.number.digits) ?? 0, slant: 8, glow: 0.7, core: true }; }
        else if (g.number) { for (const k of ["style", "height", "digits", "slant", "thickness", "ghost", "glow", "core"]) delete g.number[k]; }
        changed();
      });
      rows.push(field("Reading style", st));
      if (t === "digital") rows.push(field("Digits stay put", checkCtl(g, "number.fixed", "Width set by the biggest number on the scale, lined up on the decimal point")));
      if (seg) {
        rows.push(field("Digit height", ...numCtl(g, "number.height", { min: 16, max: 200, width: 64 }), h("span", { class: "u" }, "px")));
        rows.push(field("Fixed width", ...numCtl(g, "number.digits", { min: 0, max: 8, width: 64 }), h("span", { class: "u" }, "digits (0 = from the scale)")));
        rows.push(field("Lean", ...rangeCtl(g, "number.slant", 0, 20, 1, v => v + "°")));
        rows.push(field("Stroke", ...numCtl(g, "number.thickness", { min: 0, max: 40, width: 64 }), h("span", { class: "u" }, "px (0 = auto)")));
        rows.push(field("Unlit segments", ...rangeCtl(g, "number.ghost", 0, 0.6, 0.02, v => Math.round(v * 100) + " %")));
        rows.push(field("Glow", ...rangeCtl(g, "number.glow", 0, 1, 0.05, v => Math.round(v * 100) + " %")));
        rows.push(field("Bright centre", checkCtl(g, "number.core", "Lit segments are brighter in the middle")));
        rows.push(h("div", { class: "note" }, "Shows digits, minus, point and a few letters. Unlit segments only show when Fixed width is set (e.g. 3 digits); with 0 the width comes from the scale. The colour is the Reading colour under Colours; try a light blue like #8fd0ff. “Size” above is not used by this style." ));
      }
    }
    rows.push(...part("label", "Label", true));
    rows.push(h("div", { class: "note" }, "In the label, {u} becomes the unit (°C, PSI…) and {U} the same without the degree sign."));
    if (t !== "light") rows.push(...part("unit", "Unit", false));
    rows.push(h("div", { class: "note" }, "x / y are offsets from the gauge centre. With the gauge selected you can drag each text on the screen."));
    P.append(section("texts", "Texts", null, ...rows));
  }
  if (t !== "text" && t !== "light") {
    // status note: a word under the reading that depends on the alert state
    const ne = e.note, on = !!ne.show;
    const cb = h("input", { type: "checkbox" }); cb.checked = on;
    cb.addEventListener("change", () => {
      if (cb.checked) g.note = { ...(g.note || {}), show: true, dy: (g.note && g.note.dy) ?? 104 };
      else if (g.note) g.note.show = false;
      changed();
    });
    const rows = [field("Status note", h("label", { class: "tog" }, cb, "Show a word for each state"))];
    if (on) {
      for (const [k, name] of M.NOTE_STATES) rows.push(field(name, textCtl(g, "note." + k, { max: 15, placeholder: "(nothing)" })));
      rows.push(h("div", { class: "pair" }, field("Size", fontCtl(g, "note.font")),
        h("span", { class: "ctl", style: "display:flex;gap:6px;align-items:center" }, h("span", { class: "u" }, "x"), ...numCtl(g, "note.dx", { min: -480, max: 480, width: 64 }), h("span", { class: "u" }, "y"), ...numCtl(g, "note.dy", { min: -480, max: 480, width: 64 }))));
      rows.push(h("div", { class: "note" }, "“Not judged” is when the alert's condition is off (engine not running, throttle below the limit…)."));
    }
    // peak label: "peak 12.3", the highest reading since the screen started
    const pk = !!g.peak_label && g.peak_label.show !== false;
    const pcb = h("input", { type: "checkbox" }); pcb.checked = pk;
    pcb.addEventListener("change", () => { if (pcb.checked) g.peak_label = { text: "peak ", font: 20, dx: 0, dy: (e.note.show ? 130 : 104), ...(g.peak_label || {}), show: true }; else delete g.peak_label; changed(); });
    rows.push(field("Peak text", h("label", { class: "tog" }, pcb, "Highest reading since the screen started")));
    if (pk) {
      rows.push(field("Text before it", textCtl(g, "peak_label.text", { max: 15, placeholder: "(none)", onset: val => { g.peak_label.text = val; } })));
      rows.push(field("Starts from", ...numCtl(g, "peak_label.start", { disp: true, step: "any", width: 70, nullable: true, unit: ul }),
        h("span", { class: "u" }, "empty = first reading")));
      rows.push(h("div", { class: "pair" }, field("Size", fontCtl(g, "peak_label.font")),
        h("span", { class: "ctl", style: "display:flex;gap:6px;align-items:center" }, h("span", { class: "u" }, "x"), ...numCtl(g, "peak_label.dx", { min: -480, max: 480, width: 64 }), h("span", { class: "u" }, "y"), ...numCtl(g, "peak_label.dy", { min: -480, max: 480, width: 64 }))));
    }
    P.append(section("extra", "Status note & peak", (on ? "note" : "") + (pk ? (on ? " + " : "") + "peak" : "") || "off", ...rows));
  }

  // colours
  if (t !== "text") {
    const rows = [];
    if (t !== "digital" && t !== "light") rows.push(field(t === "needle" ? "Alert colour" : "Fill", ...colorCtl(g, "colors.fill")));
    if (t === "needle") rows.push(field("Needle", ...colorCtl(g, "colors.needle")));
    if (t !== "digital" && t !== "light") rows.push(field(t === "needle" ? "Track" : "Unlit / track", ...colorCtl(g, "colors.track")));
    if (t === "arc" || t === "bar") {
      // a real gauge look: unlit blocks as a dim shade of their own colour, lit ones in their zone's colour
      const sh = e.unlit_shade || 0;
      const us = h("select", {}, h("option", { value: "0" }, "Plain track colour"), h("option", { value: "1" }, "Dim shade of their lit colour"));
      us.value = sh > 0 ? "1" : "0";
      us.addEventListener("change", () => { if (us.value === "1") g.unlit_shade = 0.22; else delete g.unlit_shade; changed(); });
      rows.push(field("Unlit blocks", us));
      if (sh > 0) rows.push(field("Shade", ...rangeCtl(g, "unlit_shade", 0.05, 0.6, 0.01, v => Math.round(v * 100) + " %")));
      rows.push(field("Lit blocks", selectCtl(g, "zone_fill", [["false", "One colour (all amber/red on an alert)"], ["true", "Each in its zone's colour (green, amber, red)"]])));
      if ((sh > 0 || e.zone_fill) && !Object.values(e.alerts).some(x => Number.isFinite(x)))
        rows.push(h("div", { class: "note" }, "Set alert levels (below) to get amber and red zones on the scale."));
      {
        // lit-lamp look: a soft halo round the lit blocks and a brighter centre, like an illuminated analogue gauge
        rows.push(field("Glow", ...rangeCtl(g, "glow", 0, 1, 0.05, v => v ? Math.round(v * 100) + " %" : "off")));
        rows.push(field("Bright centre", checkCtl(g, "core", "Lit blocks are brighter in the middle")));
        rows.push(field("Lamp look", h("button", { class: "small", type: "button", title: "Blocks with dim unlit shades, a glow and a bright centre",
          onclick: () => { if (!g.segments) g.segments = 24; g.unlit_shade = 0.14; g.glow = 0.7; g.core = true; changed(); } }, "Apply lit-lamp look")));
      }
    }
    if (t !== "light") rows.push(field("Reading", ...colorCtl(g, "colors.text")));
    if (t !== "light" && e.unit.show) rows.push(field("Unit", ...colorCtl(g, "colors.unit")));
    rows.push(field("Label", ...colorCtl(g, "colors.label")));
    if (dial && e.ticks.major) {
      if (e.ticks.numbers) rows.push(field("Scale numbers", ...colorCtl(g, "colors.scale")));
      rows.push(field("Major ticks", ...colorCtl(g, "colors.ticks")));
      if (e.ticks.minor > 1) rows.push(field("Minor ticks", ...colorCtl(g, "colors.minor_ticks")));
    }
    rows.push(h("div", { class: "note" }, "Theme colours follow each screen's own colour setting (try the swatches above the screen). Amber and red alerts override the fill and reading colour."));
    P.append(section("colours", "Colours", null, ...rows));
  }

  // alerts
  if (t !== "text" && t !== "light") {
    const kind = M.INPUT[g.input]?.kind;
    const lvRow = (key, label, cls) => {
      const has = Number.isFinite(e.alerts[key]);
      const cb = h("input", { type: "checkbox" }); cb.checked = has;
      cb.addEventListener("change", () => {
        if (cb.checked) {
          const ee = M.eff(g), span = ee.max - ee.min;
          const guess = { low_crit: ee.min + span * 0.1, low_warn: ee.min + span * 0.2, high_warn: ee.min + span * 0.8, high_crit: ee.min + span * 0.9 }[key];
          M.setp(g, "alerts." + key, round(guess, ee.decimals));
        } else M.setp(g, "alerts." + key, undefined);
        changed();
      });
      const row = h("div", { class: "alertrow" }, cb, h("span", { class: "tag " + cls }, label),
        has ? h("span", { class: "ctl", style: "display:flex;gap:6px;align-items:center" }, ...numCtl(g, "alerts." + key, { disp: true, step: "any", width: 90, unit: ul })) : h("span", { class: "u" }, "off"));
      if (!has) return row;
      // colour of this level's zone on the scale, and of the fill / reading while in it
      return h("div", { class: "alertblock" }, row,
        h("div", { class: "alertcol" }, h("span", { class: "u" }, "colour"), ...colorCtl(g, "alerts.colors." + key)));
    };
    const a = e.alerts;
    const order = [a.low_crit, a.low_warn, a.high_warn, a.high_crit].filter(Number.isFinite);
    const bad = order.some((x, i) => i && x < order[i - 1]);
    const any = order.length > 0 || (CAN && !!a.status);
    const statusInputs = CAN ? M.INPUTS.filter(r => r.kind === "oil_status") : [];
    const stSel = h("select", {}, h("option", { value: "" }, "No - use the levels above"),
      ...statusInputs.map(r => h("option", { value: r.key }, `${r.label || r.key} (${r.key})`)));
    stSel.value = a.status || "";
    stSel.addEventListener("change", () => { M.setp(g, "alerts.status", stSel.value || undefined); changed(); });
    // only judge when another reading meets a condition (e.g. AFR under load)
    const w = a.when;
    const wcb = h("input", { type: "checkbox" }); wcb.checked = !!w;
    wcb.addEventListener("change", () => { if (wcb.checked) M.setp(g, "alerts.when", { input: "throttle_pct", op: ">=", value: 50 }); else M.setp(g, "alerts.when", undefined); changed(); });
    const whenRows = [field("Only when", h("label", { class: "tog" }, wcb, w ? "" : "always judged"))];
    if (w) {
      const wi = h("select", { style: "max-width:170px" }, ...inputOptions(false)); wi.value = w.input;
      wi.addEventListener("change", () => { M.setp(g, "alerts.when.input", wi.value); changed(); });
      whenRows.push(field("", wi, selectCtl(g, "alerts.when.op", [">=", ">", "<=", "<", "==", "!="].map(o => [o, o]), { rebuild: false }),
        ...numCtl(g, "alerts.when.value", { step: "any", width: 64 })));
    }
    P.append(section("alerts", "Alerts", any ? "on" : "none",
      lvRow("low_crit", "Low red ≤", "lvl-crit"), lvRow("low_warn", "Low amber ≤", "lvl-warn"),
      lvRow("high_warn", "High amber ≥", "lvl-warn"), lvRow("high_crit", "High red ≥", "lvl-crit"),
      any ? h("div", { class: "note" }, "Each level has its own colour: it paints that part of the scale, and the fill and reading while the value is in it. For a blue vacuum zone on a boost gauge, set Low amber ≤ 0 and give it a blue.") : null,
      bad ? h("div", { class: "warnbox" }, "These should go up in order: low red, low amber, high amber, high red.") : null,
      kind && kind !== "num" ? h("div", { class: "note" }, inputHint(g.input)) : null,
      statusInputs.length && M.INPUT[g.input]?.cat === "press" ? field("Brain's verdict", stSel) : null,
      a.status ? h("div", { class: "note" }, "While the brain sends its verdict (oil pressure judged against rpm and oil temperature), it sets amber/red; the levels above are used when it doesn't.") : null,
      any ? field("Colour reading", checkCtl(g, "alerts.number", "Turn the number amber/red too (not just the fill)")) : null,
      any ? field("Only running", checkCtl(g, "alerts.when_running", "Only with the engine running (rpm ≥ 400)")) : null,
      any ? h("div", { class: "note" }, "Use for oil pressure - it's legitimately 0 with the engine off.") : null,
      ...(any ? whenRows : []),
      any ? field("Flash", checkCtl(g, "alerts.flash", "Blink after 0.5 s in red")) : null,
      any ? field("Take over", checkCtl(g, "alerts.takeover", "Jump to this view after 1.5 s in red")) : null,
      any ? h("div", { class: "note" }, "The built-in oil alert always wins, then tyres. A swipe away is respected for 5 s. No reading never alerts.") : null));
  }

  // animation
  if (scaled) {
    P.append(section("anim", "Animation", null,
      field("Smoothing", ...rangeCtl(g, "anim.smooth", 0, 0.95, 0.05, v => v === 0 ? "none" : v.toFixed(2))),
      h("div", { class: "note" }, "0 = jumps straight to each reading; higher = calmer, slower movement. The printed number is never smoothed."),
      field("Opening sweep", checkCtl(g, "anim.sweep", "Sweep to full scale and back when the view opens")),
      field("Peak hold", checkCtl(g, "anim.peak", "Mark the highest reading", { rebuild: true })),
      M.eff(g).anim.peak ? field("Hold for", ...numCtl(g, "anim.peak_s", { min: 0.5, max: 60, step: 0.5, width: 72, unit: "s" })) : null));
  }
}

// ================================================================ canvas overlay ===
const svg = $("#overlay");
function scale() { return $("#screenBox").getBoundingClientRect().width / 480; }
function toScreen(ev) {
  const r = $("#screenBox").getBoundingClientRect();
  return [(ev.clientX - r.left) * 480 / r.width, (ev.clientY - r.top) * 480 / r.height];
}
function gaugeValue(g) { const i = M.INPUT[g.input]?.idx; return i === undefined ? NaN : S.kv[i]; }
function boxesOf(g) { return M.partBoxes(M.eff(g), (t, f) => PV.measure(t, f), units(), gaugeValue(g)); }
function inBox(b, x, y, pad = 3) { return Math.abs(x - b.x) <= b.w / 2 + pad && Math.abs(y - b.y) <= b.h / 2 + pad; }

function handlesOf(g) {
  const e = M.eff(g), out = [];
  if (e.type === "arc" || e.type === "needle") {
    const [a0, a1] = [e.start, e.start + e.sweep];
    const mid = e.start + e.sweep / 2;
    out.push({ k: "start", pt: M.polar(e.x, e.y, a0, e.r) });
    out.push({ k: "end", pt: M.polar(e.x, e.y, a1, e.r) });
    out.push({ k: "r", pt: M.polar(e.x, e.y, mid, e.r), sq: true });
    out.push({ k: "w", pt: M.polar(e.x, e.y, mid, e.r + e.width / 2), small: true });
  } else if (e.type === "line") { out.push({ k: "p1", pt: [e.x, e.y] }); out.push({ k: "p2", pt: [e.x2, e.y2] }); }
  else if (e.type === "bar") out.push({ k: "wh", pt: [e.x + e.w / 2, e.y + e.h / 2], sq: true });
  else if (e.type === "light") out.push({ k: "size", pt: [e.x + e.size / 2, e.y + e.size / 2], sq: true });
  else if (e.type === "clock") {
    if (M.clockAnalog(e, units())) out.push({ k: "r", pt: M.polar(e.x, e.y, 135, e.r), sq: true });
    else { const k = e.digit_height / 132; out.push({ k: "dh", pt: [e.x + e.number.dx + 177 * k, e.y + e.number.dy + e.digit_height / 2], sq: true }); }
  }
  return out;
}
// what is under (x, y): {gi, part: 'body'|'number'|'label'|'unit'|handle key}
function distToSeg(px, py, x0, y0, x1, y1) {
  const dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
  const t = l2 ? Math.max(0, Math.min(1, ((px - x0) * dx + (py - y0) * dy) / l2)) : 0;
  return Math.hypot(px - (x0 + t * dx), py - (y0 + t * dy));
}
function hitTest(x, y) {
  const v = view(); if (!v) return null;
  const tol = (COARSE.matches ? 28 : 16) / scale();
  const sg = gauge();
  if (sg) {
    for (const hd of handlesOf(sg)) if (Math.hypot(hd.pt[0] - x, hd.pt[1] - y) <= tol) return { gi: S.gi, part: "h:" + hd.k };
    for (const b of boxesOf(sg)) {
      if (b.part === "body" || ((sg.type === "digital" || sg.type === "clock") && b.part === "number")) continue;   // a number gauge (or digital clock) moves by its number
      if (inBox(b, x, y)) return { gi: S.gi, part: b.part };
    }
  }
  for (let i = v.gauges.length - 1; i >= 0; i--) {
    const g = v.gauges[i], e = M.eff(g);
    for (const b of boxesOf(g)) if (inBox(b, x, y)) return { gi: i, part: "body" };
    const sh = M.bodyShape(e, units());
    if (!sh) continue;
    if (sh.kind === "circle" && Math.hypot(x - sh.x, y - sh.y) <= sh.r) return { gi: i, part: "body" };
    if (sh.kind === "rect" && x >= sh.x - 3 && x <= sh.x + sh.w + 3 && y >= sh.y - 3 && y <= sh.y + sh.h + 3) return { gi: i, part: "body" };
    if (sh.kind === "line" && distToSeg(x, y, sh.x, sh.y, sh.x2, sh.y2) <= sh.w / 2 + 6) return { gi: i, part: "body" };
    if (sh.kind === "dial") {
      const d = Math.hypot(x - e.x, y - e.y);
      if (Math.abs(d - e.r) <= e.width / 2 + 5 && M.onSweep(e, M.angleOf(e.x, e.y, x, y))) return { gi: i, part: "body" };
    }
  }
  // inside a dial (not on its band): the smallest dial around the point
  let best = null;
  v.gauges.forEach((g, i) => {
    const e = M.eff(g);
    if (e.type !== "arc" && e.type !== "needle") return;
    const d = Math.hypot(x - e.x, y - e.y);
    if (d <= e.r + e.width / 2 && (!best || e.r < best.r)) best = { gi: i, part: "body", r: e.r };
  });
  return best;
}

function renderOverlay() {
  const v = view();
  let s = `<circle cx="240" cy="240" r="239" fill="none" stroke="#2a2c34" stroke-width="2" vector-effect="non-scaling-stroke"/>`;
  if (isBuiltin(v)) {
    const info = builtinInfo(v.builtin);
    s += `<text x="240" y="205" text-anchor="middle" class="bi-t1">BUILT-IN VIEW</text>`
       + `<text x="240" y="245" text-anchor="middle" class="bi-t2">${esc(v.name || (info && cap(info.name)) || v.builtin)}</text>`
       + `<text x="240" y="280" text-anchor="middle" class="bi-t3">drawn by the screen code</text>`
       + (v.enabled === false ? `<text x="240" y="320" text-anchor="middle" class="bi-off">OFF ON EVERY SCREEN</text>` : "");
    svg.innerHTML = s; return;
  }
  if (v) {
    const outline = (g, cls) => {
      const e = M.eff(g), sh = M.bodyShape(e, units());
      if (sh && sh.kind === "dial") {
        const [a0, a1] = M.sweepRange(e);
        s += `<path class="${cls}" d="${M.bandPath(e.x, e.y, e.r, e.width, a0, a1)}"/>`;
      } else if (sh && sh.kind === "line") s += `<line class="${cls}" x1="${sh.x}" y1="${sh.y}" x2="${sh.x2}" y2="${sh.y2}" stroke-width="${sh.w + 8}"/>`;
      else if (sh && sh.kind === "circle") s += `<circle class="${cls}" cx="${sh.x}" cy="${sh.y}" r="${sh.r}"/>`;
      else if (sh) s += `<rect class="${cls}" x="${sh.x - 2}" y="${sh.y - 2}" width="${sh.w + 4}" height="${sh.h + 4}" rx="${sh.round ? (sh.w + 4) / 2 : 3}"/>`;
      return e;
    };
    if (S.hover >= 0 && S.hover !== S.gi && v.gauges[S.hover]) {
      const g = v.gauges[S.hover]; outline(g, "hov");
      for (const b of boxesOf(g)) s += `<rect class="hov" x="${b.x - b.w / 2 - 2}" y="${b.y - b.h / 2 - 2}" width="${b.w + 4}" height="${b.h + 4}" rx="3"/>`;
    }
    const g = gauge();
    if (g) {
      const e = outline(g, "selo");
      for (const b of boxesOf(g)) {
        const on = S.drag && S.drag.part === b.part ? " ondrag" : "";
        s += `<rect class="${b.part === "body" ? "selo" : "part" + on}" x="${b.x - b.w / 2 - 3}" y="${b.y - b.h / 2 - 3}" width="${b.w + 6}" height="${b.h + 6}" rx="3"/>`;
      }
      const k = 1 / scale();
      for (const hd of handlesOf(g)) {
        const r = (COARSE.matches ? (hd.small ? 10 : 14) : (hd.small ? 6 : 9)) * k;
        s += hd.sq ? `<rect class="h sq" x="${hd.pt[0] - r}" y="${hd.pt[1] - r}" width="${2 * r}" height="${2 * r}" rx="${2 * k}"/>`
                   : `<circle class="h" cx="${hd.pt[0]}" cy="${hd.pt[1]}" r="${r}"/>`;
      }
      for (const gd of S.guides) s += gd[0] === "v" ? `<line class="guide" x1="${gd[1]}" y1="0" x2="${gd[1]}" y2="480"/>` : `<line class="guide" x1="0" y1="${gd[1]}" x2="480" y2="${gd[1]}"/>`;
      if (S.dragNote) s += `<text class="hl" x="${Math.min(400, e.x + 12)}" y="${Math.max(16, e.y - 12)}" font-size="${12 * k}">${esc(S.dragNote)}</text>`;
    }
  }
  svg.innerHTML = s;
}

// snap a coordinate: grid of 5 px and the centre / gauge lines
function snapXY(x, y, refX = 240, refY = 240) {
  S.guides = [];
  if (!S.snap || S.noSnap) return [Math.round(x), Math.round(y)];
  let sx = Math.round(x / 5) * 5, sy = Math.round(y / 5) * 5;
  if (Math.abs(x - refX) < 8) { sx = refX; S.guides.push(["v", refX]); }
  if (Math.abs(y - refY) < 8) { sy = refY; S.guides.push(["h", refY]); }
  return [sx, sy];
}

// ---- alignment guides: line a dragged item up with everything else ---------
// The lines it can sit on: the screen centre, and for every other gauge its
// centre, edges (band / box / line ends) and text boxes (centre and edges).
function shapeRefs(e) {
  const sh = M.bodyShape(e, units());
  if (!sh) return { xs: [e.x], ys: [e.y] };
  if (sh.kind === "circle") return { xs: [e.x - sh.r, e.x, e.x + sh.r], ys: [e.y - sh.r, e.y, e.y + sh.r] };
  if (sh.kind === "dial") { const R = sh.r + sh.w / 2; return { xs: [e.x - R, e.x, e.x + R], ys: [e.y - R, e.y, e.y + R] }; }
  if (sh.kind === "line") return { xs: [sh.x, (sh.x + sh.x2) / 2, sh.x2], ys: [sh.y, (sh.y + sh.y2) / 2, sh.y2] };
  return { xs: [sh.x, sh.x + sh.w / 2, sh.x + sh.w], ys: [sh.y, sh.y + sh.h / 2, sh.y + sh.h] };
}
const boxRefs = b => ({ xs: [b.x - b.w / 2, b.x, b.x + b.w / 2], ys: [b.y - b.h / 2, b.y, b.y + b.h / 2] });
function addRefs(a, r) { a.xs.push(...r.xs); a.ys.push(...r.ys); return a; }
// part: "body" (the whole gauge moves), a text part ("label"...), or "p1"/"p2" (a line end)
function alignTargets(gi, part) {
  const t = { xs: [240], ys: [240] };
  view().gauges.forEach((g, i) => {
    const e = M.eff(g);
    if (i !== gi) addRefs(t, shapeRefs(e));
    else if (part === "p1") { t.xs.push(e.x2); t.ys.push(e.y2); }          // the other end: square lines
    else if (part === "p2") { t.xs.push(e.x); t.ys.push(e.y); }
    else if (part !== "body") { t.xs.push(e.x); t.ys.push(e.y); }        // its own centre
    if (e.type === "line") return;
    for (const b of boxesOf(g)) {
      if (i === gi && (part === "body" || b.part === part)) continue;     // moves with the drag
      addRefs(t, boxRefs(b));
    }
  });
  return t;
}
// The dragged item's own reference lines, where it was when the drag started.
// main = the index of its centre in xs / ys.
function dragRefs(d) {
  const e = d.e0;
  if (d.part === "body") {
    const r = shapeRefs(e);
    r.main = r.xs.length === 3 ? 1 : 0;
    if (e.type !== "line") for (const b of d.boxes0) addRefs(r, boxRefs(b));
    return r;
  }
  if (d.part === "h:p1") return { xs: [e.x], ys: [e.y], main: 0 };
  if (d.part === "h:p2") return { xs: [e.x2], ys: [e.y2], main: 0 };
  const b = d.boxes0.find(b => b.part === d.part);
  return b ? { ...boxRefs(b), main: 1 } : { xs: [], ys: [], main: 0 };
}
// Move by (dx, dy), pulled onto the nearest line within a few screen pixels;
// on an axis with nothing to line up with, the anchor goes onto the 5 px grid.
function alignMove(d, dx, dy, anchor) {
  S.guides = [];
  if (!S.snap || S.noSnap) return [Math.round(dx), Math.round(dy)];
  const tol = Math.max(2, 7 / scale());
  if (!d.targets) d.targets = alignTargets(S.gi, d.part.replace(/^h:/, ""));
  if (!d.refs) d.refs = dragRefs(d);
  // the item's own anchor (its centre) wins a near-tie, so centres line up exactly
  const best = (refs, off, targets) => {
    let b = null, bs = Infinity;
    refs.forEach((r, i) => { for (const t of targets) {
      const c = t - (r + off), sc = Math.abs(c) + (i === d.refs.main ? 0 : 1);
      if (Math.abs(c) <= tol && sc < bs) { b = c; bs = sc; }
    } });
    return b;
  };
  const cx = best(d.refs.xs, dx, d.targets.xs), cy = best(d.refs.ys, dy, d.targets.ys);
  let ndx = cx !== null ? dx + cx : Math.round((anchor[0] + dx) / 5) * 5 - anchor[0];
  let ndy = cy !== null ? dy + cy : Math.round((anchor[1] + dy) / 5) * 5 - anchor[1];
  ndx = Math.round(ndx); ndy = Math.round(ndy);
  // show every line the item now sits on
  const seen = new Set();
  for (const r of d.refs.xs) for (const t of d.targets.xs) if (Math.abs(r + ndx - t) < 0.75 && !seen.has("v" + Math.round(t))) { seen.add("v" + Math.round(t)); S.guides.push(["v", t]); }
  for (const r of d.refs.ys) for (const t of d.targets.ys) if (Math.abs(r + ndy - t) < 0.75 && !seen.has("h" + Math.round(t))) { seen.add("h" + Math.round(t)); S.guides.push(["h", t]); }
  return [ndx, ndy];
}

svg.addEventListener("pointerdown", ev => {
  if (ev.button !== 0) return;
  const [x, y] = toScreen(ev);
  const hit = hitTest(x, y);
  if (!hit) { if (S.gi >= 0) { S.gi = -1; renderProps(); } renderOverlay(); return; }
  if (hit.gi !== S.gi) { S.gi = hit.gi; renderProps(); }
  const g = gauge(), e = M.eff(g);
  S.drag = { part: hit.part, x0: x, y0: y, e0: e, g0: structuredClone(g), moved: false, id: ev.pointerId, boxes0: boxesOf(g) };
  svg.setPointerCapture(ev.pointerId);
  renderOverlay();
  ev.preventDefault();
});
svg.addEventListener("pointermove", ev => {
  const [x, y] = toScreen(ev);
  if (!S.drag) {
    if (ev.pointerType === "mouse") { const hit = hitTest(x, y); const hv = hit ? hit.gi : -1; if (hv !== S.hover) { S.hover = hv; renderOverlay(); } }
    return;
  }
  const d = S.drag, g = gauge(), e0 = d.e0;
  const dx = x - d.x0, dy = y - d.y0;
  S.noSnap = ev.altKey;                         // Alt: place it freely
  if (!d.moved && Math.hypot(dx, dy) * scale() < 4) return;
  d.moved = true;
  const p = d.part;
  if (p === "body" && g.type === "line") {
    const [mx, my] = alignMove(d, dx, dy, [e0.x, e0.y]);
    g.x = e0.x + mx; g.y = e0.y + my; g.x2 = Math.round(e0.x2 + mx); g.y2 = Math.round(e0.y2 + my);
    S.dragNote = `${g.x},${g.y} → ${g.x2},${g.y2}`;
  } else if (p === "body") {
    let [mx, my] = alignMove(d, dx, dy, [e0.x, e0.y]);
    const nx = Math.max(0, Math.min(480, e0.x + mx)), ny = Math.max(0, Math.min(480, e0.y + my));
    g.x = nx; g.y = ny; S.dragNote = `x ${nx}  y ${ny}`;
  } else if (p === "h:p1" || p === "h:p2") {
    const a = p === "h:p1" ? [e0.x, e0.y] : [e0.x2, e0.y2];
    const [mx, my] = alignMove(d, x - a[0], y - a[1], a);
    const nx = a[0] + mx, ny = a[1] + my;
    if (p === "h:p1") { g.x = nx; g.y = ny; } else { g.x2 = nx; g.y2 = ny; }
    S.dragNote = `${nx}, ${ny}`;
  } else if (["number", "label", "unit", "note", "peak_label"].includes(p)) {
    const [mx, my] = alignMove(d, dx, dy, [e0.x + e0[p].dx, e0.y + e0[p].dy]);
    if (!g[p]) g[p] = {};
    g[p].dx = Math.round(e0[p].dx + mx); g[p].dy = Math.round(e0[p].dy + my);
    S.dragNote = `${p} x ${g[p].dx}  y ${g[p].dy}`;
  } else if (p === "h:r") {
    let r = Math.hypot(x - e0.x, y - e0.y); r = S.snap ? Math.round(r / 5) * 5 : Math.round(r);
    g.r = Math.max(20, Math.min(240, r)); S.dragNote = `radius ${g.r}`;
  } else if (p === "h:w") {
    let w = 2 * (Math.hypot(x - e0.x, y - e0.y) - e0.r); w = S.snap ? Math.round(w / 2) * 2 : Math.round(w);
    g.width = Math.max(2, Math.min(80, w)); S.dragNote = `thickness ${g.width}`;
  } else if (p === "h:start" || p === "h:end") {
    let a = M.angleOf(e0.x, e0.y, x, y); if (S.snap) a = Math.round(a / 5) * 5;
    const sgn = e0.sweep < 0 ? -1 : 1, end = e0.start + e0.sweep;
    if (p === "h:start") {
      let sw = sgn * M.norm360(sgn * (end - a)); if (Math.abs(sw) < 10) sw = 10 * sgn;
      g.start = M.norm360(a); g.sweep = sw;
    } else {
      let sw = sgn * M.norm360(sgn * (a - e0.start)); if (Math.abs(sw) < 10) sw = (Math.abs(e0.sweep) > 180 ? 360 : 10) * sgn;
      g.sweep = sw;
    }
    S.dragNote = `starts ${Math.round(M.eff(g).start)}°  runs ${Math.abs(Math.round(M.eff(g).sweep))}°`;
  } else if (p === "h:wh") {
    let w = 2 * Math.abs(x - e0.x), hh = 2 * Math.abs(y - e0.y);
    if (S.snap) { w = Math.round(w / 10) * 10; hh = Math.round(hh / 2) * 2; }
    g.w = Math.max(4, Math.min(480, Math.round(w))); g.h = Math.max(4, Math.min(480, Math.round(hh)));
    S.dragNote = `${g.w} × ${g.h}`;
  } else if (p === "h:dh") {
    let hh = 2 * Math.abs(y - (e0.y + e0.number.dy)); if (S.snap) hh = Math.round(hh / 2) * 2;
    g.digit_height = Math.max(20, Math.min(300, Math.round(hh))); delete g.thickness;
    S.dragNote = `digits ${g.digit_height} px tall`;
  } else if (p === "h:size") {
    let sz = 2 * Math.max(Math.abs(x - e0.x), Math.abs(y - e0.y)); if (S.snap) sz = Math.round(sz / 2) * 2;
    g.size = Math.max(6, Math.min(400, Math.round(sz))); S.dragNote = `size ${g.size}`;
  }
  changed({ commit: false, props: false });
});
function endDrag() {
  if (!S.drag) return;
  const moved = S.drag.moved;
  S.drag = null; S.guides = []; S.dragNote = null; S.noSnap = false;
  if (moved) changed({ props: false }); else renderOverlay();
}
svg.addEventListener("pointerup", endDrag);
svg.addEventListener("pointercancel", endDrag);
svg.addEventListener("pointerleave", () => { if (!S.drag && S.hover >= 0) { S.hover = -1; renderOverlay(); } });

const COARSE = window.matchMedia("(pointer: coarse)");   // a finger: bigger handles
function fitCanvas() {
  const a = $("#canvasArea").getBoundingClientRect();
  const m = a.width < 600 ? 12 : 60, mv = a.width < 600 ? 8 : 40;
  const size = Math.max(200, Math.floor(Math.min(a.width - m, a.height - mv)));
  const b = $("#screenBox"); b.style.width = size + "px"; b.style.height = size + "px";
  renderOverlay();
}
new ResizeObserver(fitCanvas).observe($("#canvasArea"));

// ================================================================ data panel ===
function setSource(src) {
  S.src = src;
  document.querySelectorAll("#dataTabs [data-src]").forEach(b => b.classList.toggle("on", b.dataset.src === src));
  if (src === "live") startLive(); else stopLive();
  if (src === "replay" && S.rec) S.play.last = now();
  renderDataBody(); savePrefs();
}
function renderDataBody() {
  const B = $("#dataBody"); B.innerHTML = "";
  const v = view();
  if (S.src === "demo") {
    B.append(h("p", { class: "note" }, "Made-up readings moving through each gauge's range, to see colours, alerts and animation. Not real data."));
  } else if (S.src === "none") {
    B.append(h("p", { class: "note" }, "No readings - how the view looks with the brain off (every value shows --)."));
  } else if (S.src === "manual") {
    const keys = v ? M.inputsOfView(v) : [];
    if (!keys.length) { B.append(h("p", { class: "note" }, "This view doesn't read anything yet.")); return; }
    const box = h("div", { class: "sliders" });
    for (const k of keys) {
      const r = M.INPUT[k];
      const g0 = v.gauges.find(g => g.input === k);
      const e = g0 ? M.eff(g0) : { min: r.min, max: r.max, input: k, units: "auto", decimals: r.dp, format: "number" };
      let lo = Math.min(e.min, e.max), hi = Math.max(e.min, e.max);
      if (k === "rpm" && !g0) { lo = 0; hi = 8000; }
      const fixed = { bool: [0, 1], lights: [0, 2], oil_status: [1, 3], gear: [-1, 6] }[r.kind];
      if (fixed) [lo, hi] = fixed; else { const sp0 = hi - lo; lo -= sp0 * 0.05; hi += sp0 * 0.05; }
      const span = hi - lo;
      if (!(k in S.manual)) S.manual[k] = r.kind === "num" ? round((Math.min(e.min, e.max) + Math.max(e.min, e.max)) / 2, 2) : lo;
      const val = S.manual[k];
      const sl = h("input", { type: "range", min: lo, max: hi, step: r.kind === "num" ? (span > 50 ? 1 : span / 200) : 1, value: Number.isFinite(val) ? val : lo });
      const out = h("span", { class: "v" });
      const cb = h("input", { type: "checkbox", title: "Has a reading (untick = no reading, shows --)" }); cb.checked = Number.isFinite(val);
      const show = () => { const x = S.manual[k]; out.textContent = Number.isFinite(x) ? M.fmtValue({ ...e, input: k }, x, units()) + " " + M.unitLabel(e, units()) : "--"; };
      sl.addEventListener("input", () => { S.manual[k] = Number(sl.value); cb.checked = true; show(); renderOverlay(); });
      cb.addEventListener("change", () => { S.manual[k] = cb.checked ? Number(sl.value) : NaN; show(); });
      show();
      B.append(box); box.append(h("div", { class: "slider" }, h("span", { title: k }, r.label || k), sl, out, cb));
    }
  } else if (S.src === "live") {
    const st = S.live;
    B.append(h("div", { class: "row" },
      h("span", { class: "pill " + (st.ok ? "ok" : "bad") }, st.ok ? "receiving" : "no data"),
      h("span", { class: "note" }, st.ok ? `from ${S.settings?.brain_url} · ${st.age !== null ? st.age.toFixed(1) + " s old" : ""}` : (st.err || "connecting…")),
      h("button", { class: "small right", onclick: openSettings }, "Brain address…")));
    B.append(h("p", { class: "note" }, "Read-only: the editor asks the brain for the same data the screens read. This computer has to be on the car's network (brain at " + (S.settings?.brain_url || "?") + ")."));
  } else if (S.src === "replay") {
    const row = h("div", { class: "row" }, h("button", { class: "small", onclick: () => $("#csvIn").click() }, S.rec ? "Open another…" : "Open a recording…"));
    if (!S.rec) {
      B.append(row, h("p", { class: "note" }, "Pick a <name>_decoded.csv saved from the brain app's Record tab. It plays back through this layout as if you were driving."));
      return;
    }
    const pb = h("button", { class: "small primary", onclick: () => { S.play.on = !S.play.on; S.play.last = now(); renderDataBody(); } }, S.play.on ? "❚❚ Pause" : "▶ Play");
    const pos = h("input", { type: "range", id: "recPos", min: 0, max: S.rec.span, step: 0.1, value: S.play.pos, style: "flex:1;min-width:200px" });
    pos.addEventListener("input", () => { S.play.pos = Number(pos.value); $("#recTime").textContent = fmtTime(S.play.pos) + " / " + fmtTime(S.rec.span); });
    const sp = h("select", {}, ...[0.25, 0.5, 1, 2, 4, 8].map(x => h("option", { value: x }, x + "×"))); sp.value = S.play.speed;
    sp.addEventListener("change", () => S.play.speed = Number(sp.value));
    const lp = h("input", { type: "checkbox" }); lp.checked = S.play.loop; lp.addEventListener("change", () => S.play.loop = lp.checked);
    row.prepend(pb); row.append(pos, h("span", { id: "recTime", class: "v" }, fmtTime(S.play.pos) + " / " + fmtTime(S.rec.span)), sp, h("label", { class: "tog" }, lp, "Loop"));
    B.append(row);
    const missing = v ? M.inputsOfView(v).filter(k => !S.rec.present.includes(k)) : [];
    B.append(h("p", { class: "note" }, `${S.rec.name}: ${S.rec.t.length.toLocaleString()} rows, ${fmtTime(S.rec.span)}.` +
      (missing.length ? ` Not in this recording (shows --): ${missing.join(", ")}.` : "")));
    if (S.rec.marks.length) B.append(h("div", { class: "row" }, h("span", { class: "note" }, "Marks:"),
      ...S.rec.marks.slice(0, 30).map(m => h("button", { class: "small", onclick: () => { S.play.pos = m.t; slowTick(); } }, `${m.text} @${fmtTime(m.t)}`))));
  }
}
async function pollLive() {
  try {
    const r = await api("/api/live");
    S.live.ok = r.ok; S.live.err = r.error; S.live.age = r.age;
    S.live.flat = r.data ? D.flatten(r.data) : null;
  } catch (e) { S.live.ok = false; S.live.err = "editor server not answering"; }
  if (S.src === "live") { const was = $("#dataBody .pill")?.textContent; if (was !== (S.live.ok ? "receiving" : "no data") || Math.random() < 0.2) renderDataBody(); }
}
function startLive() { if (!S.live.timer) { pollLive(); S.live.timer = setInterval(pollLive, 100); } }
function stopLive() { clearInterval(S.live.timer); S.live.timer = 0; }

$("#csvIn").addEventListener("change", async ev => {
  const f = ev.target.files[0]; ev.target.value = "";
  if (!f) return;
  try {
    S.rec = D.parseRecording(await f.text(), f.name);
    S.play = { ...S.play, on: true, pos: 0, last: now() };
    setSource("replay");
    toast(`Playing ${esc(f.name)}`);
  } catch (e) { toast("Couldn't read that recording: " + esc(e.message), true); }
});

// ================================================================ dialogs ===
function dialog(title, body, buttons) {
  const f = $("#dlgForm"); f.innerHTML = "";
  f.append(h("h3", {}, title), ...body, h("div", { class: "btns" }, ...buttons.map(([label, fn, cls]) =>
    h("button", { type: "button", class: cls || "", onclick: async () => { if (fn) { const keep = await fn(); if (keep === false) return; } closeDialog(); } }, label))));
  $("#dlg").showModal();
  const first = f.querySelector("input[type=text]"); if (first) { first.focus(); first.select(); }
}
function closeDialog() { if ($("#dlg").open) $("#dlg").close(); }

function confirmDiscard(then) {
  if (!S.dirty) return then();
  dialog("Unsaved changes", [h("p", {}, "The current layout has changes that aren't saved to a file. Continue anyway?")],
    [["Cancel", null], ["Save first", async () => { if (await save()) then(); }], ["Discard", then, "danger"]]);
}
function newDoc() {
  confirmDiscard(() => dialog("New layout", [h("p", { class: "note" }, "Start from:")], [
    ["Cancel", null],
    ["Empty", () => setDoc({ format: "370z-gauge-layout", version: 1, views: [] }, null)],
    ...(CAN ? [["Sample views", async () => setDoc(await api("examples/can/sample_layouts.json"), null)]] : []),
    ["Starter views", async () => setDoc(await api(CAN ? "examples/can/starter_layouts.json" : "examples/starter_layouts.json"), null), "primary"]]));
}
async function openDoc() {
  let list;
  try { list = await api("/api/files"); } catch (e) { return toast("Editor server not answering: " + esc(e.message), true); }
  const mine = list.files.filter(f => !f.edition || f.edition === ED), other = list.files.length - mine.length;
  const items = mine.length ? mine.map(f => h("button", { type: "button", onclick: () => { closeDialog(); confirmDiscard(() => openFile(f.name)); } },
    h("span", {}, f.name), h("span", { class: "muted" }, new Date(f.mtime * 1000).toLocaleString()))) : [h("p", { class: "note" }, "No layout files there yet.")];
  dialog("Open a layout", [h("div", { class: "note" }, "In " + list.dir + (other ? ` · ${other} file${other > 1 ? "s" : ""} made for ${CAN ? "the standalone gauge" : "the CAN-bus screens"} not listed` : "")), h("div", { class: "flist" }, ...items)],
    [["From this computer…", () => { $("#fileIn").click(); }], ["Cancel", null]]);
}
async function openFile(name) {
  try { setDoc(asDoc(await api("/api/file?name=" + encodeURIComponent(name))), name); toast("Opened " + esc(name)); }
  catch (e) { toast("Couldn't open " + esc(name) + ": " + esc(e.message), true); }
}
$("#fileIn").addEventListener("change", async ev => {
  const f = ev.target.files[0]; ev.target.value = ""; if (!f) return;
  try { const d = JSON.parse(await f.text()); confirmDiscard(() => { setDoc(d, null, { dirty: true }); toast(`Imported ${esc(f.name)} - Save to keep it in your layouts folder.`); }); }
  catch (e) { toast("Couldn't import: " + esc(e.message), true); }
});
async function save(as = false) {
  let name = S.file;
  if (as || !name) {
    name = await askName(name || "my_layouts.json");
    if (!name) return false;
  }
  try {
    const r = await api("/api/file?name=" + encodeURIComponent(name), { ...S.doc, edition: ED });
    S.file = name; S.dirty = false; autosave(); renderTitle();
    toast("Saved " + esc(r.path));
    return true;
  } catch (e) { toast("Couldn't save: " + esc(e.message), true); return false; }
}
function askName(def) {
  return new Promise(res => {
    const inp = h("input", { type: "text", value: def });
    dialog("Save layout as", [h("div", { class: "note" }, "Saved in your layouts folder (" + (S.settings?.layouts_dir || "") + ")."), inp],
      [["Cancel", () => res(null)], ["Save", () => {
        let n = inp.value.trim(); if (!n) return false;
        if (!n.endsWith(".json")) n += ".json";
        if (!/^[A-Za-z0-9 _.\-()]{1,80}\.json$/.test(n)) { toast("Use letters, numbers, spaces and - _ . ( ) in the name.", true); return false; }
        res(n);
      }, "primary"]]);
    inp.addEventListener("keydown", e => { if (e.key === "Enter") { e.preventDefault(); $("#dlgForm .btns .primary").click(); } });
  });
}
function download(name, text, type) {
  const a = h("a", { href: URL.createObjectURL(new Blob([text], { type })), download: name });
  document.body.append(a); a.click(); a.remove();
}
// The file the gauge's settings page takes: only what the gauge reads, in as few bytes as possible.
const GAUGE_MAX_BYTES = 65520;
function gaugeFile() {
  if (S.err) return toast("Fix this first - the gauge would reject the layout: " + esc(S.err), true, 7000);
  const views = S.doc.views.filter(v => !v.builtin);
  if (!views.length) return toast("There are no views to send yet - press + View.", true);
  const text = JSON.stringify({ format: S.doc.format, version: S.doc.version, views });
  const bytes = new TextEncoder().encode(text).length;
  if (bytes > GAUGE_MAX_BYTES) return toast(`This layout is ${Math.round(bytes / 1024)} KB; the gauge keeps at most ${Math.floor(GAUGE_MAX_BYTES / 1024)} KB. Remove some views or gauges.`, true, 7000);
  const base = (S.file || "gauge-layout").replace(/\.json$/i, "");
  download(base + "-for-gauge.json", text, "application/json");
  dialog("Saved for the gauge", [h("p", {}, `Saved ${base}-for-gauge.json (${(bytes / 1024).toFixed(1)} KB, ${views.length} view${views.length > 1 ? "s" : ""}).`),
    h("p", { class: "note" }, "Next: join the gauge's Wi-Fi, open 192.168.4.1 in a browser, and under Designed screens choose this file and press Upload layout. Then pick the new screens in the Screens list and Save settings.")], [["OK", null, "primary"]]);
}
async function exportSketch() {
  if (S.err) return toast("Fix this first - the screen would reject the layout: " + esc(S.err), true, 7000);
  if (usesNewEngine(S.doc) && !S.builtins.convert)
    return toast("This layout uses newer gauge options (lines, notes, peak text, marks, own scales per unit…) that the screen code in the sketch doesn't have - its older Gauge_Layout.cpp would reject or ignore them. Update the screen code first.", true, 9000);
  if (S.doc.views.some(v => (v.gauges || []).some(g => g.type === "clock" || g.type === "image" || (g.colors && g.colors.nodata))) && !S.builtins.media)
    return toast("This layout has clock or image gauges, which the screen code in the sketch can't draw yet. Update the screen code first (Gauge.cpp, Gauge_Layout.cpp/.h).", true, 9000);
  if (S.doc.views.some(v => (v.gauges || []).some(g => g.colors && ["scale", "ticks", "minor_ticks", "unit"].some(k => g.colors[k] != null))) && !S.builtins.scalecol)
    return toast("This layout sets colours for scale numbers, ticks or units, which the screen code in the sketch can't do yet. Update Gauge_Layout.cpp/.h first.", true, 9000);
  if (usesLampEngine(S.doc) && !S.builtins.lamp)
    return toast("This layout uses newer options (glow, seven-segment digits, scale steps from zero, own colours for alert zones) that the screen code in the sketch doesn't have. Copy the updated Gauge_Layout.cpp and Gauge_Layout.h from this editor's engine-for-can-screens folder into the sketch first.", true, 10000);
  if (S.doc.views.some(v => v.bg != null) && !S.builtins.bg)
    return toast("This layout gives a view its own background colour, which the screen code in the sketch can't do yet. Update the screen code first (Gauge.cpp, Gauge_Layout.cpp/.h).", true, 9000);
  if (S.doc.views.some(isBuiltin) && !S.builtins.supported)
    return toast("This layout lists built-in views, but the screen code in the sketch can't handle that yet - its old Gauge_Layout.cpp would show them as empty views. Update the screen code first (Gauge.cpp, Gauge_Layout.cpp/.h, Gauge_Telemetry.h).", true, 9000);
  try {
    const r = await api("/api/export", S.doc);
    dialog("Exported", [h("p", {}, "Wrote ", h("b", {}, r.path), "."),
      r.backup ? h("p", { class: "note" }, "The previous one is kept at " + r.backup + " (not in the sketch, so it stays out of git).") : null,
      h("p", { class: "note" }, "Next: comp.sh to check it builds, raise GA_VERSION in Gauge_Config.h, then release.sh - the new views are built into that release."),
      !S.builtins.lamp && S.doc.views.some(v => (v.gauges || []).some(g => g.type === "digital"))
        ? h("p", { class: "note" }, "The sketch's Gauge_Layout.cpp is the older one: Number gauges will be centred, not held in place on the decimal point as in the preview. Copy the updated one from engine-for-can-screens/ to get that.") : null,
      h("p", { class: "note" }, "Views: " + (S.doc.views.map(v => v.name || v.id).join(", ") || "none"))].filter(Boolean), [["OK", null, "primary"]]);
  } catch (e) {
    dialog("Couldn't export to the sketch", [h("p", {}, e.message),
      h("p", { class: "note" }, "You can set the sketch folder in Settings, or download Gauge_Layouts.h and copy it into the sketch folder yourself.")],
      [["Settings…", () => { setTimeout(openSettings, 50); }], ["Download Gauge_Layouts.h", downloadHeader], ["Close", null]]);
  }
}
// Options added with the built-in view converter - need the matching Gauge_Layout.cpp.
// Options that need the newer Gauge_Layout.cpp (engine-for-can-screens/ in the editor folder).
function usesLampEngine(doc) {
  return doc.views.some(v => (v.gauges || []).some(g => g.glow || g.core || (g.ticks && Number(g.ticks.step) > 0) ||
    (g.number && (g.number.style || g.number.fixed === false)) || (g.alerts && g.alerts.colors) ||
    Object.values(g.by_unit || {}).some(u => u && Number(u.step) > 0)));
}
function usesNewEngine(doc) {
  return doc.views.some(v => (v.gauges || []).some(g => g.type === "line" || g.note || g.peak_label || g.marks || g.by_unit ||
    g.gaps || g.rounded || (g.alerts && (g.alerts.status || g.alerts.when || g.alerts.number === false)) ||
    (g.ticks && g.ticks.minor_length != null) || /\{[uU]\}/.test((g.label && g.label.text) || "")));
}
async function downloadHeader() {
  try { const r = await api("/api/header", S.doc); download("Gauge_Layouts.h", r.text, "text/plain"); }
  catch (e) { toast("Couldn't make the header: " + esc(e.message), true); }
}
async function loadFromSketch() {
  try {
    const t = await api("/api/export/current");
    confirmDiscard(() => { setDoc(asDoc(t), null, { dirty: true }); toast("Opened the layouts currently in the screen sketch (Gauge_Layouts.h)."); });
  } catch (e) { toast("Couldn't read Gauge_Layouts.h from the sketch: " + esc(e.message), true); }
}
async function openSettings() {
  const s = await api("/api/settings");
  const fld = (k, label, ...notes) => { const i = h("input", { type: "text", value: s[k], "data-k": k }); return [h("div", {}, h("div", { class: "note" }, label), i, ...notes)]; };
  const chk = h("div", { class: "note" }, "Checking…");
  const test = async () => {
    chk.textContent = "Checking…"; chk.style.color = "";
    const inp = $("#dlgForm input[data-k=sketch_dir]");
    if (inp.value.trim() !== s.sketch_dir) { await api("/api/settings", { sketch_dir: inp.value }); s.sketch_dir = inp.value.trim(); }
    try { const r = await api("/api/settings?check=1"); chk.textContent = (r.sketch_ok ? "✓ " : "✗ ") + r.sketch_msg; chk.style.color = r.sketch_ok ? "#7ee2a2" : "#ff8b98"; }
    catch (e) { chk.textContent = "✗ " + e.message; chk.style.color = "#ff8b98"; }
  };
  dialog("Settings", [
    ...fld("layouts_dir", "Layouts folder on this computer (Save / Open)"),
    ...fld("sketch_dir", "Screen sketch folder - Export writes Gauge_Layouts.h here",
      h("div", { class: "note" }, "On another machine (your build server): user@server:~/waveshare2.1/Arduino/examples/LVGL_Arduino - copied over SSH. Needs key login from this computer, set up once in a terminal with: ssh-copy-id user@server"),
      h("div", { class: "row" }, h("button", { type: "button", class: "small", onclick: test }, "Test"), chk)),
    ...fld("brain_url", "Brain address (live data)"),
  ], [["Cancel", null], ["Save", async () => {
    const body = {}; $("#dlgForm").querySelectorAll("input[data-k]").forEach(i => body[i.dataset.k] = i.value);
    await api("/api/settings", body); S.settings = await api("/api/settings"); toast("Settings saved"); await loadBuiltins(); renderViews(); renderDataBody();
  }, "primary"]]);
  test();
}

// ================================================================ keyboard / toolbar ===
document.addEventListener("keydown", ev => {
  const typing = ev.target.matches("input, select, textarea") || $("#dlg").open;
  const ctrl = ev.ctrlKey || ev.metaKey;
  if (ctrl && ev.key.toLowerCase() === "s") { ev.preventDefault(); save(ev.shiftKey); return; }
  if (typing) return;
  if (ctrl && ev.key.toLowerCase() === "z") { ev.preventDefault(); undoRedo(ev.shiftKey ? 1 : -1); return; }
  if (ctrl && ev.key.toLowerCase() === "y") { ev.preventDefault(); undoRedo(1); return; }
  if (ctrl && ev.key.toLowerCase() === "d") { ev.preventDefault(); gaugeOp("dup"); return; }
  if (ctrl && ev.key.toLowerCase() === "c" && gauge()) { S.clip = structuredClone(gauge()); toast("Gauge copied - Ctrl+V pastes it into any view", false, 1800); return; }
  if (ctrl && ev.key.toLowerCase() === "v" && S.clip && view() && !isBuiltin(view())) {
    if (view().gauges.length >= M.LIMITS.gauges) return toast(`A view takes at most ${M.LIMITS.gauges} gauges.`, true);
    view().gauges.push(structuredClone(S.clip)); S.gi = view().gauges.length - 1; changed(); return;
  }
  const g = gauge();
  if (ev.key === "Escape") { if (S.gi >= 0) { S.gi = -1; renderProps(); renderOverlay(); } return; }
  if (!g) return;
  if (ev.key === "Delete" || ev.key === "Backspace") { ev.preventDefault(); gaugeOp("del"); return; }
  // arrows move the selected gauge 5 px (the snap grid); Shift = 1 px at a time
  const step = ev.shiftKey ? 1 : 5, mv = { ArrowLeft: [-step, 0], ArrowRight: [step, 0], ArrowUp: [0, -step], ArrowDown: [0, step] }[ev.key];
  if (mv) {
    ev.preventDefault();
    const e = M.eff(g);
    const mx = Math.max(-e.x, Math.min(480 - e.x, mv[0])), my = Math.max(-e.y, Math.min(480 - e.y, mv[1]));
    g.x = e.x + mx; g.y = e.y + my;
    if (g.type === "line") { g.x2 = e.x2 + mx; g.y2 = e.y2 + my; }   // a line moves both ends
    S.dragNote = g.type === "line" ? `${g.x},${g.y} → ${g.x2},${g.y2}` : `x ${g.x}  y ${g.y}`;
    changed({ commit: false, props: false });
    // one undo step per burst of key presses, once they stop
    clearTimeout(S.nudgeT); S.nudgeT = setTimeout(() => { S.dragNote = null; changed({ props: false }); }, 600);
  }
});
document.addEventListener("click", ev => {
  const b = ev.target.closest("[data-act]");
  const menu = $("#moreMenu");
  if (!ev.target.closest("[data-act=more]") && !ev.target.closest("#moreMenu")) menu.hidden = true;
  if (!b) return;
  const act = b.dataset.act;
  if (act !== "more") menu.hidden = true;
  ({
    new: newDoc, open: openDoc, save: () => save(false), saveas: () => save(true), undo: () => undoRedo(-1), redo: () => undoRedo(1),
    export: exportSketch, gaugefile: gaugeFile, more: () => { menu.hidden = !menu.hidden; }, settings: openSettings,
    loadsketch: loadFromSketch, import: () => $("#fileIn").click(),
    download: () => download(S.file || "layouts.json", JSON.stringify({ ...S.doc, edition: ED }, null, 1) + "\n", "application/json"), downloadh: downloadHeader,
    addview: addView, viewup: () => viewOp("up"), viewdown: () => viewOp("down"), viewdup: () => viewOp("dup"), viewdel: () => viewOp("del"),
    sweep: () => showCurrent(true),
  })[act]?.();
});
document.querySelectorAll("[data-add]").forEach(b => b.addEventListener("click", () => addGauge(b.dataset.add)));
document.querySelectorAll("#dataTabs [data-src]").forEach(b => b.addEventListener("click", () => setSource(b.dataset.src)));
$("#snap").addEventListener("change", ev => { S.snap = ev.target.checked; savePrefs(); });

function renderTheme() {
  const a = $("#accents"); a.innerHTML = "";
  M.ACCENTS.forEach((c, i) => {
    const legible = !(S.theme.dark && i === 6) && !(!S.theme.dark && i === 5);
    a.append(h("button", { class: i === S.theme.accent ? "on" : "", title: c.name + (legible ? "" : " (not offered on this face colour)"), disabled: !legible,
      style: `background:${M.hex6(c.rgb)}`, onclick: () => { S.theme.accent = i; themeChanged(); } }));
  });
  $("#dialSel").value = String(S.theme.dark); $("#tempSel").value = String(S.theme.temp_f);
  $("#clockSel").value = String(S.theme.clock === 0 ? 0 : 1);
  $("#oilSel").value = String(S.theme.oil_bar); $("#screenSel").value = String(S.theme.screen);
}
function themeChanged() {
  // the screens won't use an accent the same as the face (themeLegible)
  if ((S.theme.dark && S.theme.accent === 6) || (!S.theme.dark && S.theme.accent === 5)) S.theme.accent = 0;
  renderTheme(); showCurrent(false); renderProps(); renderOverlay(); savePrefs();
  if (S.src === "manual") renderDataBody();
}
$("#dialSel").addEventListener("change", ev => { S.theme.dark = +ev.target.value; themeChanged(); });
$("#clockSel").addEventListener("change", ev => { S.theme.clock = +ev.target.value; themeChanged(); });
$("#tempSel").addEventListener("change", ev => { S.theme.temp_f = +ev.target.value; themeChanged(); });
$("#oilSel").addEventListener("change", ev => { S.theme.oil_bar = +ev.target.value; themeChanged(); });
$("#screenSel").addEventListener("change", ev => { S.theme.screen = +ev.target.value; slowTick(); savePrefs(); });
window.addEventListener("beforeunload", ev => { if (S.dirty) { ev.preventDefault(); ev.returnValue = ""; } });

async function loadBuiltins() {
  if (!CAN) { S.builtins = { ok: false, views: [], supported: true, bg: true, media: false, scalecol: true, convert: false, config: {}, error: "" }; return; }   // the standalone gauge has no built-in views to list
  try { S.builtins = await api("/api/builtins"); } catch (e) { S.builtins = { ok: false, views: [], supported: false, error: e.message }; }
}

function renderAll() { renderTitle(); renderViews(); renderProps(); renderOverlay(); renderStatus(); renderDataBody(); }

// ================================================================ start ===
// ---- which version of the editor: the standalone oil gauge, or the full CAN-bus screens
function switchEdition(to) {
  if (to === ED) return;
  dialog("Switch to " + EDITION_NAMES[to] + "?", [
    h("p", {}, "The two versions read different data and have different features, so a layout made for one doesn't carry over to the other. The editor will reopen with " + EDITION_NAMES[to] + " and its own screens."),
    h("p", { class: "note" }, "Unsaved changes here are kept in this browser for this version, so switching back picks them up again. Saved files aren't touched.")],
    [["Cancel", null], ["Switch", () => {
      autosave(); S.dirty = false;                       // (the autosave above still remembers it was unsaved)
      try { localStorage.setItem(EDITION_KEY, to); } catch (e) { /* storage off: it will reopen as before */ }
      location.href = location.pathname + "?edition=" + to;
    }, "primary"]]);
}
$("#edSel").addEventListener("change", ev => switchEdition(ev.target.value));
$("#dlg").addEventListener("close", () => { $("#edSel").value = ED; });

(async function start() {
  try { S.settings = await api("/api/settings"); } catch (e) { S.settings = null; }
  const asked = new URLSearchParams(location.search).get("edition");
  let saved = null; try { saved = localStorage.getItem(EDITION_KEY); } catch (e) { /* ignore */ }
  ED = [asked, saved, S.settings && S.settings.edition_default, "gauge"].find(x => x === "gauge" || x === "can");
  try { localStorage.setItem(EDITION_KEY, ED); } catch (e) { /* ignore */ }
  CAN = ED === "can"; M.setEdition(ED);
  document.body.dataset.ed = ED; $("#edSel").value = ED;
  $("#brandName").textContent = CAN ? "370Z" : "Oil Gauge";
  document.title = (CAN ? "370Z" : "Oil Gauge") + " Layout Editor";
  const wasm = CAN ? "preview_can.wasm" : "preview.wasm";
  try { await PV.init(wasm); }
  catch (e) { document.body.innerHTML = `<p style="padding:30px">Couldn't start the screen preview (${wasm}): ${esc(e.message)}</p>`; return; }
  M.setInputs(PV.inputs);
  S.kv = new Float32Array(M.INPUTS.length).fill(NaN);
  try {
    const p = JSON.parse(localStorage.getItem(prefsKey()) || "{}");
    Object.assign(S.theme, p.theme || {}); if (p.snap === false) S.snap = false;
    if (CAN ? (p.src && p.src !== "replay") : ["demo", "manual", "none"].includes(p.src)) S.src = p.src;
    S.openSec = p.openSec || {};
  } catch (e) { /* defaults */ }
  // The standalone gauge always draws its own green-on-black theme, in degrees C, with its own screen number
  if (!CAN) { S.theme.accent = 0; S.theme.dark = 1; S.theme.temp_f = 0; S.theme.screen = 1; S.theme.clock = 1; }
  $("#snap").checked = S.snap;
  renderTheme();
  await loadBuiltins();
  await loadStyles();
  let started = false;
  try {
    const a = JSON.parse(localStorage.getItem(autosaveKey()) || "null");
    if (a && a.doc) { setDoc(a.doc, a.file, { dirty: !!a.dirty }); started = true; if (a.dirty) toast("Picked up where you left off (unsaved changes kept)."); }
  } catch (e) { /* ignore a broken autosave */ }
  if (!started && CAN) {
    try { setDoc(asDoc(await api("/api/export/current")), null); toast("Showing the layouts currently in the screen sketch."); started = true; } catch (e) { /* no sketch */ }
  }
  if (!started) setDoc(await api(CAN ? "examples/can/starter_layouts.json" : "examples/starter_layouts.json"), null);
  setSource(S.src);
  fitCanvas();
  loop();
})();
