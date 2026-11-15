# OilGauge370Z

A standalone oil pressure gauge with optional boost and air/fuel ratio (AFR) screens. It runs on one Waveshare ESP32-S3 round touch screen and reads its sensors directly. 

It was built for a Nissan 370Z, which is where the project name and the factory defaults come from. Nothing in it is specific to that car. It reads ordinary analogue sensors (a 0.5–4.5 V oil pressure sender, a MAP sensor, a wideband controller's analogue output) through a small I2C measuring board, so it works on any car with sensors like those. Five kinds of board are supported (section 4). Where your sensors differ, set the sender range, curves and limits on the phone page.

You set it up from your phone. The gauge makes its own Wi-Fi hotspot, and a settings page on it lets you pick screens, sensors, limits and calibration. Firmware updates also go over that Wi-Fi. You can also design your own screens in a PC layout editor and upload them from the same page (section 5.8), and a **Run demo** button shows every gauge moving with invented readings, without the car running (section 5.9).

**Version:** v1.5.0

---

## Contents

1. [At a glance](#1-at-a-glance)
2. [Design overview](#2-design-overview)
3. [Building and flashing](#3-building-and-flashing)
4. [Wiring](#4-wiring)
5. [How it works](#5-how-it-works)
6. [Using the gauge](#6-using-the-gauge)
7. [The phone settings page](#7-the-phone-settings-page)
8. [Reference](#8-reference)
9. [Before you trust it](#9-before-you-trust-it)
10. [Project layout and tests](#10-project-layout-and-tests)

---

## 1. At a glance

| | |
|---|---|
| **Display** | Waveshare ESP32-S3-Touch-LCD-2.1 (480×480 round, curved glass, capacitive touch, onboard buzzer) |
| **Sensor board** | One of five, set under Inputs → Board on the phone page (details in section 4): |
| | **Auto-detect** (default) finds an **M5 Meter ISOLATED / M5 Voltmeter** (ADS1115 at 0x49, one input, oil only) or an **Adafruit INA3221** (three inputs). |
| | You can instead choose: a plain **ADS1115 breakout** (4 inputs), an **M5 PaHUB with M5 voltmeters** (one voltmeter per port, up to 6) or an **M5 PbHUB** ("I/O Hub 1 to 6", its 6 ports as analog inputs). These three are never auto-detected. |
| **Gauges** | Oil pressure (always). Boost and AFR (any board except the single-input M5 Meter). |
| **Screens** | Up to 4, each a single gauge or a split with two gauges one above the other. |
| **Alerts** | Low oil pressure: red ring flash, banner and buzzer. Boost and AFR change colour only. |
| **Demo** | *Run demo* on the phone page animates every gauge with invented readings for 2 minutes. |
| **Setup** | Phone, over the gauge's own Wi-Fi at `http://192.168.4.1/`. No home network or app. |
| **Updates** | Over Wi-Fi from the same page, after the first USB flash. |
| **Toolchain** | Arduino IDE, ESP32 core 3.0.x, LVGL 8.3.10 |

---

## 2. Design overview

### 2.1 Architecture

```
  Oil sender ─┐   M5 Meter / INA3221 / ADS1115 /
  MAP sensor ─┼─► PaHUB + M5 voltmeters / PbHUB
  Wideband ───┘            │ I2C (GPIO15 / GPIO7)
                           ▼
                ┌───────────────────────────────┐
                │  ESP32-S3                     │
                │                               │
                │  sensor reader ► conversions  │
                │        │            │         │
                │        ▼            ▼         │
                │   LVGL dials    alert logic   │──► round display
                │        ▲            │         │──► buzzer
                │   touch input       │         │
                │                               │
                │  Wi-Fi hotspot + web server   │◄──► phone
                │  settings in flash (NVS)      │
                └───────────────────────────────┘
```

Everything runs in one cooperative loop in a single task. That loop services the display and touch, then the sensor reader, then the web server. Because only that loop talks to the I2C bus, the touch chip, IO expander and sensor board never contend for it. A PaHUB's port selection only affects the chips behind the hub, never the touch chip or IO expander.

### 2.2 Design principles

- **It never shows a made-up number.** If the sensor is missing, unplugged, shorted or silent, the dial says so in words (`NO SENSOR`, `CHECK WIRING`, `NO DATA`). It does not show `0`.
- **Each gauge fails on its own.** An unplugged AFR lead does not affect the oil or boost readings. With a PaHUB, an unplugged voltmeter only affects its own input.
- **Auto-detect never changes.** The ADS1115, PaHUB and PbHUB are used only when you choose them, so adding one to the bus cannot change a working setup.
- **Invalid settings are refused, not clamped.** A setting that is out of range is rejected with a reason, and nothing is half-applied. The page never reports "saved" unless the write to flash actually succeeded.
- **Wi-Fi is off unless you ask for it.** It is off at power-up, switches on with a swipe, and switches itself off after a period of no use.
- **The Wi-Fi password is never drawn on the screen**, so anyone looking at the car cannot read it off the dial.
- **Only the oil alarm is loud.** Boost and AFR use colour only, so a gauge you glance at while driving does not nag.
- **The arithmetic is separate from the hardware.** Conversions, limits, alert levels and settings validation live in plain C++ headers with no Arduino or LVGL dependencies. They are tested on a PC.

### 2.3 Software modules

| Module | Role |
|---|---|
| `OilGauge370Z.ino` | `setup()` and the main loop |
| `oil_config.h` | Factory defaults (wiring, sender range, thresholds, Wi-Fi) |
| `oil_settings.*` | Phone-editable settings: validation, presets, screen layouts, saving to flash |
| `oil_sensor.*` | Non-blocking readers for the M5 Meter, INA3221, plain ADS1115, PaHUB + M5 voltmeters and PbHUB; auto-detection of the first two |
| `oil_math.h` | Volts → psi / boost / AFR (and the ADS1115 / PbHUB count → volts conversions), and the colour and alert levels |
| `oil_ui.*` | Single and split dials, page dots, alert banner, touch gestures, the demo (readings substitution, DEMO tag) |
| `oil_demo.h` | The demo's invented readings, its time limit and the start / stop request check (pure) |
| `oil_web.*` | Hotspot, HTTP server, Wi-Fi name and password, firmware update |
| `oil_api.h` | Logic behind the page's data (live readings, zero boost, applying a form) |
| `oil_page.h` | The phone page, one self-contained HTML document |
| `oil_layout.*`, `oil_layout_store.h` | Designed screens: checking an uploaded layout, keeping it in flash (two alternating slots, checksummed), loading it at start-up |
| `Gauge_Layout.*`, `Gauge_Inputs.*` | The layout engine that draws designed screens, and the five readings it can show (shared with the editor's preview) |
| `ArduinoJson.h` | JSON parser used by the layout engine (bundled, MIT licence) |
| `board_pins.h` | Every pin, I2C address and panel timing number for the Waveshare board |
| `board_i2c.*` | The shared I2C bus |
| `board_expander.*` | The IO expander: panel and touch resets, panel chip select, buzzer |
| `board_panel.*` | The ST7701 round panel (set-up commands, RGB output) and the backlight |
| `board_touch.*` | The CST820 touch controller |
| `board_lvgl.*` | Connects LVGL to the panel and touch |
| `lv_conf.h` | LVGL configuration |

---

## 3. Building and flashing

### 3.1 Libraries

You need one library to install: **LVGL 8.3.10**. Do not use LVGL 9.x, which is incompatible. (ArduinoJson, used by the layout engine, is bundled in the sketch folder.)

- In the Arduino IDE open *Sketch → Include Library → Manage Libraries*, search for `lvgl` and install version **8.3.10**.
- Copy this project's `lv_conf.h` over `libraries/lvgl/src/lv_conf.h`. It enables the fonts the gauge uses and sets the LVGL memory to 128 KB.
- Wi-Fi, WebServer and Update come with the ESP32 core. No other libraries are needed.
- Restart the IDE after installing the library.

### 3.2 Arduino IDE settings

Install **esp32 by Espressif Systems**, version 3.0.x (built and tested against 3.0.7).

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash size | 16 MB |
| Partition scheme | 16MB Flash (3MB APP / 9.9MB FATFS), `app3M_fat9M_16MB` |
| PSRAM | OPI PSRAM |
| Flash mode | QIO 80 MHz (not OPI) |
| USB CDC on boot | Enabled |
| USB mode | Hardware CDC and JTAG |

The partition scheme matters: it leaves room for two firmware images, which is what makes over-the-air updates possible.

### 3.3 First flash (USB)

1. Open `OilGauge370Z.ino`.
2. Optionally edit `oil_config.h` (factory defaults). Everything in it can also be set later from the phone page.
3. Connect the screen over USB-C and upload. If the upload will not start, hold **BOOT**, tap **RESET**, release **BOOT**, then upload again.
4. Open the serial monitor at 115200 baud. You should see the boot banner, free memory and which sensor board was found.

### 3.4 Updating later (over Wi-Fi)

1. In the Arduino IDE use *Sketch → Export Compiled Binary*. Use the file named `OilGauge370Z.ino.bin`, **not** the ones with `bootloader` or `merged` in the name.
2. On the gauge, swipe up to switch Wi-Fi on and join its hotspot.
3. Open `http://192.168.4.1/`, go to the **Firmware** card and upload the `.bin`.
4. The gauge restarts by itself. Saved settings and Wi-Fi details are kept.

Do this only while parked: the gauge stops reading pressure while it installs. If an update fails, the old firmware keeps running.

---

## 4. Wiring

### 4.1 I2C connection

The gauge uses the screen's own I2C bus: **SDA = GPIO15, SCL = GPIO7**. The touch chip, IO expander, IMU and RTC share that bus. None of them use the sensor addresses (0x40–0x43, 0x49), nor those of the optional boards in 4.5 (0x48–0x4B, 0x61–0x68, 0x70–0x77), so there is no clash.

- Use the board's I2C connector or pads and a **3.3 V** supply for the sensor board.
- Check the SDA and SCL labels on your board before connecting.
- Keep the lead short. The bus runs at a conservative 100 kHz.

### 4.2 M5 Meter ISOLATED (oil only)

- SDA, SCL and GND go to the screen as above.
- Connect the sender signal to the M5's measuring input.
- The M5 is electrically isolated, so there is no common-ground concern.
- It has a single input, so it can read oil only.

### 4.3 Adafruit INA3221 (oil, boost and AFR)

- SDA, SCL, GND and VS to the screen (VS = 3.3 V).
- Each sensor's **signal wire goes to its channel's `Vin-` terminal**. Tie that channel's `Vin+` to `Vin-` (no shunt is used).
- Sensor grounds go to the INA3221 GND. This must be the **same ground as the screen and the car**, because the INA3221 is not isolated.
- Each sensor needs its own channel. Suggested layout: oil on channel 1, boost on channel 2, AFR on channel 3. The channels are chosen on the phone page.
- Address pad: A0 open or GND = 0x40, A0 to VS = 0x41, SDA = 0x42, SCL = 0x43. The gauge scans all four.
- Inputs tolerate up to 26 V.
- A MAP sensor needs its own 5 V supply (check its pinout). A wideband controller's analogue output is usually 0–5 V. Choose the preset or curve that matches how your controller is set up.

### 4.4 Both boards connected

If both the M5 and the INA3221 are present, Auto-detect uses the M5 by default. You can force the choice on the page (Inputs → Board). Boost and AFR cannot be read by the single-input M5 Meter, so select the INA3221 (or one of the boards in 4.5) if you want them.

| Board (Inputs → Board) | Inputs | Address | Found by Auto-detect | Boost / AFR |
|---|---|---|---|---|
| M5 Meter ISOLATED / M5 Voltmeter | 1 | 0x49 | yes | no (oil only) |
| INA3221 | 3 | 0x40–0x43 | yes | yes |
| ADS1115 breakout | 4 (A0–A3) | 0x48–0x4B | no | yes |
| M5 PaHUB + M5 voltmeters | 6 ports, one voltmeter each | hub 0x70–0x77 | no | yes |
| M5 PbHUB | 6 ports (IO0 analog) | 0x61–0x68 | no | yes |

### 4.5 Other boards (chosen on the phone page, never auto-detected)

These three are only used when you pick them under **Inputs → Board**. Auto-detect keeps looking for the M5 Meter and INA3221 exactly as before, so adding one of these to the bus cannot change a working setup. All sit on the same I2C bus (SDA = GPIO15, SCL = GPIO7, 3.3 V) and use addresses that do not clash with the screen's own chips. They have been tested against simulated chips on a PC, not yet on real hardware, so work through the checks in section 9 the first time.

**ADS1115 breakout** (address 0x48 to 0x4B, set by its ADDR pin; choose it under *Address*)
- Four inputs, A0 to A3 (the page calls them Input A0 to A3), each measured against the breakout's GND. Signals go to the AIN pins, grounds to the breakout GND, which must be the same ground as the screen and the car (it is not isolated).
- 16-bit, about 0.19 mV per step: finer than the INA3221.
- **It can only measure up to its own supply voltage plus 0.3 V.** From the gauge's 3.3 V supply, a 0.5 to 4.5 V sender or a 0 to 5 V wideband output would damage it. Put a voltage divider on each such input (for example two equal resistors give a ratio of 2) and enter the ratio under *Input divider*. The gauge multiplies the reading back up.
- Not the same as the M5 Meter / M5 Voltmeter unit, which has its own input divider and is found at 0x49 by Auto-detect. If an ADS1115 breakout is set to 0x49 and an M5 Meter is on the same bus, they clash; use another address.
- The *Input divider* and *ADS1115 / PbHUB scale and offset* (Calibration card) apply: sender volts = measured volts × divider × scale + offset.

**M5 PaHUB with M5 voltmeters** (PaHUB v2.0 / v2.1: PCA9548AP I2C multiplexer, address 0x70 to 0x77 by its DIP switch)
- The M5 voltmeter always has the address 0x49, so two of them cannot share a bus. The PaHUB gives each its own port: plug one voltmeter into each port you use, then give oil, boost and AFR a different port each on the page.
- The PaHUB's upstream (input) port goes to the screen's I2C bus. Its address is set by the DIP switch (0x70 by default); enter it under *Address* if you changed it. The gauge starts every voltmeter converting at once and reads them back, so three voltmeters are not slower than one.
- A voltmeter that is unplugged only makes its own input read NO SENSOR; the others carry on. If the hub itself stops answering, the whole board reads NO SENSOR and the gauge searches again.
- Each voltmeter is read exactly like the single M5 Meter (same range switching and the same *M5 scale / offset* calibration).

**M5 PbHUB** ("I/O Hub 1 to 6 Expansion Unit", STM32F030, address 0x61 to 0x68 by solder pads)
- This is a **GPIO / analog expander, not an I2C hub**: its six ports carry pins, not an I2C bus, so M5 voltmeters cannot be plugged into it. Here the gauge uses each port's analog input as a sensor input.
- A sensor's signal goes to the port's **IO0** pin (the yellow wire of the M5 cable), its ground to the port's GND. Only IO0 of each port has an analog input.
- Accuracy is the weak point: M5 documents a 10-bit converter over 0 to 5 V (about 5 mV, or 0.12 psi on a 100 psi sender, per step). Other documentation describes the hub as 12-bit. Because of that disagreement the page has a **PbHUB reading** setting: 10-bit 0 to 5 V (M5's documentation, the default), 12-bit 0 to 3.3 V, or 12-bit 0 to 5 V. If the hub really reads 12-bit while the page says 10-bit, the oil input shows CHECK WIRING (high) and the serial log says to change the setting, rather than showing a wrong number.
- **Check with a multimeter once.** The live volts on the page must match the voltmeter on the sender wire; if they do not, change *PbHUB reading* or use *Calibration*. This is the least certain of the boards, because the converter details are documented inconsistently.
- Do not feed it more than its own supply allows. Use the *Input divider* if your hub's input is limited to 3.3 V.
- A 10-bit, 0 to 5 V hub can never read above 5 V, so a boost or AFR wire shorted to a supply higher than that cannot be flagged as "too high" there.

On every board: oil, boost and AFR each need a different input; the lists on the page show only the inputs the chosen board has. Changing the board resets *Address* to the board's usual one.

### 4.6 Resolution

The INA3221 reads in 8 mV steps:

| Sensor | Approximate step |
|---|---|
| Oil sender, 0.5–4.5 V = 0–10 bar | 0.3 psi |
| 3 bar MAP sensor | 0.08 psi |
| 0–5 V AFR, spanning 10 AFR | 0.02 AFR |

That is fine for gauges. Median filtering and smoothing are applied on top. The M5 (ADS1115) is finer, which is why it is preferred for oil when both are available.

The other boards:

| Board | Step | Oil sender (0.5–4.5 V = 0–10 bar) |
|---|---|---|
| ADS1115 breakout | 0.19 mV | under 0.01 psi (before any divider, which scales it by its ratio) |
| M5 PaHUB + voltmeter | as the M5 Meter | as the M5 Meter |
| M5 PbHUB, 10-bit 0–5 V | 4.9 mV | about 0.12 psi |
| M5 PbHUB, 12-bit | 0.8–1.2 mV | about 0.03 psi |

---

## 5. How it works

### 5.1 Sensor reading

A sensor reader polls the board in small non-blocking steps, so the display and touch never wait on I2C.

- **Auto-detection.** At start-up it looks for an M5 at `0x49` and an INA3221 at `0x40`–`0x43`. If nothing is found it keeps looking every 2 seconds, so a board connected late is picked up. Boards can be unplugged and re-plugged while running. When you have chosen the ADS1115, PaHUB or PbHUB it looks only for that board, at its address, in the same way.
- **Sampling.** Each cycle takes 9 readings per channel, takes the **median** (to reject spikes), then applies an exponential smoothing filter (factor 0.4). A new reading is produced about every 250 ms.
- **M5 range switching.** The ADS1115 starts on its fine ±0.256 V range and switches to ±4.096 V if the signal clips.
- **INA3221 channels.** Only the channels you assign are read. With one channel the chip averages 16 samples. With several it averages 4, to keep the update rate up.
- **Plain ADS1115.** Each input is read in turn (single-shot, 250 samples a second, ±6.144 V range), so three inputs take about 3 times as long as one, still well inside the update rate.
- **PaHUB + voltmeters.** The gauge starts every voltmeter converting at the same time, waits once, then reads each, so three voltmeters cost about the same as one. A voltmeter that clips on its fine range is read again on the coarse range, alone. A voltmeter that does not answer gives no reading, so only its input shows `NO SENSOR`; if the hub itself does not answer, the whole board is treated as lost.
- **PbHUB.** One read per input per sample. The hub's own converter does the work, so this is the fastest board.
- **Staleness.** If no fresh reading arrives within 1.5 s, the gauge shows `NO DATA`. It never keeps showing an old value. After several consecutive failed I2C transfers the board is treated as lost.

### 5.2 From volts to numbers

All conversions are in `oil_math.h`. Calibration trims are applied to the measured volts first.

**Oil pressure.** The sender is a 0.5–4.5 V device spanning 0–10 bar (all editable). The reading is `bar = (volts − 0.5) × 10 / 4.0`, converted to psi, then trimmed by an optional scale and offset.

**Boost.** A straight line through two (volts, kPa) points, set by the sensor preset or a custom curve.
- *Absolute (MAP) sensor:* boost = sensor kPa − atmospheric kPa. Vacuum shows as negative psi.
- *Gauge-pressure sensor:* the reading is used directly.

**AFR.** A straight line through two (volts, AFR) points, set by the controller preset or a custom curve.

### 5.3 Fault detection

A powered sensor never sits near 0 V, and never above its supply. Voltages outside the healthy window are reported as faults, not as a number.

| Display | Meaning |
|---|---|
| `NO SENSOR` | No sensor board answering on the bus (keeps looking). With a PaHUB, it can also mean there is no voltmeter on that one port. |
| `CHECK WIRING` | Signal outside the healthy window: unplugged or shorted to ground, or shorted high |
| `NO DATA` | Board present but no fresh reading for 1.5 s |
| `NEEDS INA3221` | Boost or AFR is switched on but only the single-input M5 Meter is in use. Choose another board for them. |
| `OFF` / not shown | That gauge is not fitted |

Default fault limits: oil below 0.30 V or more than 0.4 V above sender full scale (4.9 V by default); boost below 0.10 V or above 5.25 V; AFR below 0.15 V or above 5.25 V. The limits apply to the sender volts, after any input divider.

With a 10-bit 0–5 V PbHUB, a value above 1023 counts cannot exist, so if the hub really reads 12-bit while the page says 10-bit, the reading is shown as `CHECK WIRING` (high), never as a wrong pressure.

### 5.4 Alerts and colours

**Oil pressure** is the only gauge that alarms.

- **Arming.** The alarm arms the first time pressure reaches 15 psi, meaning the engine has run. Until then the dial stays neutral, so key-on with the engine off does not alarm. It stays armed until power is removed.
- **Warning.** Below 20 psi the dial is amber.
- **Critical.** Below 10 psi for 1.5 seconds: the ring flashes red, `LOW OIL PRESSURE` is shown and the buzzer beeps. If no screen is showing oil, the gauge jumps to an oil screen, or shows a banner if none of your screens includes oil. Tapping silences the buzzer and flash; the dial stays red while the pressure is low.

The gauge has no RPM or oil temperature input, so the oil limits are flat thresholds rather than curves.

**Boost** is colour only: amber from 18 psi, red from 22 psi (defaults; set your own safe limit).

**AFR** is colour only:
- Lean limits (amber above 13.4, red above 14.0) apply only **under load**, because lean is normal at cruise. With no throttle input, "under load" means manifold pressure is at or above a gate (default −2 psi, so it also works on a naturally aspirated engine). If the boost sensor is missing or faulty, the lean colours stay off rather than guess. Set the gate to −15 to apply the limits always.
- Below 11.0 the number turns blue. This is information only.

### 5.5 The display

- **Single screens** draw one dial: a 270° arc of 20 segments with a large number, units and a sensor label.
- **Split screens** draw two dials one above the other (160° arcs of 12 segments each) with a divider across the middle. The sensor label is hidden to save space.
- Dial ranges: oil 0–120 psi (or 0–8 bar), boost −15 to +30 psi (or −1 to +2 bar) with zero marked brighter, AFR 10–20 with stoichiometric (14.7) marked brighter.
- Small dots show which screen you are on.
- Objects are only redrawn when their value changes, which keeps the display smooth and the memory use stable.
- Wi-Fi requests and screen changes are applied at a safe point in the update cycle, never from inside a touch callback.

### 5.6 Settings storage

- Phone-editable settings are stored in the board's flash (non-volatile storage) and override the defaults in `oil_config.h`.
- Wi-Fi name and password are stored separately, so resetting one does not touch the other.
- **Defaults** on the page resets the settings but leaves Wi-Fi alone. The 5-second hold on the dial resets Wi-Fi only.
- Every value is validated before it is saved, including its range, channel clashes, and screens that use a gauge that is not fitted.
- Settings saved by an earlier firmware version are upgraded automatically and keep your calibration, thresholds, units, screens and sensor choices. The ADS1115 / hub settings added in v1.5.0 (address, PbHUB reading, input divider, trim) start at their defaults after an upgrade.

### 5.7 Wi-Fi and the web server

- The gauge is its own access point (WPA2) at **192.168.4.1**. The name is `OilGauge-XXXX`, where XXXX comes from the chip so each gauge is different. It needs no internet and no home network.
- Wi-Fi is **off at power-up**. Swipe up to switch it on and swipe down to switch it off. It switches itself off after 15 minutes without use of the page (adjustable; 0 means never). The timer starts when Wi-Fi is switched on. A phone merely joining does not reset it, but loading the page and keeping it open on screen does.
- The gauge refuses to start Wi-Fi if free internal memory is low (under about 45 KB) and says `LOW MEMORY` on the dial rather than risk the display.
- While Wi-Fi is on, the dial shows the network name and address, but never the password.
- Anyone who can join the hotspot can change settings and install firmware, because there is no second login. The password is the protection, so change the factory one on first use.

HTTP endpoints, for reference:

| Path | Method | Purpose |
|---|---|---|
| `/` | GET | The phone page |
| `/api/live` | GET | Current readings and status (polled by the page) |
| `/api/info` | GET | Version, board, free memory, whether the factory password is still in use |
| `/api/settings` | GET / POST | Read and save settings |
| `/api/presets` | GET | Boost and AFR preset lists |
| `/api/zero` | POST | Zero boost (learn atmospheric pressure) |
| `/api/demo` | POST | Start (`on=1`) or stop (`on=0`) the 2-minute demo that shows invented readings on the screen |
| `/api/defaults` | POST | Reset settings to defaults (not Wi-Fi) |
| `/api/wifi` | POST | Change the Wi-Fi name and password |
| `/update` | POST | Firmware upload |
| `/api/layout` | GET / POST | Designed screens: what is stored and which views it holds / upload a layout file (checked before it is kept) |
| `/api/layout/clear` | POST | Remove the stored layout |
| `/api/layout.json` | GET | Download the stored layout |

The firmware upload accepts only an application `.bin` image. It rejects files with `bootloader` or `merged` in the name, and anything that is not a valid app image.

### 5.8 Designed screens

Besides the built-in oil, boost, AFR and split screens, a screen slot can show a **designed view**: a layout you drew in the layout editor (a separate PC tool, `370z-gauge-editor`, whose preview is this firmware's own drawing code).

- A layout file holds up to 12 views of up to 16 gauges each (arcs, needles, bars, numbers, lights, text, lines), under 64 KB.
- The editor's **Download for gauge** button writes the file. On the phone page, **Designed screens** takes it. The gauge checks the whole file with the same code that draws it. If anything is wrong, it says why and **keeps the layout it had**. No reflash is needed.
- An accepted layout is kept in flash (in the spare data partition, in two alternating slots with a checksum, so a power cut during a save leaves the previous layout intact). It is loaded at every start-up.
- Each view then appears as `Designed: <name>` in the Screens list, mixed with the built-in screens in the four slots. Views that are switched off in the editor, or set for another screen number only, are not offered.
- The Big number gauge in designed screens keeps its digits still: the field is as wide as the biggest value on the scale and lines up on the decimal point, so 9 → 10 doesn't shift them (turn off with "Digits stay put").
- Readings can be drawn as glowing seven-segment digits, and arcs and bars can have a glow and a bright centre like a lit lamp. These cost drawing time, so use them on one or two gauges per view.
- A designed view can show five readings: oil pressure, the oil alarm state, boost, AFR and "engine running". With no reading it shows `--`, like the built-in dials. A designed screen that uses oil pressure takes part in the low-oil alarm the same way (the alarm banner appears on screens that do not show oil).
- If the stored layout later turns out to be unusable, or a screen points at a view that is no longer in it, that screen is skipped until a layout with the view is uploaded again. The page flags it.
- Warning lights have a **Value in** choice (psi, bar or kPa for oil/boost) so a light can be set in bar; needles are always drawn on top of lights.
- Not available on this gauge: clock and image gauges, and the full system's built-in view entries.

### 5.9 Run demo

The phone page has a **Demo** card with a *Run demo* button. It makes every screen, built-in or designed, show invented oil, boost and AFR readings, so you can see the gauges, alert colours and warning lights move without the car running (for example to check a layout you have just uploaded).

- The readings glide through idle, normal running, low oil pressure, high boost and a lean AFR, then repeat every 36 seconds. They stay inside each dial's range.
- It ends by itself after **2 minutes**, when you press *Stop demo*, or at once if the **real** oil pressure falls to the alarm level, so a real problem is never hidden behind it.
- A small **DEMO** tag is drawn on the screen with the seconds left. The buzzer stays silent during the demo.
- Nothing is saved. The real sensor readings, the oil-alarm "armed" state and your settings are untouched, and the highest values (peaks) are cleared when the demo starts and ends.
- The page's own *Live* card keeps showing the real sensor values.
- The request is `POST /api/demo` with `on=1` or `on=0`; the *Live* data includes how many demo seconds are left.

---

## 6. Using the gauge

### 6.1 Touch gestures

| Gesture | What it does |
|---|---|
| Swipe **left / right** | Next / previous screen |
| **Tap** | Silences a sounding alert. Otherwise switches psi ↔ bar (applies to oil and boost, and is saved). |
| Swipe **up** | Wi-Fi on |
| Swipe **down** | Wi-Fi off |
| **Hold 5 s**, finger still | Offers to reset the Wi-Fi name and password. Press **RESET** or **CANCEL**. It cancels itself after 10 s. |

Brightness is set on the phone page, not by swiping.

### 6.2 Everyday use

Power the gauge and it starts on your first screen. Oil stays neutral until the engine has run and pressure has reached 15 psi, then the dial colours and alarm become active. Swipe between the screens you have set up. With only one screen configured, swiping shows `ONLY ONE SCREEN SET`.

### 6.3 First-time setup

1. Flash the gauge and connect the sensor board ([sections 3 and 4](#3-building-and-flashing)).
2. Swipe up on the dial. It shows `WI-FI OilGauge-XXXX` and `192.168.4.1`.
3. On your phone join that network. The factory password is `JLbFc1h4` (set by `OG_WIFI_DEFAULT_PASS` in `oil_config.h`). The phone will say there is no internet, which is normal.
4. Open `http://192.168.4.1/` in the phone's browser.
5. Work through the page ([section 7](#7-the-phone-settings-page)): choose your board (leave it on Auto-detect for the M5 Meter or INA3221; pick the ADS1115, PaHUB or PbHUB by hand), choose each sensor's input, check the live readings, pick screens, set limits, and calibrate. *Run demo* shows the screens moving.
6. **Change the Wi-Fi password** (Wi-Fi card). The page reminds you until you do.
7. Press **Save**, then swipe down to turn Wi-Fi off.

### 6.4 If you forget the Wi-Fi password

The password is never shown on the gauge. If you change it and forget it, hold a finger still on the dial for 5 seconds and press **RESET**. The name and password go back to the factory ones (`OilGauge-XXXX` / `JLbFc1h4`). Calibration, thresholds and units are untouched.

### 6.5 Tips

- Use Wi-Fi when parked. Switch it off before driving, because it also shares memory resources with the display.
- If the gauge says `LOW MEMORY` when you try to enable Wi-Fi, see [section 9](#9-before-you-trust-it).
- The serial monitor (115200) prints one line every 2 seconds with the oil, boost and AFR readings in use, the volts, and which board is in use (for example `ADS1115  0x48  O1 B2 A3`, `PAHUB  0x70  O2 B1 A6` or `PBHUB  0x61  O1 B5`; the letters are oil, boost and AFR followed by each one's input).

---

## 7. The phone settings page

Open `http://192.168.4.1/` while joined to the gauge's Wi-Fi. Press **Save** to apply changes. Anything out of range is refused with a reason and nothing is applied.

| Card | What you can set |
|---|---|
| **Live** | Oil, boost and AFR readings and status, and which board is being read. Use it while checking wiring. |
| **Screens** | Up to 4 screen slots. Each is one of: Oil, Boost, AFR, Oil over Boost, Boost over Oil, Oil over AFR, AFR over Oil, Boost over AFR, AFR over Boost, or unused. A screen showing a gauge whose channel is "Not fitted" is refused. |
| **Designed screens** | Upload a layout file made in the layout editor, remove it, or download the one on the gauge. Uploaded views then appear in the Screens list. |
| **Inputs** | **Board**: Auto-detect, M5 voltmeter, INA3221, ADS1115, M5 PaHUB or M5 PbHUB. Under it, a note describes the chosen board. **Address** (ADS1115, PaHUB, PbHUB; "Usual" unless you changed it on the hardware). Which input each of oil, boost and AFR is on; the lists show only what the board has (INA3221 channels 1–3, ADS1115 inputs A0–A3, hub ports 1–6). Boost and AFR can be "Not fitted". Two sensors on one input are refused. **Input divider** (ADS1115 and PbHUB). **PbHUB reading** (10-bit 0–5 V, 12-bit 0–3.3 V, 12-bit 0–5 V). Changing the board resets the address to the usual one and keeps your input choices where the new board has them. |
| **Demo** | *Run demo* / *Stop demo*, with the seconds left. See section 5.9. |
| **Display & alerts** | Units (psi / bar), brightness slider, oil warning, critical and arm levels, buzzer on/off, Wi-Fi auto-off time |
| **Boost sensor** | Preset picker, sensor type (absolute MAP or gauge pressure), the two-point curve with a live "this volt = this kPa" preview, atmospheric pressure and **Zero boost now**, warning and critical psi |
| **AFR controller** | Preset picker, the two-point curve with a live preview, stoichiometric AFR, lean warning and critical, rich level, and the under-load gate |
| **Calibration** | Scale and offset for the M5 (also used for the PaHUB's voltmeters), the INA3221, the ADS1115 / PbHUB (each shared by all its inputs) and the oil pressure. Also the sender range (volts at 0 bar, volts at full scale, full-scale bar). The pair for the board in use is highlighted, and **MATCH** fills in the right one. |
| **Wi-Fi** | Change the hotspot name and password |
| **Firmware** | Upload a new `.bin` |

### 7.1 Sensor presets

Picking a preset fills in the curve. Editing any number afterwards changes the label to **Custom**.

| Boost sensor | |
|---|---|
| Bosch-style 3 bar MAP (default) | 11 kPa @ 0.25 V to 307 kPa @ 4.75 V, absolute |
| GM 3 bar MAP (12223861) | absolute |
| Generic 0.5–4.5 V absolute | 2, 3 or 4 bar versions |
| Generic 0.5–4.5 V gauge | 0–30 psi |

| AFR controller | |
|---|---|
| Linear 0–5 V (default) | 10 to 20 AFR |
| Innovate LC-1 / LC-2 / LM-2 default | 7.35 to 22.39 AFR |
| AEM X-Series gasoline | 8.5 to 18.0 AFR |

The generic presets are defined by their stated range, not by a part number. Every preset is only as correct as the manual it came from, so **check it against your own sensor or controller**. Some controllers can be configured to output a different range, so match the preset to your controller's actual setting.

### 7.2 Calibrating a sensor

1. Open the **Calibration** card and watch the live volts.
2. Put a multimeter on the sender or signal wire and type its reading in.
3. Press **MATCH**. The page works out the scale for the board in use from the live volts.
4. Press **Save**.

For oil pressure, compare with a mechanical gauge once if you have one, and use the pressure scale and offset to trim it.

### 7.3 Zero boost

An absolute (MAP) sensor reads atmospheric pressure with the engine off and the key on. Press **Zero boost now** in that state and the gauge stores that reading, so boost reads 0 psi at ambient pressure. The default is 101.3 kPa (sea level).

The gauge refuses the zero if the reading is not between 80 and 110 kPa. A wrong preset or a running engine therefore cannot set a nonsense zero. Gauge-pressure sensors read 0 at ambient already and do not need it.

### 7.4 Changing Wi-Fi name and password

Type the new name (1–32 characters) and password (8–63 characters) in the Wi-Fi card and press Save Wi-Fi. The gauge restarts its hotspot a few seconds later and your phone drops off. Rejoin with the new details. Leave the password blank to keep the current one.

---

## 8. Reference

### 8.1 Factory defaults

These suit the 370Z the gauge was built for. Change any of them on the phone page for a different car or sensor.

| Setting | Default |
|---|---|
| Board | Auto-detect (M5 preferred if both found) |
| Address (ADS1115 / PaHUB / PbHUB) | Usual: 0x48 / 0x70 / 0x61 |
| PbHUB reading | 10-bit, 0–5 V |
| Input divider | 1.0 (none) |
| ADS1115 / PbHUB scale / offset | 1.0 / 0.0 |
| Oil sender | 0.5 V = 0 bar, 4.5 V = 10 bar |
| Oil dial | 0–120 psi (0–8 bar) |
| Oil warning / critical / arm | 20 / 10 / 15 psi |
| Critical debounce | 1.5 s |
| Boost dial | −15 to +30 psi (−1 to +2 bar) |
| Boost warning / critical | 18 / 22 psi |
| Atmospheric pressure | 101.3 kPa |
| AFR dial | 10–20 |
| AFR lean warning / critical (under load) | 13.4 / 14.0 |
| AFR rich information | below 11.0 |
| Under-load gate | −2 psi |
| Brightness | 70 % |
| Units | psi |
| Buzzer | On |
| Boost / AFR input | Not fitted |
| Wi-Fi auto-off | 15 minutes |
| Hotspot | `OilGauge-XXXX`, factory password `JLbFc1h4` |
| Hold-to-reset time | 5 s |

### 8.2 Limits to know about

- The oil alarm cannot tell "engine off" from "pressure lost" until it has armed, because there is no RPM input.
- Boost and AFR do not alarm audibly, and a designed view's alert levels only change colours.
- Designed screens need the 16 MB flash and the `app3M_fat9M_16MB` partition scheme (the data partition is where the layout is kept). Without it the Designed screens card is switched off.
- The M5 Meter has one input, so boost and AFR need another board (INA3221, ADS1115, PaHUB or PbHUB).
- The INA3221 and the plain ADS1115 are not isolated, so they need a common ground with the car. The M5 voltmeters are isolated.
- A plain ADS1115 on 3.3 V cannot measure above 3.3 V: use an input divider for 0.5–4.5 V and 0–5 V signals.
- The ADS1115, PaHUB and PbHUB are chosen by hand, not auto-detected; only one board type is used at a time (you cannot mix, say, an INA3221 and a PbHUB).
- The PbHUB is not an I2C hub: M5 voltmeters cannot be plugged into it (use a PaHUB for that). Its analog input is coarser than the other boards, and an unusual reading format is guarded against but should still be checked once with a multimeter.
- The demo's readings are invented: it is for looking at screens, never for judging the car.
- Anyone with the Wi-Fi password can change settings and install firmware.

---

## 9. Before you trust it

Work through this once on the real car.

- [ ] **Calibration.** Set the board and pressure scale and offset on the page. The defaults are uncalibrated (1.0 / 0.0).
- [ ] **Sender range.** Confirm that the 0.5–4.5 V, 10 bar defaults match your sender.
- [ ] **Multimeter check.** Compare the page's live volts with a meter on the sender wire. Use **MATCH** if they differ, especially on the INA3221.
- [ ] **Pull the sensor lead.** Expect `NO SENSOR` or `CHECK WIRING`, never `0`.
- [ ] **Run the engine.** The gauge arms (serial prints `[oil] armed`). Compare the reading with a mechanical gauge if you have one.
- [ ] **Settings persist.** Swipe up, join, change a setting, power-cycle, and check that it stuck. Then try the 5-second hold and press **CANCEL**.
- [ ] **Look at every screen.** In particular check the split layouts on the curved edge for overlap or clipped text.
- [ ] **Wi-Fi stability.** Watch the picture for a few minutes with Wi-Fi on to confirm it stays steady.
- [ ] **Designed screens.** Upload a layout, pick it in a slot, and check it draws and updates like the editor preview. Pull the power while uploading once and check the previous layout is still there. Check the low-oil alarm still shows on a designed screen that does not use oil.
- [ ] **Boost and AFR.** Check the presets against your own sensor and controller. Use **Zero boost now** with the engine off.
- [ ] **ADS1115 breakout.** Confirm its supply and the divider: no input may ever exceed the supply plus 0.3 V. With the engine off, compare live volts with a multimeter on each input.
- [ ] **PaHUB + voltmeters.** Plug in one voltmeter at a time and see the matching input come alive on the Live card. Unplug one and check only that input reads `NO SENSOR`. Check the address switch matches the *Address* setting.
- [ ] **PbHUB.** Apply a known voltage (a 3.3 V pin is handy) and check the live volts agree with a multimeter. If they do not, try the other *PbHUB reading* choices before using *Calibration*.
- [ ] **Run demo.** Press it, watch every screen move, and press *Stop demo*. Check the DEMO tag appears and the buzzer stays silent.

**If Wi-Fi refuses to start and the dial says `LOW MEMORY`:** internal RAM is short (about 45 KB is needed, and the serial log prints the figure). In this gauge's copy of `lv_conf.h` set `LV_MEM_SIZE` to `(64U * 1024U)` and rebuild. Do not put that file in a library folder that other LVGL projects share.

---

## 10. Project layout and tests

```
OilGauge370Z/
├── OilGauge370Z.ino        setup + main loop
├── oil_config.h            factory defaults
├── oil_settings.h/.cpp     settings, validation, presets, flash storage
├── oil_api.h               phone-page API logic (pure)
├── oil_types.h             shared fault / reading types
├── oil_math.h              conversions and alert levels (pure)
├── oil_sensor.h/.cpp       M5, INA3221, ADS1115, PaHUB, PbHUB readers
├── oil_demo.h              the Run demo readings and request check (pure)
├── oil_ui.h/.cpp           dials, alerts, touch
├── oil_web.h/.cpp          Wi-Fi, web server, firmware update
├── oil_page.h              the phone page
├── oil_layout.h/.cpp       designed screens: upload check, flash store, load
├── oil_layout_store.h      flash slot format (pure)
├── Gauge_Layout.h/.cpp     layout engine (draws designed views)
├── Gauge_Inputs.h/.cpp     the five readings a designed view can use
├── ArduinoJson.h           bundled JSON parser (MIT)
├── lv_conf.h               LVGL configuration
├── board_pins.h            pins, addresses, panel timing
├── board_i2c, board_expander, board_panel, board_touch, board_lvgl
│                           board support (.h/.cpp each)
└── tools/                  PC tests
```

### 10.1 PC tests

The logic is tested on a PC, without hardware:

| Test | What it checks |
|---|---|
| `tools/test_oil_math.cpp` | Volts → psi, boost and AFR across the voltage range, every preset, fault limits, zero-boost acceptance, and alert levels including the under-load gate |
| `tools/test_oil_settings.cpp` | Every setting's limits, junk input refused, nothing half-applied, input clashes, input numbers and addresses for each board, screens using a gauge that is off, preset behaviour, and upgrading older saved settings (including the v1.4 → v1.5 layout) |
| `tools/test_layout_store.cpp` | The flash slot format: checksum, picking the newest valid slot, a power cut at every step of a save, damaged slots, wrap-around |
| `tools/test_oil_demo.cpp` | The demo: readings stay in each dial's range, glide smoothly, reach every alert level, repeat; the time limit; the start / stop request check |
| `tools/sim/` | The real sensor code against simulated chips: M5 and INA3221 (detection, hot-plug, range switching, wiring faults, several channels), a plain ADS1115 (inputs, divider, trim, address), a PaHUB with three voltmeters (parallel reads, a missing voltmeter, coarse range, hub lost and back, other address, touch chip untouched) and a PbHUB (ports, 10-bit and 12-bit, wrong setting caught) |
| `tools/webtest/` | The real phone page in headless Chromium against the gauge's real settings and live-data code (and a mock of the layout upload) (needs Python with Playwright, and g++) |

Run them from the project folder:

```
g++ -std=c++11 -Wall -I. tools/test_oil_math.cpp -o t_math && ./t_math
g++ -std=c++11 -Wall -I. tools/test_oil_settings.cpp -o t_settings && ./t_settings
g++ -std=c++11 -Wall -I. tools/test_layout_store.cpp -o t_store && ./t_store
g++ -std=c++17 -I. tools/test_oil_demo.cpp -o t_demo && ./t_demo
g++ -std=c++14 -Itools/sim -I. tools/sim/sim_main.cpp oil_sensor.cpp -o t_sim && ./t_sim
python3 tools/webtest/run_page_test.py
```

The display, touch, buzzer and Wi-Fi radio can only be checked on the real board. Use the checklist in [section 9](#9-before-you-trust-it).
