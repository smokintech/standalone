// convert.js - turn a built-in screen view into an editable custom view.
//
// Each recipe redraws the built-in view from Gauge.cpp with the layout engine:
// same geometry (R = 200, 30 px band, 20 blocks, ticks 26..44 px in, numbers at
// R - 66), same scales and the same alert rules. The numbers (ranges, limits)
// come from the screen code's own Gauge_Config.h, read from the sketch, so a
// converted view starts from exactly what the screen does today.
//
// cfg: { GA_OIL_MAX: 120, ... } as read by editor.py (/api/builtins).

export const CONVERTIBLE = ["oil", "oiltemp", "oildual", "volts", "speed", "afr", "boost", "coolant", "tpms", "clock"];
// these need the layout engine with clock and image gauges
export const NEEDS_MEDIA = ["tpms", "clock"];

// The band and ticks every built-in dial uses.
// Gauge.cpp marks the blocks with 6 px lines in the face colour from 18 px
// outside the band to 18 px into it - notches over a solid band.
const DIAL = { r: 185, width: 30, start: 225, sweep: 270, segments: 20, zones: false, rounded: true,
               gaps: { width: 6, outer: 18, inner: 18, color: "dial" } };
const TICKS = { numbers: true, font: 20, inset: -4, length: 18, minor_length: 12 };
const TEXTS = { number: { font: 48, dx: 0, dy: 0 }, unit: { show: false } };
const STILL = { smooth: 0 };                       // the built-in views jump straight to a reading

// The stock values (Gauge_Config.h as shipped), used only for a limit the
// sketch doesn't define in a way the editor can read. The editor lists any it used.
export const STOCK = { GA_OIL_MAX: 120, GA_OIL_MAX_BAR: 8, GA_OIL_WARN_PSI: 20, GA_OIL_CRIT_PSI: 10,
  GA_OILT_MIN_C: 0, GA_OILT_MAX: 150, GA_OILT_MIN_F: 50, GA_OILT_MAX_F: 300, GA_OILT_WARN_C: 120, GA_OILT_CRIT_C: 135,
  GA_VOLT_MIN: 10, GA_VOLT_MAX: 16, GA_VOLT_CRIT: 11.8, GA_VOLT_LOW: 12.2, GA_VOLT_HIGH: 15, GA_SPD_MAX: 160,
  GA_AFR_MIN: 10, GA_AFR_MAX: 20, GA_AFR_STOICH: 14.7, GA_AFR_LOAD_PCT: 55, GA_AFR_LEAN_WARN: 13.4, GA_AFR_LEAN_CRIT: 14,
  GA_AFR_RICH_INFO: 11, GA_BOOST_MIN: -15, GA_BOOST_MAX: 30, GA_BOOST_WARN: 18, GA_BOOST_CRIT: 22,
  GA_COOL_MIN_C: 40, GA_COOL_MAX_C: 140, GA_COOL_MIN_F: 100, GA_COOL_MAX_F: 300, GA_COOL_WARN_C: 105, GA_COOL_CRIT_C: 115,
  GA_TYRE_WARN_LO: 30, GA_TYRE_CRIT_LO: 26, GA_TYRE_WARN_HI: 40, GA_TYRE_CRIT_HI: 44 };

// -> { name, gauges, stock: [limits that fell back to the stock value] }
export function convertBuiltin(key, cfg) {
  const stock = [];
  const c = k => {
    const v = cfg[k];
    if (typeof v === "number" && Number.isFinite(v)) return v;
    if (!stock.includes(k)) stock.push(k);
    return STOCK[k];
  };
  const view = recipe(key, c);
  return { ...view, stock };
}

