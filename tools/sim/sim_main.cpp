// Runs the real oil_sensor.cpp against simulated chips.
// g++ -std=c++14 -I. -I../.. sim_main.cpp ../../oil_sensor.cpp -o sim && ./sim
#include "Arduino.h"
#include "Wire.h"
#include "oil_sensor.h"
#include "oil_settings.h"
OilSettings g_set;
uint32_t g_now = 1000; SerialT Serial; SimChips sim; TwoWire Wire;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void run(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 5) { g_now += 5; OilSensor_Poll(); } }
static bool near(float a, float b, float t) { return fabsf(a - b) <= t; }

int main() {
  og_defaults(g_set);
  printf("1. nothing connected\n");
  OilSensor_Begin(); run(1000);
  CHECK(OilSensor_Get(OG_CH_OIL).fault == OIL_FAULT_NO_SENSOR, "expected NO_SENSOR");

  printf("2. M5 appears later (hot-plug), sender 2.5 V\n");
  sim.m5 = true; sim.sender_v = 2.5f; run(3500);
  { ChanReading r = OilSensor_Get(OG_CH_OIL);
    CHECK(OilSensor_Kind() == OIL_KIND_M5, "kind %d", OilSensor_Kind());
    CHECK(r.fault == OIL_FAULT_NONE && near(r.val, 72.5f, 0.6f), "fault %d psi %.2f (want 72.5)", r.fault, r.val);
    CHECK(near(r.volts, 2.5f, 0.02f), "volts %.3f", r.volts);
    printf("   -> %s  %.1f psi  %.3f V\n", OilSensor_Name(), r.val, r.volts); }

  printf("3. pressure follows the sender: 0.5 V, 3.5 V\n");
  sim.sender_v = 3.5f; run(2500);
  { ChanReading r = OilSensor_Get(OG_CH_OIL); CHECK(near(r.val, 108.8f, 0.6f), "psi %.2f want 108.8", r.val); }
  sim.sender_v = 0.5f; run(6000);
  { ChanReading r = OilSensor_Get(OG_CH_OIL); CHECK(r.fault == OIL_FAULT_NONE && near(r.val, 0.0f, 0.3f), "fault %d psi %.2f", r.fault, r.val); }

  printf("4. wiring faults are named, never a fake number\n");
  sim.sender_v = 0.0f; run(1500);
  { ChanReading r = OilSensor_Get(OG_CH_OIL); CHECK(r.fault == OIL_FAULT_WIRING_LOW && isnan(r.val), "low: fault %d psi %.2f", r.fault, r.val); }
  sim.sender_v = 4.95f; run(1500);
  { ChanReading r = OilSensor_Get(OG_CH_OIL); CHECK(r.fault == OIL_FAULT_WIRING_HIGH && isnan(r.val), "high: fault %d psi %.2f", r.fault, r.val); }
  sim.sender_v = 25.0f; run(1500);   // clips the fine range: must switch to the coarse one
  { ChanReading r = OilSensor_Get(OG_CH_OIL); CHECK(sim.ads_coarse > 0, "coarse range never used");
    CHECK(r.fault == OIL_FAULT_WIRING_HIGH, "25 V: fault %d", r.fault); }

  printf("5. M5 unplugged -> NO_SENSOR; plugged back -> recovers\n");
  sim.sender_v = 2.5f; run(1000); sim.m5 = false; run(1500);
  CHECK(OilSensor_Get(OG_CH_OIL).fault == OIL_FAULT_NO_SENSOR, "fault %d", OilSensor_Get(OG_CH_OIL).fault);
  CHECK(OilSensor_Kind() == OIL_KIND_NONE, "kind");
  sim.m5 = true; run(4000);
  { ChanReading r = OilSensor_Get(OG_CH_OIL); CHECK(r.fault == OIL_FAULT_NONE && near(r.val, 72.5f, 0.6f), "after replug fault %d psi %.2f", r.fault, r.val); }

  printf("6. a steady input gives a steady reading\n");
  { float lo = 1e9, hi = -1e9; sim.sender_v = 2.0f; run(6000);
    for (int i = 0; i < 200; i++) { run(50); float p = OilSensor_Get(OG_CH_OIL).val; if (p < lo) lo = p; if (p > hi) hi = p; }
    CHECK(hi - lo < 0.2f, "steady input wobbled %.2f..%.2f", lo, hi); }

  printf("7. M5 unplugged, INA3221 on the A0=VS address (0x41) found by scan\n");
  sim.m5 = false; run(1500);
  sim.ina = true; sim.ina_addr = 0x41; sim.sender_v = 2.5f; run(4000);
  { ChanReading r = OilSensor_Get(OG_CH_OIL);
    CHECK(OilSensor_Kind() == OIL_KIND_INA3221, "kind %d", OilSensor_Kind());
    CHECK(r.fault == OIL_FAULT_NONE && near(r.val, 72.5f, 0.8f), "fault %d psi %.2f", r.fault, r.val);
    CHECK(sim.ina_cfg == 0x4526, "INA config word 0x%04X (want 0x4526: ch1, avg16, bus continuous)", sim.ina_cfg);
    printf("   -> %s  %.1f psi  %.3f V\n", OilSensor_Name(), r.val, r.volts); }

  printf("8. both boards present -> the preferred one (M5) wins\n");
  sim.m5 = true; sim.ina_addr = 0x40; sim.ina = true;
  sim.ina = false; run(1500); sim.ina = true;     // force a re-probe through a NO_SENSOR gap
  sim.ina = false; sim.m5 = false; run(2500); sim.ina = true; sim.m5 = true; run(4000);
  CHECK(OilSensor_Kind() == OIL_KIND_M5, "kind %d", OilSensor_Kind());

  
  // ================= boost + AFR on INA3221 channels 2 and 3 =================
  auto P = [](OgChan c) { return OilSensor_Get(c); };
  auto reset_world = [&]() {                       // INA3221 only, settings as given in g_set
    sim.m5 = false; sim.ina = false; run(500);
    OilSensor_Reconfigure(); run(100);
    sim.ina = true; sim.ina_addr = 0x40;
  };

  printf("9. oil ch1 + boost ch2 + AFR ch3 on one INA3221\n");
  og_defaults(g_set); g_set.sensor_mode = 2; g_set.boost_ch = 2; g_set.afr_ch = 3;
  sim.sender_v = 2.5f; sim.ch_v[2] = 3.0f; sim.ch_v[3] = 2.35f; sim.ina_reads[1] = sim.ina_reads[2] = sim.ina_reads[3] = 0;
  reset_world(); run(4000);
  { ChanReading o = P(OG_CH_OIL), b = P(OG_CH_BOOST), a = P(OG_CH_AFR);
    CHECK(OilSensor_Kind() == OIL_KIND_INA3221, "kind");
    CHECK(sim.ina_cfg == 0x7326, "config word 0x%04X (want 0x7326: ch1+2+3, avg4, bus continuous)", sim.ina_cfg);
    CHECK(o.fault == OIL_FAULT_NONE && near(o.val, 72.5f, 0.8f), "oil: fault %d %.2f psi", o.fault, o.val);
    // Bosch-style 3 bar: 3.0 V -> 11 + 2.75*65.78 = 191.9 kPa abs -> (191.9-101.3)*0.145 = 13.1 psi
    CHECK(b.fault == OIL_FAULT_NONE && near(b.val, 13.1f, 0.6f), "boost: fault %d %.2f psi (want 13.1)", b.fault, b.val);
    CHECK(a.fault == OIL_FAULT_NONE && near(a.val, 14.7f, 0.1f), "afr: fault %d %.2f (want 14.7)", a.fault, a.val);
    CHECK(near(b.volts, 3.0f, 0.02f) && near(a.volts, 2.35f, 0.02f), "volts b %.3f a %.3f", b.volts, a.volts);
    CHECK(sim.ina_reads[1] > 20 && sim.ina_reads[2] > 20 && sim.ina_reads[3] > 20, "each channel read: %d %d %d", sim.ina_reads[1], sim.ina_reads[2], sim.ina_reads[3]);
    printf("   -> %s   oil %.1f psi, boost %.1f psi, AFR %.2f\n", OilSensor_Name(), o.val, b.val, a.val); }

  printf("10. channels are independent: AFR unplugged, boost keeps working, oil keeps working\n");
  sim.ch_v[3] = 0.0f; run(2000);
  { ChanReading o = P(OG_CH_OIL), b = P(OG_CH_BOOST), a = P(OG_CH_AFR);
    CHECK(a.fault == OIL_FAULT_WIRING_LOW && isnan(a.val), "afr fault %d val %.2f", a.fault, a.val);
    CHECK(b.fault == OIL_FAULT_NONE && near(b.val, 13.1f, 0.6f), "boost fault %d %.2f", b.fault, b.val);
    CHECK(o.fault == OIL_FAULT_NONE && near(o.val, 72.5f, 0.8f), "oil fault %d %.2f", o.fault, o.val); }
  sim.ch_v[3] = 2.35f; sim.ch_v[2] = 0.05f; run(2000);
  { ChanReading b = P(OG_CH_BOOST); CHECK(b.fault == OIL_FAULT_WIRING_LOW && isnan(b.val), "boost unplugged: fault %d", b.fault); }
  sim.ch_v[2] = 5.4f; run(2000);
  { ChanReading b = P(OG_CH_BOOST); CHECK(b.fault == OIL_FAULT_WIRING_HIGH && isnan(b.val), "boost >5.25 V: fault %d", b.fault); }
  sim.ch_v[2] = 1.6228f; run(3000);                 // ambient on the Bosch curve
  { ChanReading b = P(OG_CH_BOOST); CHECK(b.fault == OIL_FAULT_NONE && near(b.val, 0.0f, 0.3f), "ambient: %.2f psi", b.val); }
  sim.ch_v[2] = 0.60f; run(3000);                   // idle vacuum: 11 + 0.35*65.78 = 34 kPa -> -9.8 psi
  { ChanReading b = P(OG_CH_BOOST); CHECK(b.fault == OIL_FAULT_NONE && b.val < -9.0f && b.val > -10.6f, "vacuum: %.2f psi", b.val); }

  printf("11. only oil + AFR (boost off): 2 channels, and boost reports OFF\n");
  og_defaults(g_set); g_set.sensor_mode = 2; g_set.afr_ch = 2; sim.ch_v[2] = 2.35f;
  reset_world(); run(4000);
  { ChanReading b = P(OG_CH_BOOST), a = P(OG_CH_AFR);
    CHECK(b.fault == OIL_FAULT_OFF, "boost off: fault %d", b.fault);
    CHECK(a.fault == OIL_FAULT_NONE && near(a.val, 14.7f, 0.1f), "afr on ch2: fault %d %.2f", a.fault, a.val);
    CHECK(sim.ina_cfg == 0x6326, "config word 0x%04X (want 0x6326: ch1+2, avg4)", sim.ina_cfg); }

  printf("12. oil only is exactly as before (ch1, avg16, 0x4526) and the others are OFF\n");
  og_defaults(g_set); g_set.sensor_mode = 2;
  reset_world(); run(4000);
  CHECK(sim.ina_cfg == 0x4526, "config word 0x%04X", sim.ina_cfg);
  CHECK(P(OG_CH_BOOST).fault == OIL_FAULT_OFF && P(OG_CH_AFR).fault == OIL_FAULT_OFF, "others off");
  CHECK(P(OG_CH_OIL).fault == OIL_FAULT_NONE, "oil ok");

  printf("13. oil on INA channel 3, boost on channel 1 (any wiring order works)\n");
  og_defaults(g_set); g_set.sensor_mode = 2; g_set.ina_channel = 3; g_set.boost_ch = 1;
  sim.ch_v[1] = 3.0f; sim.ch_v[3] = 2.5f; sim.ch_v[2] = -1;
  reset_world(); run(4000);
  { ChanReading o = P(OG_CH_OIL), b = P(OG_CH_BOOST);
    CHECK(o.fault == OIL_FAULT_NONE && near(o.val, 72.5f, 0.8f), "oil on ch3: fault %d %.2f", o.fault, o.val);
    CHECK(b.fault == OIL_FAULT_NONE && near(b.val, 13.1f, 0.6f), "boost on ch1: fault %d %.2f", b.fault, b.val);
    CHECK(sim.ina_cfg == 0x5326, "config word 0x%04X (want 0x5326: ch1+3, avg4)", sim.ina_cfg); }

  printf("14. M5 Meter with boost switched on: oil works, boost says UNSUPPORTED (needs INA3221)\n");
  og_defaults(g_set); g_set.sensor_mode = 1; g_set.boost_ch = 2;
  sim.ch_v[1] = -1; sim.sender_v = 2.5f;
  sim.m5 = true; sim.ina = false; OilSensor_Reconfigure(); run(5000);
  { ChanReading o = P(OG_CH_OIL), b = P(OG_CH_BOOST);
    CHECK(OilSensor_Kind() == OIL_KIND_M5, "kind");
    CHECK(o.fault == OIL_FAULT_NONE && near(o.val, 72.5f, 0.8f), "oil on M5: %d %.2f", o.fault, o.val);
    CHECK(b.fault == OIL_FAULT_UNSUPPORTED, "boost on M5: fault %d", b.fault); }

  printf("15. readings go stale if the bus goes away (all channels, not only oil)\n");
  og_defaults(g_set); g_set.sensor_mode = 2; g_set.boost_ch = 2; g_set.afr_ch = 3;
  sim.ch_v[1] = -1; sim.ch_v[2] = 3.0f; sim.ch_v[3] = 2.35f; sim.sender_v = 2.5f; sim.m5 = false;
  reset_world(); run(3000);
  sim.ina = false; run(300);
  { ChanReading b = P(OG_CH_BOOST), a = P(OG_CH_AFR);
    CHECK(b.fault != OIL_FAULT_NONE && isnan(b.val) && a.fault != OIL_FAULT_NONE && isnan(a.val), "boost fault %d afr fault %d", b.fault, a.fault); }


  // ================= ADS1115 breakout, M5 PaHUB + voltmeters, M5 PbHUB =================
  auto world = [&](int mode, int oil, int boost, int afr) {         // nothing plugged in yet; settings for this board
    sim.m5 = sim.ina = sim.ads = sim.pahub = sim.pbhub = false; sim.pa_sel = 0; sim.pa_nak_select = false;
    for (int i = 0; i < 6; i++) { sim.pv[i] = -1; sim.pb_v[i] = 0; }
    sim.pb_counts_per_v = 1024.0f / 5.0f; sim.ads_addr = 0x48; sim.pa_addr = 0x70; sim.pb_addr = 0x61;
    sim.pa_starts = 0; sim.pa_coarse = 0; sim.pa_selects = 0;
    og_defaults(g_set); g_set.sensor_mode = (uint8_t)mode; g_set.ina_channel = (uint8_t)oil; g_set.boost_ch = (uint8_t)boost; g_set.afr_ch = (uint8_t)afr;
    OilSensor_Reconfigure(); run(300);
  };

  printf("16. plain ADS1115 on A0/A1/A2: oil + boost + AFR\n");
  world(3, 1, 2, 3);
  CHECK(OilSensor_Get(OG_CH_OIL).fault == OIL_FAULT_NO_SENSOR, "nothing there yet: %d", OilSensor_Get(OG_CH_OIL).fault);
  sim.ads = true; sim.ain_v[0] = 2.5f; sim.ain_v[1] = 3.0f; sim.ain_v[2] = 2.35f; run(4000);
  { ChanReading o = P(OG_CH_OIL), b = P(OG_CH_BOOST), a = P(OG_CH_AFR);
    CHECK(OilSensor_Kind() == OIL_KIND_ADS1115, "kind %d", OilSensor_Kind());
    CHECK(o.fault == OIL_FAULT_NONE && near(o.val, 72.5f, 0.3f), "oil %d %.2f", o.fault, o.val);
    CHECK(b.fault == OIL_FAULT_NONE && near(b.val, 13.1f, 0.4f), "boost %d %.2f", b.fault, b.val);
    CHECK(a.fault == OIL_FAULT_NONE && near(a.val, 14.69f, 0.05f), "afr %d %.2f", a.fault, a.val);
    CHECK(near(o.volts, 2.5f, 0.01f), "oil volts %.4f", o.volts);
    CHECK(sim.ads_g_mux_seen[0] > 0 && sim.ads_g_mux_seen[1] > 0 && sim.ads_g_mux_seen[2] > 0 && sim.ads_g_mux_seen[3] == 0, "inputs used %d %d %d %d",
          sim.ads_g_mux_seen[0], sim.ads_g_mux_seen[1], sim.ads_g_mux_seen[2], sim.ads_g_mux_seen[3]);
    CHECK(strcmp(OilSensor_Name(), "ADS1115  0x48  O1 B2 A3") == 0, "name '%s'", OilSensor_Name());
    printf("   -> %s  oil %.1f psi, boost %.1f psi, AFR %.2f\n", OilSensor_Name(), o.val, b.val, a.val); }
  printf("   wiring faults are named, per input\n");
  sim.ain_v[1] = 0.0f; sim.ain_v[2] = 5.9f; run(2500);
  { ChanReading o = P(OG_CH_OIL), b = P(OG_CH_BOOST), a = P(OG_CH_AFR);
    CHECK(o.fault == OIL_FAULT_NONE, "oil unaffected: %d", o.fault);
    CHECK(b.fault == OIL_FAULT_WIRING_LOW && isnan(b.val), "boost 0 V: %d", b.fault);
    CHECK(a.fault == OIL_FAULT_WIRING_HIGH && isnan(a.val), "afr 5.9 V: %d", a.fault); }
  printf("   an input divider of 2:1 is undone, and the trim applies\n");
  world(3, 1, 0, 0); sim.ads = true; g_set.in_div = 2.0f; sim.ain_v[0] = 1.25f; run(3000);
  { ChanReading o = P(OG_CH_OIL); CHECK(o.fault == OIL_FAULT_NONE && near(o.val, 72.5f, 0.3f) && near(o.volts, 2.5f, 0.01f), "divider: %d %.2f %.3f V", o.fault, o.val, o.volts); }
  g_set.adc_scale = 1.02f; run(3000);
  { ChanReading o = P(OG_CH_OIL); CHECK(near(o.volts, 2.55f, 0.02f), "scale 1.02: %.3f V", o.volts); }
  printf("   oil on input 4 (A3), at another address (0x4A)\n");
  world(3, 4, 0, 0); g_set.hub_addr = 0x4A; sim.ads = true; sim.ads_addr = 0x4A; sim.ain_v[3] = 2.5f; run(3000);
  { ChanReading o = P(OG_CH_OIL); CHECK(o.fault == OIL_FAULT_NONE && near(o.val, 72.5f, 0.3f) && sim.ads_g_mux_seen[3] > 0, "A3: %d %.2f", o.fault, o.val);
    CHECK(strcmp(OilSensor_Name(), "ADS1115  0x4A  CH4") == 0, "name '%s'", OilSensor_Name()); }
  printf("   the board is chosen, not guessed: Auto-detect ignores it\n");
  world(0, 1, 0, 0); sim.ads = true; sim.ads_addr = 0x48; sim.ain_v[0] = 2.5f; run(4000);
  CHECK(OilSensor_Kind() == OIL_KIND_NONE, "auto picked kind %d", OilSensor_Kind());
  printf("   unplugged mid-run -> NO SENSOR, plugged back -> recovers\n");
  world(3, 1, 0, 0); sim.ads = true; sim.ain_v[0] = 2.5f; run(3000); sim.ads = false; run(2500);
  CHECK(P(OG_CH_OIL).fault == OIL_FAULT_NO_SENSOR && OilSensor_Kind() == OIL_KIND_NONE, "unplugged: %d", P(OG_CH_OIL).fault);
  sim.ads = true; run(5000); CHECK(P(OG_CH_OIL).fault == OIL_FAULT_NONE && near(P(OG_CH_OIL).val, 72.5f, 0.3f), "replug: %d", P(OG_CH_OIL).fault);

  printf("17. M5 PaHUB with three M5 voltmeters (all at 0x49) - oil port 2, boost port 1, AFR port 6\n");
  world(4, 2, 1, 6);
  sim.pahub = true; sim.pv[1] = 2.5f; sim.pv[0] = 3.0f; sim.pv[5] = 2.35f; run(4000);
  { ChanReading o = P(OG_CH_OIL), b = P(OG_CH_BOOST), a = P(OG_CH_AFR);
    CHECK(OilSensor_Kind() == OIL_KIND_PAHUB, "kind %d", OilSensor_Kind());
    CHECK(o.fault == OIL_FAULT_NONE && near(o.val, 72.5f, 0.3f), "oil %d %.2f", o.fault, o.val);
    CHECK(b.fault == OIL_FAULT_NONE && near(b.val, 13.1f, 0.4f), "boost %d %.2f", b.fault, b.val);
    CHECK(a.fault == OIL_FAULT_NONE && near(a.val, 14.69f, 0.05f), "afr %d %.2f", a.fault, a.val);
    CHECK(near(o.volts, 2.5f, 0.02f) && near(b.volts, 3.0f, 0.02f), "volts %.3f %.3f", o.volts, b.volts);
    CHECK(strcmp(OilSensor_Name(), "PAHUB  0x70  O2 B1 A6") == 0, "name '%s'", OilSensor_Name());
    // three voltmeters convert at once: far fewer waits than one after another
    CHECK(sim.pa_started[1] > 0 && sim.pa_started[0] > 0 && sim.pa_started[5] > 0 && sim.pa_started[2] == 0, "ports started %d %d %d %d", sim.pa_started[0], sim.pa_started[1], sim.pa_started[5], sim.pa_started[2]);
    CHECK(sim.pa_coarse == 0, "no coarse passes needed at 2.5 V (%d)", sim.pa_coarse);
    printf("   -> %s  oil %.1f psi, boost %.1f psi, AFR %.2f\n", OilSensor_Name(), o.val, b.val, a.val); }
  printf("   the readings keep arriving (no stall behind the hub)\n");
  { uint32_t t0 = g_now; int fresh = 0, stale = 0; float last = -1;    // worst case: the loop polls only once per 5 ms
    for (int i = 0; i < 40; i++) { sim.pv[1] = 2.0f + 0.01f * i; run(100); float v = P(OG_CH_OIL).volts; if (v != last) fresh++; last = v;
      if (P(OG_CH_OIL).fault != OIL_FAULT_NONE) stale++; }
    CHECK(fresh >= 7 && stale == 0, "%d fresh oil readings in %u ms, %d stale/fault", fresh, (unsigned)(g_now - t0), stale); }
  sim.pv[1] = 2.5f; sim.pv[0] = 3.0f; run(3000);
  printf("   a voltmeter that is not there: only that input reports NO SENSOR\n");
  sim.pv[0] = -1; run(5000);
  { ChanReading o = P(OG_CH_OIL), b = P(OG_CH_BOOST), a = P(OG_CH_AFR);
    CHECK(b.fault == OIL_FAULT_NO_SENSOR && isnan(b.val), "boost: %d", b.fault);
    CHECK(o.fault == OIL_FAULT_NONE && near(o.val, 72.5f, 0.3f) && a.fault == OIL_FAULT_NONE, "oil %d %.2f afr %d", o.fault, o.val, a.fault);
    CHECK(OilSensor_Kind() == OIL_KIND_PAHUB, "hub still found (kind %d)", OilSensor_Kind()); }
  sim.pv[0] = 3.0f; run(4000);
  CHECK(P(OG_CH_BOOST).fault == OIL_FAULT_NONE && near(P(OG_CH_BOOST).val, 13.1f, 0.4f), "boost back: %d %.2f", P(OG_CH_BOOST).fault, P(OG_CH_BOOST).val);
  printf("   a high voltage clips the fine range: that voltmeter alone is read on the coarse range\n");
  sim.pv[1] = 25.0f; run(3000);
  { ChanReading o = P(OG_CH_OIL), a = P(OG_CH_AFR); CHECK(sim.pa_coarse > 0, "coarse range never used");
    CHECK(o.fault == OIL_FAULT_WIRING_HIGH, "oil 25 V: %d", o.fault);
    CHECK(a.fault == OIL_FAULT_NONE && near(a.val, 14.69f, 0.05f), "AFR unaffected: %d %.2f", a.fault, a.val); }
  sim.pv[1] = 2.5f; run(3000);
  printf("   the hub stops answering -> all NO SENSOR; it comes back -> recovers\n");
  sim.pa_nak_select = true; run(2500);
  CHECK(OilSensor_Kind() == OIL_KIND_NONE && P(OG_CH_OIL).fault == OIL_FAULT_NO_SENSOR, "hub gone: kind %d fault %d", OilSensor_Kind(), P(OG_CH_OIL).fault);
  sim.pa_nak_select = false; run(6000);
  CHECK(OilSensor_Kind() == OIL_KIND_PAHUB && P(OG_CH_OIL).fault == OIL_FAULT_NONE && near(P(OG_CH_OIL).val, 72.5f, 0.3f), "hub back: kind %d fault %d", OilSensor_Kind(), P(OG_CH_OIL).fault);
  printf("   PaHUB at another address (DIP switch 0x73), oil only\n");
  world(4, 3, 0, 0); g_set.hub_addr = 0x73; sim.pahub = true; sim.pa_addr = 0x73; sim.pv[2] = 2.5f; run(3000);
  CHECK(P(OG_CH_OIL).fault == OIL_FAULT_NONE && near(P(OG_CH_OIL).val, 72.5f, 0.3f), "0x73: %d", P(OG_CH_OIL).fault);
  printf("   the touch chip is on the same bus and is never talked to by the hub driver\n");
  { sim.touch_acks = 0; run(2000); CHECK(sim.touch_acks == 0, "touch chip was addressed %d times", sim.touch_acks); }

  printf("18. M5 PbHUB: ports as analog inputs (10-bit, 0-5 V as M5 documents)\n");
  world(5, 1, 5, 6);                                 // oil port 1 (0x46), boost port 5 (0x86), AFR port 6 (0xA6)
  sim.pbhub = true; sim.pb_v[0] = 2.5f; sim.pb_v[4] = 3.0f; sim.pb_v[5] = 2.35f; run(3000);
  { ChanReading o = P(OG_CH_OIL), b = P(OG_CH_BOOST), a = P(OG_CH_AFR);
    CHECK(OilSensor_Kind() == OIL_KIND_PBHUB, "kind %d", OilSensor_Kind());
    CHECK(o.fault == OIL_FAULT_NONE && near(o.val, 72.5f, 1.0f), "oil %d %.2f (10-bit steps are ~0.12 psi)", o.fault, o.val);
    CHECK(b.fault == OIL_FAULT_NONE && near(b.val, 13.1f, 0.6f), "boost %d %.2f", b.fault, b.val);
    CHECK(a.fault == OIL_FAULT_NONE && near(a.val, 14.69f, 0.1f), "afr %d %.2f", a.fault, a.val);
    CHECK(sim.pb_reads[0] > 0 && sim.pb_reads[4] > 0 && sim.pb_reads[5] > 0 && sim.pb_reads[1] == 0, "ports read %d %d %d, unused %d", sim.pb_reads[0], sim.pb_reads[4], sim.pb_reads[5], sim.pb_reads[1]);
    CHECK(strcmp(OilSensor_Name(), "PBHUB  0x61  O1 B5 A6") == 0, "name '%s'", OilSensor_Name());
    printf("   -> %s  oil %.1f psi, boost %.1f psi, AFR %.2f\n", OilSensor_Name(), o.val, b.val, a.val); }
  printf("   wiring faults: 0 V = low; the oil sender at 5 V = high (a 10-bit 0-5 V hub cannot read above 5 V)\n");
  sim.pb_v[4] = 0.0f; sim.pb_v[0] = 5.0f; sim.pb_v[5] = 5.0f; run(2500);
  { CHECK(P(OG_CH_BOOST).fault == OIL_FAULT_WIRING_LOW, "boost 0 V: %d", P(OG_CH_BOOST).fault);
    CHECK(P(OG_CH_OIL).fault == OIL_FAULT_WIRING_HIGH, "oil 5 V: %d", P(OG_CH_OIL).fault);
    CHECK(P(OG_CH_AFR).fault == OIL_FAULT_NONE, "AFR 5 V is a valid wideband output: %d", P(OG_CH_AFR).fault); }
  printf("   a hub that really reads 12-bit while set to 10-bit is a fault, never a wrong number\n");
  world(5, 1, 0, 0); sim.pbhub = true; sim.pb_counts_per_v = 4096.0f / 5.0f; sim.pb_v[0] = 2.5f; run(3000);
  CHECK(P(OG_CH_OIL).fault == OIL_FAULT_WIRING_HIGH && isnan(P(OG_CH_OIL).val), "12-bit hub, 10-bit setting: %d", P(OG_CH_OIL).fault);
  printf("   ...and with the right setting it reads correctly (12-bit 0-5 V; then 12-bit 0-3.3 V)\n");
  g_set.pb_adc = 2; OilSensor_Reconfigure(); run(3500);
  CHECK(P(OG_CH_OIL).fault == OIL_FAULT_NONE && near(P(OG_CH_OIL).val, 72.5f, 0.3f), "12-bit 5 V: %d %.2f", P(OG_CH_OIL).fault, P(OG_CH_OIL).val);
  g_set.pb_adc = 1; sim.pb_counts_per_v = 4096.0f / 3.3f; sim.pb_v[0] = 2.5f / 2.0f; g_set.in_div = 2.0f; OilSensor_Reconfigure(); run(3500);
  CHECK(P(OG_CH_OIL).fault == OIL_FAULT_NONE && near(P(OG_CH_OIL).val, 72.5f, 0.3f), "12-bit 3.3 V + 2:1 divider: %d %.2f", P(OG_CH_OIL).fault, P(OG_CH_OIL).val);
  printf("   PbHUB unplugged -> NO SENSOR, back -> recovers; not found by Auto-detect\n");
  sim.pbhub = false; run(2500); CHECK(P(OG_CH_OIL).fault == OIL_FAULT_NO_SENSOR && OilSensor_Kind() == OIL_KIND_NONE, "unplugged %d", P(OG_CH_OIL).fault);
  sim.pbhub = true; run(5000); CHECK(P(OG_CH_OIL).fault == OIL_FAULT_NONE, "replug %d", P(OG_CH_OIL).fault);
  world(0, 1, 0, 0); sim.pbhub = true; sim.pahub = true; run(4000); CHECK(OilSensor_Kind() == OIL_KIND_NONE, "auto ignores hubs: %d", OilSensor_Kind());

  printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
  return fails != 0;
}
