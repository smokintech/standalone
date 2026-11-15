// ---------------------------------------------------------------------------
// oil_sensor.cpp - reads the oil pressure sender (and, on an INA3221, a boost
// sensor and a wideband AFR controller) through either
//   * the M5 Meter ISOLATED (ADS1115 @ 0x49) - one input, oil only, or
//   * an Adafruit INA3221 (bus-voltage input of up to three channels), or - chosen on the phone page -
//   * a plain ADS1115 breakout (A0..A3, single-ended), or
//   * an M5 PaHUB (PCA9548A multiplexer) with an M5 voltmeter on each port, or
//   * an M5 PbHUB (its six ports read as analog inputs),
// on the Waveshare board's own I2C bus.
//
// Everything here is NON-BLOCKING and runs on the same task as LVGL and the
// touch driver (Arduino's Wire is not thread-safe, and the touch chip is on the
// same bus). OilSensor_Poll() does at most one short I2C transfer per call and
// returns, so the screen never stalls waiting for a conversion.
//
// A reading is only ever published as a number when it is genuinely good.
// Otherwise the verdict is a named fault - never a made-up 0.
// ---------------------------------------------------------------------------
#include <Wire.h>
#include "oil_sensor.h"
#include "oil_settings.h"
#include "oil_math.h"

// ---- ADS1115 -----------------------------------------------------------
static const uint16_t ADS_MUX_P0N1 = 0x0000;      // AIN0 - AIN1, as the M5 unit is wired
static const uint16_t ADS_PGA_4096 = 0x0200;
static const uint16_t ADS_PGA_0256 = 0x0A00;
// OS=1 start | mux | pga | single-shot | 128 SPS | comparator off  (identical
// to Brain370Z/sensors_task.cpp ads_raw)
static inline uint16_t ads_cfg(uint16_t pga) {
  return 0x8000 | ADS_MUX_P0N1 | pga | 0x0100 | 0x0080 | 0x0003;
}
static const uint32_t ADS_CONV_MS  = 9;           // 128 SPS = 7.8 ms
static const uint32_t ADS_TIMEOUT_MS = 40;

// A plain ADS1115 breakout: one input at a time against ground, +/-6.144 V range, 250 samples a second.
// OS=1 start | mux AINx-GND | PGA 6.144 | single-shot | 250 SPS | comparator off
static inline uint16_t ads_single_cfg(int ain /*1..4*/) {
  return 0x8000 | (uint16_t)((0x4 + (ain - 1)) << 12) | 0x0000 | 0x0100 | 0x00A0 | 0x0003;
}
static const uint32_t ADS_FAST_MS = 5;            // 250 SPS = 4 ms

// ---- M5 PaHUB (PCA9548A multiplexer) and PbHUB (STM32F030 expander) ----
static const uint8_t  PA_VOLTMETER_ADDR = 0x49;   // every M5 voltmeter has this address - the hub keeps them apart
// PbHUB: the analog-read command of port 1..6 (IO0): the port's base command | 0x06, answer = 2 bytes, low byte first.
static const uint8_t  PB_BASE[6] = { 0x40, 0x50, 0x60, 0x70, 0x80, 0xA0 };
static const uint32_t PB_SAMPLE_GAP_MS = 10;

// ---- INA3221 -----------------------------------------------------------
static const uint8_t  INA_REG_CONFIG = 0x00;
static const uint8_t  INA_REG_MFR_ID = 0xFE;      // reads 0x5449 ("TI")
static const uint8_t  INA_REG_DIE_ID = 0xFF;      // reads 0x3220
// Config: only the channels in use are enabled, bus-voltage continuous mode (110).
// Hardware averaging is 16 samples when one channel is in use, 4 when several are
// (each channel is converted in turn, so 3 channels x 4 x 1.1 ms = 13 ms still fits
// inside the 20 ms sample gap; 16 would take 53 ms). The shunt side is not used at
// all: every signal is read on the BUS input of its channel.
static inline uint16_t ina_cfg(uint8_t mask, int nch) {   // mask bit0..2 = channel 1..3
  uint16_t en = 0;
  for (int c = 1; c <= 3; c++) if (mask & (1u << (c - 1))) en |= (uint16_t)(0x8000u >> c);   // CH1 = bit14, CH2 = bit13, CH3 = bit12
  const uint16_t avg = (nch > 1) ? 0x1u /*4*/ : 0x2u /*16*/;
  return en | (avg << 9) | (0x4u << 6) /*VBUS 1.1ms*/ | (0x4u << 3) /*VSH 1.1ms*/ | 0x6u /*bus, continuous*/;
}
static const uint32_t INA_SAMPLE_GAP_MS = 20;     // > one full pass over the enabled channels (<= 17.6 ms)