function recipe(key, c) {
  const label = (text, dy = 70) => ({ text, font: 20, dx: 0, dy });
  const dial = (input, extra) => ({ type: "arc", input, x: 240, y: 240, ...DIAL, ...TEXTS, anim: STILL, ...extra });
  const oilAlerts = { status: "oil_status", low_warn: c("GA_OIL_WARN_PSI"), low_crit: c("GA_OIL_CRIT_PSI"),
                      when_running: true, number: false };
  const oilBar = { bar: { min: 0, max: c("GA_OIL_MAX_BAR"), major: 4 } };
  switch (key) {
    case "oil": return { name: "Oil pressure", gauges: [dial("oil_psi", {
      min: 0, max: c("GA_OIL_MAX"), decimals: 0, by_unit: oilBar,
      ticks: { ...TICKS, major: 6, minor: 2 }, label: label("OIL  {U}"), alerts: oilAlerts })] };
    case "oiltemp": return { name: "Oil temperature", gauges: [dial("oil_temp_c", {
      min: c("GA_OILT_MIN_C"), max: c("GA_OILT_MAX"), decimals: 0,
      by_unit: { f: { min: c("GA_OILT_MIN_F"), max: c("GA_OILT_MAX_F"), major: 5 } },
      ticks: { ...TICKS, major: 6, minor: 2 }, label: label("OIL  TEMP  {U}"),
      alerts: { high_warn: c("GA_OILT_WARN_C"), high_crit: c("GA_OILT_CRIT_C"), number: false } })] };
    case "volts": return { name: "Battery voltage", gauges: [dial("volts", {
      min: c("GA_VOLT_MIN"), max: c("GA_VOLT_MAX"), decimals: 1,
      ticks: { ...TICKS, major: 6, minor: 2 }, label: label("BATTERY  V"),
      alerts: { low_crit: c("GA_VOLT_CRIT"), low_warn: c("GA_VOLT_LOW"), high_crit: c("GA_VOLT_HIGH"), number: false } })] };
    case "speed": return { name: "Speedometer", gauges: [dial("speed_mph", {
      min: 0, max: c("GA_SPD_MAX"), decimals: 0, units: "mph",
      ticks: { ...TICKS, major: 8, minor: 2 }, label: label("MPH") })] };
    case "afr": return { name: "Air/fuel ratio", gauges: [dial("afr", {
      min: c("GA_AFR_MIN"), max: c("GA_AFR_MAX"), decimals: 1,
      ticks: { ...TICKS, major: 5, minor: 2 }, label: label("AFR"),
      marks: [{ value: c("GA_AFR_STOICH"), color: "dim", width: 3, outer: 16, inner: 46 }],
      // lean only matters under load: judged with the throttle past GA_AFR_LOAD_PCT
      alerts: { when: { input: "throttle_pct", op: ">=", value: c("GA_AFR_LOAD_PCT") },
                high_crit: c("GA_AFR_LEAN_CRIT"), high_warn: c("GA_AFR_LEAN_WARN"), low_warn: c("GA_AFR_RICH_INFO"), number: true },
      note: { font: 16, dx: 0, dy: 104, none: "no wideband", ok: "on load", low_warn: "rich", high_warn: "lean", high_crit: "LEAN" } })] };
    case "boost": return { name: "Boost", gauges: [dial("boost_psi", {
      min: c("GA_BOOST_MIN"), max: c("GA_BOOST_MAX"), origin: 0, decimals: 1, units: "psi",
      ticks: { ...TICKS, major: 9, minor: 1 }, label: label("BOOST  PSI"),
      marks: [{ value: 0, color: "text", width: 4, outer: 16, inner: 46 }],
      alerts: { high_warn: c("GA_BOOST_WARN"), high_crit: c("GA_BOOST_CRIT"), number: true },
      peak_label: { text: "peak ", font: 20, dx: 0, dy: 104, start: 0 },
      note: { font: 20, dx: 0, dy: 104, none: "no sensor" } })] };
    case "coolant": return { name: "Coolant temperature", gauges: [dial("coolant_c", {
      min: c("GA_COOL_MIN_C"), max: c("GA_COOL_MAX_C"), decimals: 0,
      by_unit: { f: { min: c("GA_COOL_MIN_F"), max: c("GA_COOL_MAX_F"), major: 5 } },
      ticks: { ...TICKS, major: 5, minor: 2 }, label: label("COOLANT  {U}"),
      alerts: { high_warn: c("GA_COOL_WARN_C"), high_crit: c("GA_COOL_CRIT_C"), number: true } })] };
    case "oildual": {
      const half = { ...DIAL, segments: 12, sweep: 160, ticks: { ...TICKS, major: 4, minor: 1, numbers: false } };
      return { name: "Oil pressure + temp", gauges: [
        { type: "arc", input: "oil_psi", x: 240, y: 240, ...half, start: 280, anim: STILL,
          min: 0, max: c("GA_OIL_MAX"), decimals: 0, by_unit: oilBar,
          number: { font: 48, dx: 0, dy: -70 }, unit: { show: true, font: 20, dx: 0, dy: -25 }, label: { text: "" },
          alerts: oilAlerts },
        { type: "arc", input: "oil_temp_c", x: 240, y: 240, ...half, start: 100, anim: STILL,
          min: c("GA_OILT_MIN_C"), max: c("GA_OILT_MAX"), decimals: 0,
          by_unit: { f: { min: c("GA_OILT_MIN_F"), max: c("GA_OILT_MAX_F") } },
          number: { font: 48, dx: 0, dy: 70 }, unit: { show: false }, label: label("OIL {U}", 28),
          alerts: { high_warn: c("GA_OILT_WARN_C"), high_crit: c("GA_OILT_CRIT_C"), number: false } },
        { type: "line", x: 90, y: 240, x2: 390, y2: 240, width: 2, color: "tickmaj" }] };
    }
    case "tpms": {
      // Gauge.cpp buildTpms: the car picture, a pressure by each wheel (grey "--"
      // with no reading, amber / red outside the limits), one PSI caption
      const lim = { low_crit: c("GA_TYRE_CRIT_LO"), low_warn: c("GA_TYRE_WARN_LO"),
                    high_warn: c("GA_TYRE_WARN_HI"), high_crit: c("GA_TYRE_CRIT_HI"), number: true };
      const wheel = (input, name, dx, dy) => ({ type: "digital", input, x: 240 + dx, y: 240 + dy, decimals: 0, units: "psi",
        number: { font: 48, dx: 0, dy: 0 }, unit: { show: false }, label: { text: name, font: 16, dx: 0, dy: -30 },
        colors: { text: "gauge", label: "dim", nodata: "dim" }, alerts: lim, anim: STILL });
      return { name: "Tyre pressures", gauges: [
        { type: "image", image: "car_370z", x: 240, y: 234 },
        wheel("tyre_fl", "FL", -122, -62), wheel("tyre_fr", "FR", 122, -62),
        wheel("tyre_rl", "RL", -122, 62), wheel("tyre_rr", "RR", 122, 62),
        { type: "text", text: "PSI", font: 20, color: "dim", x: 240, y: 352 }] };
    }
    case "clock": return { name: "Clock", gauges: [{ type: "clock", x: 240, y: 240 }] };   // the built-in look is the default
    default: throw new Error(`“${key}” can't be converted yet`);
  }
}

