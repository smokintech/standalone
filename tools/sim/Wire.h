#pragma once
#include "Arduino.h"
// Simulated bus. Chips tests can plug / unplug:
//   m5      an M5 voltmeter (ADS1115 @ 0x49, differential, behind an input divider of 62.8)
//   ina     an INA3221 (address ina_addr)
//   ads     a plain ADS1115 breakout (address ads_addr), four single-ended inputs ain_v[0..3]
//   pahub   an M5 PaHUB (PCA9548A @ pa_addr) with an M5 voltmeter on every port whose pv[] is >= 0
//   pbhub   an M5 PbHUB (@ pb_addr), analog value of port p = pb_v[p] volts * counts-per-volt
struct SimChips {
  bool m5 = false; bool ina = false; uint8_t ina_addr = 0x40;
  float sender_v = 2.5f;           // volts on the oil sender wire (INA channel 1 / the M5)
  float ch_v[4] = { 0, -1, -1, -1 }; // INA channels 1..3 (index 1..3); -1 = follow sender_v
  uint16_t ina_cfg = 0; int ads_starts = 0, ads_coarse = 0; int ina_reads[4] = {0,0,0,0};
  uint16_t ads_cfg = 0; uint32_t ads_done = 0;

  bool ads = false; uint8_t ads_addr = 0x48; float ain_v[4] = { 0, 0, 0, 0 };
  uint16_t ads_g_cfg = 0; uint32_t ads_g_done = 0; int ads_g_starts = 0; int ads_g_mux_seen[4] = {0,0,0,0};

  bool pahub = false; uint8_t pa_addr = 0x70; uint8_t pa_sel = 0;
  float pv[6] = { -1, -1, -1, -1, -1, -1 };          // sender volts at the voltmeter on each port; < 0 = no voltmeter there
  uint16_t pa_cfg[6] = {0,0,0,0,0,0}; uint32_t pa_done[6] = {0,0,0,0,0,0};
  int pa_selects = 0, pa_starts = 0, pa_coarse = 0; int pa_started[6] = {0,0,0,0,0,0};
  bool pa_nak_select = false;                         // hub stops answering