// ---- state -------------------------------------------------------------
static OilKind  g_kind = OIL_KIND_NONE;
static uint8_t  g_addr = 0;
static uint32_t g_last_probe = 0;
static int      g_errs = 0;

// per-channel state (index = OgChan)
struct Chan {
  bool     on;                    // being measured by the current board
  uint8_t  reg;                   // its input: INA3221 channel 1..3 / ADS1115 input 1..4 / hub port 1..6
  bool     coarse;                // PaHUB: this voltmeter clipped on its fine range and needs the coarse one
  bool     got;                   // PaHUB: has a value for the sample being taken
  float    volts[OG_SAMPLES];
  OilFault fault;
  float    val, v;
  bool     have_ema;
  float    ema;
};
static Chan     g_ch[OG_NCH];
static uint8_t  g_mask = 0;        // INA3221 channels in use (bit0 = ch1)
static int      g_nch = 0;
static int      g_act[OG_NCH];     // the channels being measured, in order
static int      g_nact = 0;
static int      g_ai = 0;          // which of them a step is on
static int      g_phase = 0, g_pass = 0;   // PaHUB: 0 = start conversions, 1 = wait, 2 = read; pass 0 = fine range, 1 = coarse
static bool     g_pb_hinted = false;

// sampling cycle
static bool     g_sampling = false;
static int      g_ns = 0;                          // samples collected this cycle
static uint32_t g_next_cycle = 0;

// ADS1115 conversion in flight
static bool     g_waiting = false;
static uint16_t g_cur_pga = ADS_PGA_0256;
static uint32_t g_t_conv = 0;
// INA3221 pacing
static uint32_t g_next_sample = 0;

// published verdict
static uint32_t g_updated = 0;

static bool configured(OgChan c) {
  return c == OG_CH_OIL || (c == OG_CH_BOOST && g_set.boost_ch) || (c == OG_CH_AFR && g_set.afr_ch);
}

static void clearChannels() {
  for (int c = 0; c < OG_NCH; c++) {
    g_ch[c].on = false; g_ch[c].have_ema = false; g_ch[c].val = NAN; g_ch[c].v = NAN;
    g_ch[c].fault = OIL_FAULT_NO_SENSOR;
  }
  g_mask = 0; g_nch = 0; g_nact = 0; g_ai = 0;
}

// ---- raw I2C helpers (Wire is already started by board_i2c_begin()) ------------
static bool wr16(uint8_t a, uint8_t reg, uint16_t v) {
  Wire.beginTransmission(a);
  Wire.write(reg); Wire.write((uint8_t)(v >> 8)); Wire.write((uint8_t)(v & 0xFF));
  return Wire.endTransmission() == 0;
}
static bool rd16(uint8_t a, uint8_t reg, uint16_t& v) {
  Wire.beginTransmission(a); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)a, 2) != 2) return false;
  v = (uint16_t)(Wire.read() << 8); v |= (uint16_t)Wire.read();
  return true;
}
static bool ack(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }

// ---- detection ---------------------------------------------------------
static bool ina_present(uint8_t a) {
  uint16_t m;
  return ack(a) && rd16(a, INA_REG_MFR_ID, m) && m == 0x5449;
}
static bool m5_present() {
  uint16_t c;
  return ack(OG_M5_ADDR) && rd16(OG_M5_ADDR, 0x01, c);
}

// ---- the hubs ----
static bool pa_select(uint8_t addr, int port) {      // PCA9548A: one data byte, one bit per port; 0 = none
  Wire.beginTransmission(addr); Wire.write((uint8_t)(port >= 1 ? (1u << (port - 1)) : 0));
  return Wire.endTransmission() == 0;
}
static bool pa_present(uint8_t addr) {
  if (!ack(addr) || !pa_select(addr, 0)) return false;
  if (Wire.requestFrom((int)addr, 1) != 1) return false;
  return Wire.read() == 0x00;                         // the control register reads back what was written
}
static bool pb_read(uint8_t addr, int port, uint16_t& raw) {
  if (port < 1 || port > 6) return false;
  Wire.beginTransmission(addr); Wire.write((uint8_t)(PB_BASE[port - 1] | 0x06));
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)addr, 2) != 2) return false;
  const uint8_t lo = (uint8_t)Wire.read(), hi = (uint8_t)Wire.read();
  raw = (uint16_t)(lo | (hi << 8));
  return true;
}