// The limits the recipes use: editor.py reads exactly these from the sketch.
export const CONFIG_KEYS = ["GA_OIL_MAX", "GA_OIL_MAX_BAR", "GA_OIL_WARN_PSI", "GA_OIL_CRIT_PSI",
  "GA_OILT_MIN_C", "GA_OILT_MAX", "GA_OILT_MIN_F", "GA_OILT_MAX_F", "GA_OILT_WARN_C", "GA_OILT_CRIT_C",
  "GA_VOLT_MIN", "GA_VOLT_MAX", "GA_VOLT_CRIT", "GA_VOLT_LOW", "GA_VOLT_HIGH", "GA_SPD_MAX",
  "GA_AFR_MIN", "GA_AFR_MAX", "GA_AFR_STOICH", "GA_AFR_LOAD_PCT", "GA_AFR_LEAN_WARN", "GA_AFR_LEAN_CRIT", "GA_AFR_RICH_INFO",
  "GA_BOOST_MIN", "GA_BOOST_MAX", "GA_BOOST_WARN", "GA_BOOST_CRIT",
  "GA_COOL_MIN_C", "GA_COOL_MAX_C", "GA_COOL_MIN_F", "GA_COOL_MAX_F", "GA_COOL_WARN_C", "GA_COOL_CRIT_C",
  "GA_TYRE_WARN_LO", "GA_TYRE_CRIT_LO", "GA_TYRE_WARN_HI", "GA_TYRE_CRIT_HI"];
