// preview.js - runs the screens' own drawing code (preview.wasm: LVGL 8.3.10,
// the screens' lv_conf.h, Gauge_Layout.cpp, Gauge_Inputs.cpp) in the page.

export class Preview {
  async init(url) {
    const imports = { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }) };
    const res = await fetch(url);
    const { instance } = await WebAssembly.instantiate(await res.arrayBuffer(), imports);
    this.x = instance.exports;
    if (this.x._initialize) this.x._initialize();
    this.x.pv_init();
    this.scratch = this.x.pv_alloc(256);
    this.sizes = new Map();
    this.inputs = JSON.parse(this.cstr(this.x.pv_inputs()));
    this.view = -1;
  }
  get mem() { return new Uint8Array(this.x.memory.buffer); }
  cstr(p) { const m = this.mem; let e = p; while (m[e]) e++; return new TextDecoder().decode(m.subarray(p, e)); }

  // -> null when the screen would accept the layout, else the screen's error message
  load(text) {
    const b = new TextEncoder().encode(text);
    const p = this.x.pv_alloc(b.length + 1);
    this.mem.set(b, p); this.mem[p + b.length] = 0;
    const ok = this.x.pv_load(p, b.length);
    this.x.pv_free(p);
    this.view = -1;
    return ok ? null : (this.cstr(this.x.pv_error()) || "rejected");
  }
  viewCount() { return this.x.pv_view_count(); }
  theme(accent, dark, tempF, oilBar) { this.x.pv_theme(accent, dark ? 1 : 0, tempF ? 1 : 0, oilBar ? 1 : 0); }
  show(v, now) { this.view = this.x.pv_show(v, now | 0) ? v : -1; return this.view >= 0; }
  setValues(kv) {                       // kv: Float32Array(GI_COUNT), NaN = no reading
    new Float32Array(this.x.memory.buffer, this.x.pv_kv(), this.inputs.length).set(kv);
  }
  step(now) { this.x.pv_step(now | 0); }
  // The images "image" gauges can use: the built-in car plus [{name, w, h, bytes}] (RGB565 LE + alpha).
  // Call before load(), whenever the list changes.
  setImages(list) {
    this.x.pv_images_clear();
    for (const im of list) {
      const nb = new TextEncoder().encode(im.name);
      const np = this.x.pv_alloc(nb.length + 1), dp = this.x.pv_alloc(im.bytes.length);
      this.mem.set(nb, np); this.mem[np + nb.length] = 0;
      this.mem.set(im.bytes, dp);
      this.x.pv_image_add(np, im.w, im.h, dp);
      this.x.pv_free(np); this.x.pv_free(dp);
    }
  }
  carSize() { const r = this.x.pv_car_size(); return r ? [r >>> 16, r & 0xffff] : null; }
  // The time clock gauges show (a JS Date), and the screen's analogue/digital setting.
  clock(d, analog) {
    this.x.pv_clock(1, d.getFullYear(), d.getMonth() + 1, d.getDate(), d.getDay(), d.getHours(), d.getMinutes(), d.getSeconds(), analog ? 1 : 0);
  }
  takeover(screenId) { return this.x.pv_takeover(screenId); }
  heapUsed() { return this.x.pv_heap_used(); }
  // Paint the current frame (RGB565) into a 2D canvas context.
  paint(ctx, img) {
    const src = new Uint16Array(this.x.memory.buffer, this.x.pv_frame(), 480 * 480);
    const d = img.data;
    for (let i = 0, j = 0; i < 230400; i++, j += 4) {
      const v = src[i];
      const r = v >> 11, g = (v >> 5) & 63, b = v & 31;
      d[j] = (r << 3) | (r >> 2); d[j + 1] = (g << 2) | (g >> 4); d[j + 2] = (b << 3) | (b >> 2); d[j + 3] = 255;
    }
    ctx.putImageData(img, 0, 0);
  }
  // [w, h] of a text in a screen font size (same font choice as the engine)
  measure(text, font) {
    const key = font + "|" + text;
    let s = this.sizes.get(key);
    if (!s) {
      const b = new TextEncoder().encode(String(text).slice(0, 60));
      this.mem.set(b, this.scratch); this.mem[this.scratch + b.length] = 0;
      const r = this.x.pv_text_size(this.scratch, font);
      s = [r >>> 16, r & 0xffff];
      if (this.sizes.size > 2000) this.sizes.clear();
      this.sizes.set(key, s);
    }
    return s;
  }
}