static void resetCycle() {
  g_sampling = false; g_waiting = false; g_ns = 0;
  for (int c = 0; c < OG_NCH; c++) g_ch[c].have_ema = false;
  g_next_cycle = millis();
}

// Decide which channels the current board measures.
static void planChannels(OilKind k) {
  clearChannels();
  g_ch[OG_CH_OIL].on = true; g_ch[OG_CH_OIL].reg = g_set.ina_channel;
  if (k != OIL_KIND_M5) {                           // every board but the one-input M5 Meter can carry boost and AFR
    if (g_set.boost_ch) { g_ch[OG_CH_BOOST].on = true; g_ch[OG_CH_BOOST].reg = g_set.boost_ch; }
    if (g_set.afr_ch)   { g_ch[OG_CH_AFR].on   = true; g_ch[OG_CH_AFR].reg   = g_set.afr_ch; }
  }
  for (int c = 0; c < OG_NCH; c++) if (g_ch[c].on) {
    g_nch++; g_act[g_nact++] = c;
    if (g_ch[c].reg >= 1 && g_ch[c].reg <= 8) g_mask |= (uint8_t)(1u << (g_ch[c].reg - 1));
  }
}

static void setKind(OilKind k, uint8_t a) {
  g_kind = k; g_addr = a; g_errs = 0;
  planChannels(k);
  resetCycle();
  for (int c = 0; c < OG_NCH; c++) if (g_ch[c].on) g_ch[c].fault = OIL_FAULT_NO_DATA;
  g_updated = millis();
}

static void probeHub() {
  const uint8_t a = og_hub_addr(g_set);
  const OilKind k = g_set.sensor_mode == 3 ? OIL_KIND_ADS1115 : g_set.sensor_mode == 4 ? OIL_KIND_PAHUB : OIL_KIND_PBHUB;
  bool ok = false;
  if (k == OIL_KIND_ADS1115) { uint16_t c; ok = ack(a) && rd16(a, 0x01, c); }
  else if (k == OIL_KIND_PAHUB) ok = pa_present(a);
  else { uint16_t r; ok = ack(a) && pb_read(a, g_set.ina_channel, r); }
  if (!ok) return;
  setKind(k, a);
  g_pb_hinted = false;
  Serial.printf("[oil] %s found at 0x%02X: oil on %s %d", k == OIL_KIND_ADS1115 ? "ADS1115" : k == OIL_KIND_PAHUB ? "PaHUB" : "PbHUB",
                a, k == OIL_KIND_ADS1115 ? "input" : "port", (int)g_set.ina_channel);
  if (g_set.boost_ch) Serial.printf(", boost on %d", (int)g_set.boost_ch);
  if (g_set.afr_ch)   Serial.printf(", AFR on %d", (int)g_set.afr_ch);
  Serial.printf("\n");
}