  bool pbhub = false; uint8_t pb_addr = 0x61; float pb_v[6] = { 0, 0, 0, 0, 0, 0 };
  float pb_counts_per_v = 1024.0f / 5.0f;             // what the (simulated) hub's ADC gives per volt
  int pb_reads[6] = {0,0,0,0,0,0};
  bool touch = true;                                  // the touch chip (0x15) is on the same bus: it must never be disturbed
  int touch_acks = 0;
};
extern SimChips sim;
class TwoWire {
  uint8_t addr = 0, reg = 0; uint8_t txbuf[8]; int txn = 0; uint8_t rx[2]; int rxi = 0, rxn = 0;
  // which chip sits at this address right now (0 = none)
  //   1 m5 (main bus)   2 ina   3 ads breakout   4 pahub mux   5 pbhub   6 an M5 voltmeter behind the PaHUB   7 touch
  int who(uint8_t a) {
    if (a == 0x15 && sim.touch) return 7;
    if (sim.pahub && a == sim.pa_addr) return sim.pa_nak_select ? 0 : 4;
    if (sim.pahub && a == 0x49 && sim.pa_sel) { for (int p = 0; p < 6; p++) if ((sim.pa_sel >> p) & 1) return sim.pv[p] >= 0 ? 6 : 0; return 0; }
    if (a == 0x49 && sim.m5) return 1;
    if (sim.ina && a == sim.ina_addr) return 2;
    if (sim.ads && a == sim.ads_addr) return 3;
    if (sim.pbhub && a == sim.pb_addr) return 5;
    return 0;
  }
  int port() { for (int p = 0; p < 6; p++) if ((sim.pa_sel >> p) & 1) return p; return -1; }
  static float fs_of(uint16_t cfg) { static const float t[8] = { 6.144f, 4.096f, 2.048f, 1.024f, 0.512f, 0.256f, 0.256f, 0.256f }; return t[(cfg >> 9) & 7]; }
  static uint16_t clip(long raw) { if (raw > 32767) raw = 32767; if (raw < -32768) raw = -32768; return (uint16_t)(int16_t)raw; }
public:
  void setClock(uint32_t) {}
  void beginTransmission(uint8_t a) { addr = a; txn = 0; }
  size_t write(uint8_t b) { if (txn < 8) txbuf[txn++] = b; return 1; }
  uint8_t endTransmission(bool = true) {
    const int w = who(addr);
    if (!w) return 2;
    if (w == 7) { sim.touch_acks++; return 0; }
    if (w == 4) {                                     // PCA9548A: the one data byte is the port bits
      if (txn == 1) { sim.pa_sel = txbuf[0] & 0x3F; sim.pa_selects++; }
      return 0;
    }
    if (txn >= 1) reg = txbuf[0];
    if (w == 5) return 0;                             // PbHUB: the command byte only picks what the next read returns
    if (txn == 3) {
      uint16_t v = (uint16_t)(txbuf[1] << 8 | txbuf[2]);
      if (w == 1 && reg == 0x01 && (v & 0x8000)) {
        sim.ads_cfg = v; sim.ads_starts++; sim.ads_done = g_now + 8;
        if ((v & 0x0E00) == 0x0200) sim.ads_coarse++;
      }
      if (w == 6 && reg == 0x01 && (v & 0x8000)) {
        const int p = port(); sim.pa_cfg[p] = v; sim.pa_done[p] = g_now + 8; sim.pa_starts++; sim.pa_started[p]++;
        if ((v & 0x0E00) == 0x0200) sim.pa_coarse++;
      }
      if (w == 3 && reg == 0x01 && (v & 0x8000)) {
        sim.ads_g_cfg = v; sim.ads_g_starts++; sim.ads_g_done = g_now + 4;
        const int m = (v >> 12) & 7; if (m >= 4) sim.ads_g_mux_seen[m - 4]++;
      }
      if (w == 2 && reg == 0x00) sim.ina_cfg = v;
    }
    return 0;
  }
  int requestFrom(int a, int n) {
    const int w = who((uint8_t)a);
    if (!w) return 0;
    if (w == 4) { if (n != 1) return 0; rx[0] = sim.pa_sel; rxi = 0; rxn = 1; return 1; }
    if (w == 5) {                                     // PbHUB analog read: command = port base | 6, two bytes, low first
      if (n != 2) return 0;
      static const uint8_t base[6] = { 0x40, 0x50, 0x60, 0x70, 0x80, 0xA0 };
      int p = -1; for (int i = 0; i < 6; i++) if ((base[i] | 0x06) == reg) p = i;
      if (p < 0) return 0;
      sim.pb_reads[p]++;
      long raw = lroundf(sim.pb_v[p] * sim.pb_counts_per_v); if (raw < 0) raw = 0; if (raw > 4095) raw = 4095;
      rx[0] = (uint8_t)(raw & 0xFF); rx[1] = (uint8_t)(raw >> 8); rxi = 0; rxn = 2; return 2;
    }
    if (n != 2) return 0;
    uint16_t v = 0;
    if (w == 1) {
      if (reg == 0x01) v = (g_now >= sim.ads_done) ? 0x8000 : 0x0000;
      else if (reg == 0x00) v = clip(lroundf(sim.sender_v / 62.8181818f / fs_of(sim.ads_cfg) * 32768.0f));
    } else if (w == 6) {
      const int p = port();
      if (reg == 0x01) v = (g_now >= sim.pa_done[p]) ? 0x8000 : 0x0000;
      else if (reg == 0x00) v = clip(lroundf(sim.pv[p] / 62.8181818f / fs_of(sim.pa_cfg[p]) * 32768.0f));
    } else if (w == 3) {
      if (reg == 0x01) v = (g_now >= sim.ads_g_done) ? 0x8000 : 0x0000;
      else if (reg == 0x00) { const int m = ((sim.ads_g_cfg >> 12) & 7) - 4; v = clip(lroundf((m >= 0 && m < 4 ? sim.ain_v[m] : 0.0f) / fs_of(sim.ads_g_cfg) * 32768.0f)); }
    } else {
      if (reg == 0xFE) v = 0x5449; else if (reg == 0xFF) v = 0x3220;
      else if (reg == 0x02 || reg == 0x04 || reg == 0x06) {
        int c = (reg - 0x02) / 2 + 1;
        float volts = sim.ch_v[c] >= 0 ? sim.ch_v[c] : sim.sender_v;
        v = (uint16_t)((int)(volts / 0.008f) << 3);
        sim.ina_reads[c]++;
      }
    }
    rx[0] = v >> 8; rx[1] = v & 0xFF; rxi = 0; rxn = 2; return 2;
  }
  int read() { return rxi < rxn ? rx[rxi++] : -1; }
};
extern TwoWire Wire;