static void probe() {
  if (g_set.sensor_mode >= 3) { probeHub(); return; }
  const bool m5  = (g_set.sensor_mode != 2) && m5_present();
  uint8_t ina_a = 0;
  if (g_set.sensor_mode != 1) {
    // Try the configured address first, then the other three the A0 pin allows.
    const uint8_t order[4] = { (uint8_t)OG_INA_ADDR, 0x40, 0x41, 0x42 };
    for (int i = 0; i < 4 && !ina_a; i++) if (ina_present(order[i])) ina_a = order[i];
    if (!ina_a && ina_present(0x43)) ina_a = 0x43;
  }
  OilKind pick = OIL_KIND_NONE;
  if (m5 && ina_a)   pick = (OG_SENSOR_PREFER == 2) ? OIL_KIND_INA3221 : OIL_KIND_M5;
  else if (m5)       pick = OIL_KIND_M5;
  else if (ina_a)    pick = OIL_KIND_INA3221;

  if (pick == OIL_KIND_M5) {
    setKind(OIL_KIND_M5, OG_M5_ADDR);
    Serial.printf("[oil] M5 Meter ISOLATED (ADS1115) found at 0x%02X%s\n", OG_M5_ADDR,
                  ina_a ? "  (INA3221 also present - ignored, OG_SENSOR_PREFER)" : "");
    if (g_set.boost_ch || g_set.afr_ch)
      Serial.println("[oil] NOTE: boost / AFR are switched on but the M5 Meter has one input - they need the INA3221 (set Board = INA3221)");
  } else if (pick == OIL_KIND_INA3221) {
    planChannels(OIL_KIND_INA3221);                 // to know the channel mask for the config word
    if (!wr16(ina_a, INA_REG_CONFIG, ina_cfg(g_mask, g_nch))) {
      Serial.printf("[oil] INA3221 at 0x%02X answered but would not accept its config\n", ina_a);
      return;
    }
    setKind(OIL_KIND_INA3221, ina_a);
    uint16_t die = 0; rd16(ina_a, INA_REG_DIE_ID, die);
    Serial.printf("[oil] INA3221 found at 0x%02X (die id 0x%04X): oil on channel %d", ina_a, die, (int)g_set.ina_channel);
    if (g_set.boost_ch) Serial.printf(", boost on channel %d", (int)g_set.boost_ch);
    if (g_set.afr_ch)   Serial.printf(", AFR on channel %d", (int)g_set.afr_ch);
    Serial.printf("%s\n", m5 ? "  (M5 also present - ignored, OG_SENSOR_PREFER)" : "");
  }
}

// ---- error / cycle bookkeeping -----------------------------------------
static void lost() {
  Serial.println("[oil] sensor lost (I2C errors) - searching again");
  g_kind = OIL_KIND_NONE; g_addr = 0; g_waiting = false; g_sampling = false;
  clearChannels();
  g_updated = millis(); g_last_probe = millis();
}
static void i2cErr() {
  g_waiting = false; g_ai = 0; g_phase = 0; g_pass = 0;
  if (++g_errs >= OG_LOST_AFTER_ERRS) lost();
}

// Signal volts -> the channel's value. Returns OG_CONV_OK / LOW / HIGH.
static OgConv convert(int ch, float v, float& out) {
  switch (ch) {
    case OG_CH_OIL:   return og_volts_to_psi(v, out);
    case OG_CH_BOOST: return og_volts_to_boost(v, out);
    default:          return og_volts_to_afr(v, out);
  }
}
static float roundTo(int ch, float x) {                 // oil / boost to 0.1, AFR to 0.01
  const float k = (ch == OG_CH_AFR) ? 100.0f : 10.0f;
  return roundf(x * k) / k;
}

// A full set of samples is in: turn each channel's into one published verdict.
static void finishCycle() {
  for (int c = 0; c < OG_NCH; c++) {
    Chan& h = g_ch[c];
    if (!h.on) continue;
    float vals[OG_SAMPLES], vcopy[OG_SAMPLES]; int np = 0, nlow = 0, nhigh = 0, nv = 0;
    for (int i = 0; i < g_ns; i++) {
      if (!isfinite(h.volts[i])) continue;          // a sample this input could not give (PaHUB: no voltmeter answering on its port)
      vcopy[nv++] = h.volts[i];
      float x;
      OgConv r = convert(c, h.volts[i], x);
      if (r == OG_CONV_OK) vals[np++] = x;
      else if (r == OG_CONV_LOW) nlow++;
      else nhigh++;
    }
    h.v = nv ? og_median(vcopy, nv) : NAN;

    if (np == 0) {                                  // nothing usable: a named fault
      h.fault = nv == 0 ? OIL_FAULT_NO_SENSOR : (nlow >= nhigh) ? OIL_FAULT_WIRING_LOW : OIL_FAULT_WIRING_HIGH;
      h.val = NAN; h.have_ema = false;
    } else {
      float med = og_median(vals, np);
      h.ema = !h.have_ema ? med : OG_SMOOTH * med + (1.0f - OG_SMOOTH) * h.ema;
      h.have_ema = true;
      h.val = roundTo(c, h.ema);
      h.fault = OIL_FAULT_NONE;
    }
  }
  g_updated = millis();
  g_sampling = false; g_ns = 0;
  g_next_cycle = g_next_cycle + OG_INTERVAL_MS;   // fixed cadence, no drift
  if ((int32_t)(millis() - g_next_cycle) > (int32_t)OG_INTERVAL_MS) g_next_cycle = millis();
}

static void addSample(int ch, float v) {
  if (g_ns < OG_SAMPLES) g_ch[ch].volts[g_ns] = v;
}
static void sampleDone() {                         // one sample taken on every channel in use
  if (g_ns < OG_SAMPLES) g_ns++;
  g_errs = 0;
}

// ---- M5 / ADS1115: start -> wait -> read, one step per call --------------
static void m5Start(uint16_t pga) {
  if (!wr16(g_addr, 0x01, ads_cfg(pga))) { i2cErr(); return; }
  g_cur_pga = pga; g_t_conv = millis(); g_waiting = true;
}

static void pollM5(uint32_t now) {
  if (!g_sampling) {
    if ((int32_t)(now - g_next_cycle) < 0) return;
    g_sampling = true; g_ns = 0;
    m5Start(ADS_PGA_0256);                         // fine range first, as oil_publisher.py
    return;
  }
  if (!g_waiting) { m5Start(ADS_PGA_0256); return; }
  if (now - g_t_conv < ADS_CONV_MS) return;

  uint16_t c;
  if (!rd16(g_addr, 0x01, c)) { i2cErr(); return; }
  if (!(c & 0x8000)) {                             // not finished yet
    if (now - g_t_conv > ADS_TIMEOUT_MS) i2cErr();
    return;
  }
  uint16_t r;
  if (!rd16(g_addr, 0x00, r)) { i2cErr(); return; }
  int16_t raw = (int16_t)r;
  if (g_cur_pga == ADS_PGA_0256 && abs((int)raw) > 32000) {   // clipped: use the coarse range
    m5Start(ADS_PGA_4096);
    return;
  }
  addSample(OG_CH_OIL, og_ads1115_volts(raw, g_cur_pga == ADS_PGA_4096 ? 4.096f : 0.256f));
  sampleDone();
  g_waiting = false;
  if (g_ns >= OG_SAMPLES) finishCycle();
}

// ---- INA3221: read the bus-voltage register of every channel in use ------
static void pollINA(uint32_t now) {
  if (!g_sampling) {
    if ((int32_t)(now - g_next_cycle) < 0) return;
    g_sampling = true; g_ns = 0; g_next_sample = now;
  }
  if ((int32_t)(now - g_next_sample) < 0) return;
  for (int c = 0; c < OG_NCH; c++) {               // a few short reads (about 0.6 ms each at 100 kHz)
    if (!g_ch[c].on) continue;
    uint16_t r;
    if (!rd16(g_addr, (uint8_t)(0x02 + 2 * (g_ch[c].reg - 1)), r)) { i2cErr(); return; }   // drop the partial tick
    addSample(c, og_ina3221_volts(r));
  }
  sampleDone();
  g_next_sample = now + INA_SAMPLE_GAP_MS;
  if (g_ns >= OG_SAMPLES) finishCycle();
}

// ---- plain ADS1115 breakout: one input at a time, start -> wait -> read, one step per call ----
static void pollADS(uint32_t now) {
  if (!g_sampling) {
    if ((int32_t)(now - g_next_cycle) < 0) return;
    g_sampling = true; g_ns = 0; g_ai = 0; g_waiting = false;
  }
  const int c = g_act[g_ai];
  if (!g_waiting) {
    if (!wr16(g_addr, 0x01, ads_single_cfg(g_ch[c].reg))) { i2cErr(); return; }
    g_t_conv = now; g_waiting = true;
    return;
  }
  if (now - g_t_conv < ADS_FAST_MS) return;
  uint16_t cfg, r;
  if (!rd16(g_addr, 0x01, cfg)) { i2cErr(); return; }
  if (!(cfg & 0x8000)) {                           // not finished yet
    if (now - g_t_conv > ADS_TIMEOUT_MS) i2cErr();
    return;
  }
  if (!rd16(g_addr, 0x00, r)) { i2cErr(); return; }
  addSample(c, og_ads_single_volts((int16_t)r));
  g_waiting = false;
  if (++g_ai >= g_nact) {                          // every input has its reading for this sample
    g_ai = 0; sampleDone();
    if (g_ns >= OG_SAMPLES) finishCycle();
  }
}

// ---- M5 PaHUB + an M5 voltmeter on each port ----------------------------------
// All the voltmeters convert at the same time: start every one, wait once, read every one. A voltmeter that clips
// on its fine range is run again on the coarse range (a second, short pass for just those). A voltmeter that does
// not answer gives no sample (its input shows NO SENSOR); the hub not answering counts towards losing the board.
static void paSampleDone() {
  for (int k = 0; k < g_nact; k++) { const int c = g_act[k]; if (!g_ch[c].got) addSample(c, NAN); }
  g_phase = 0; g_pass = 0; g_ai = 0;
  sampleDone();
  if (g_ns >= OG_SAMPLES) finishCycle();
}
static void pollPaHub(uint32_t now) {
  if (!g_sampling) {
    if ((int32_t)(now - g_next_cycle) < 0) return;
    g_sampling = true; g_ns = 0; g_phase = 0; g_pass = 0; g_ai = 0;
  }
  if (g_phase == 0) {                              // start the conversions, one voltmeter per call
    if (g_pass == 0 && g_ai == 0)                  // a fresh sample (also after a hub error): forget the last one
      for (int k = 0; k < g_nact; k++) { g_ch[g_act[k]].got = false; g_ch[g_act[k]].coarse = false; }
    while (g_ai < g_nact && g_pass == 1 && !g_ch[g_act[g_ai]].coarse) g_ai++;
    if (g_ai >= g_nact) { g_phase = 1; g_t_conv = now; return; }
    const int c = g_act[g_ai++];
    if (!pa_select(g_addr, g_ch[c].reg)) { i2cErr(); return; }                 // the hub itself did not answer
    wr16(PA_VOLTMETER_ADDR, 0x01, ads_cfg(g_pass ? ADS_PGA_4096 : ADS_PGA_0256));   // no voltmeter there: it fails to read below, so no sample
    return;
  }
  if (g_phase == 1) {
    if (now - g_t_conv < ADS_CONV_MS) return;
    g_phase = 2; g_ai = 0;
    return;
  }
  // phase 2: read the voltmeters that were started in this pass
  while (g_ai < g_nact) {
    const int c = g_act[g_ai];
    Chan& h = g_ch[c];
    if (g_ch[c].got || (g_pass == 1 && !h.coarse)) { g_ai++; continue; }
    if (!pa_select(g_addr, h.reg)) { i2cErr(); return; }
    uint16_t cfg, r;
    if (!rd16(PA_VOLTMETER_ADDR, 0x01, cfg)) { g_ai++; continue; }              // nothing answering on this port: no sample
    if (!(cfg & 0x8000)) {                                                     // still converting
      if (now - g_t_conv > ADS_TIMEOUT_MS) g_ai++;
      return;
    }
    if (!rd16(PA_VOLTMETER_ADDR, 0x00, r)) { g_ai++; continue; }
    const int16_t raw = (int16_t)r;
    if (g_pass == 0 && abs((int)raw) > 32000) { h.coarse = true; g_ai++; continue; }   // clipped: needs the coarse range
    addSample(c, og_ads1115_volts(raw, g_pass ? 4.096f : 0.256f));
    h.got = true; h.coarse = false;
    g_ai++;
    return;                                                                    // one voltmeter per call
  }
  // every voltmeter of this pass is read
  if (g_pass == 0) {
    bool any = false;
    for (int k = 0; k < g_nact; k++) if (g_ch[g_act[k]].coarse) any = true;
    if (any) { g_pass = 1; g_phase = 0; g_ai = 0; return; }
  }
  g_errs = 0;
  paSampleDone();
}

// ---- M5 PbHUB: read the analog value of every port in use ---------------------
static void pollPbHub(uint32_t now) {
  if (!g_sampling) {
    if ((int32_t)(now - g_next_cycle) < 0) return;
    g_sampling = true; g_ns = 0; g_next_sample = now;
  }
  if ((int32_t)(now - g_next_sample) < 0) return;
  for (int k = 0; k < g_nact; k++) {
    const int c = g_act[k];
    uint16_t raw;
    if (!pb_read(g_addr, g_ch[c].reg, raw)) { i2cErr(); return; }              // drop the partial tick
    if (!g_pb_hinted && og_pbhub_too_big_for_10bit(raw)) {
      g_pb_hinted = true;
      Serial.printf("[oil] PbHUB gave %u, more than a 10-bit reading can be - set PbHUB reading to 12-bit on the phone page\n", (unsigned)raw);
    }
    addSample(c, og_pbhub_volts(raw));
  }
  sampleDone();
  g_next_sample = now + PB_SAMPLE_GAP_MS;
  if (g_ns >= OG_SAMPLES) finishCycle();
}

// ---- public ------------------------------------------------------------
void OilSensor_Begin() {
  Wire.setClock(OG_I2C_HZ);
  clearChannels();
  probe();
  if (g_kind == OIL_KIND_NONE) {
    if (g_set.sensor_mode >= 3)
      Serial.printf("[oil] no %s found yet at 0x%02X - will keep looking\n",
                    g_set.sensor_mode == 3 ? "ADS1115" : g_set.sensor_mode == 4 ? "PaHUB" : "PbHUB", og_hub_addr(g_set));
    else
      Serial.println("[oil] no sensor board found yet (M5 @0x49 or INA3221 @0x40-0x43) - will keep looking");
  }
}

void OilSensor_Poll() {
  uint32_t now = millis();
  if (g_kind == OIL_KIND_NONE) {
    if (now - g_last_probe >= 2000) { g_last_probe = now; probe(); }
    return;
  }
  switch (g_kind) {
    case OIL_KIND_M5:      pollM5(now);     break;
    case OIL_KIND_ADS1115: pollADS(now);    break;
    case OIL_KIND_PAHUB:   pollPaHub(now);  break;
    case OIL_KIND_PBHUB:   pollPbHub(now);  break;
    default:               pollINA(now);    break;
  }
}

ChanReading OilSensor_Get(OgChan ch) {
  ChanReading r; r.val = NAN; r.volts = NAN;
  if (ch < 0 || ch >= OG_NCH) { r.fault = OIL_FAULT_NO_SENSOR; return r; }
  if (!configured(ch))              { r.fault = OIL_FAULT_OFF; return r; }
  if (g_kind == OIL_KIND_NONE)      { r.fault = OIL_FAULT_NO_SENSOR; return r; }
  if (!g_ch[ch].on)                 { r.fault = OIL_FAULT_UNSUPPORTED; return r; }
  r.fault = g_ch[ch].fault; r.val = g_ch[ch].val; r.volts = g_ch[ch].v;
  if (millis() - g_updated > OG_STALE_MS) { r.fault = OIL_FAULT_NO_DATA; r.val = NAN; }
  return r;
}

OilKind OilSensor_Kind() { return g_kind; }

// Sensor choice / channels / calibration changed (from the phone page): forget
// the current board, restart smoothing, and look again straight away.
void OilSensor_Reconfigure() {
  g_kind = OIL_KIND_NONE; g_addr = 0;
  g_sampling = false; g_waiting = false; g_ns = 0;
  clearChannels();
  g_updated = millis(); g_last_probe = 0;
}

void OilSensor_RestartSmoothing() { for (int c = 0; c < OG_NCH; c++) g_ch[c].have_ema = false; }

const char* OilSensor_Name() {
  static char buf[32];
  if (g_kind == OIL_KIND_M5) { snprintf(buf, sizeof buf, "M5 METER  0x%02X", g_addr); return buf; }
  if (g_kind == OIL_KIND_NONE) return "NO SENSOR";
  const char* nm = g_kind == OIL_KIND_ADS1115 ? "ADS1115" : g_kind == OIL_KIND_PAHUB ? "PAHUB" : g_kind == OIL_KIND_PBHUB ? "PBHUB" : "INA3221";
  if (g_nch <= 1) { snprintf(buf, sizeof buf, "%s  0x%02X  CH%d", nm, g_addr, (int)g_set.ina_channel); return buf; }
  // several in use: O = oil, B = boost, A = AFR, each followed by its channel
  int n = snprintf(buf, sizeof buf, "%s  0x%02X  O%d", nm, g_addr, (int)g_set.ina_channel);
  if (g_ch[OG_CH_BOOST].on && n < (int)sizeof buf) n += snprintf(buf + n, sizeof buf - n, " B%d", (int)g_set.boost_ch);
  if (g_ch[OG_CH_AFR].on   && n < (int)sizeof buf) n += snprintf(buf + n, sizeof buf - n, " A%d", (int)g_set.afr_ch);
  return buf;
}
