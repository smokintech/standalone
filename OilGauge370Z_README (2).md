# OilGauge370Z

A standalone oil pressure gauge with optional boost and air/fuel ratio (AFR) screens. It runs on one Waveshare ESP32-S3 round touch screen and reads its sensors directly. It needs no Raspberry Pi and no other brain unit.

It was built for a Nissan 370Z, which is where the project name and the factory defaults come from. Nothing in it is specific to that car. It reads ordinary analogue sensors (a 0.5–4.5 V oil pressure sender, a MAP sensor, a wideband controller's analogue output), so it works on any car with sensors like those. Where your sensors differ, set the sender range, curves and limits on the phone page.

You set it up from your phone. The gauge makes its own Wi-Fi hotspot, and a settings page on it lets you pick screens, sensors, limits and calibration. Firmware updates also go over that Wi-Fi. You can also design your own screens in a PC layout editor and upload them from the same page (section 5.8).

**Version:** v1.3.0

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
| **Sensor board** | M5 Meter ISOLATED (ADS1115, one input) **or** Adafruit INA3221 (three inputs). The gauge detects whichever is connected. |
| **Gauges** | Oil pressure (always). Boost and AFR (INA3221 only). |
| **Screens** | Up to 4, each a single gauge or a split with two gauges one above the other. |
| **Alerts** | Low oil pressure: red ring flash, banner and buzzer. Boost and AFR change colour only. |
| **Setup** | Phone, over the gauge's own Wi-Fi at `http://192.168.4.1/`. No home network or app. |
| **Updates** | Over Wi-Fi from the same page, after the first USB flash. |
| **Toolchain** | Arduino IDE, ESP32 core 3.0.x, LVGL 8.3.10 |

---

## 2. Design overview

### 2.1 Architecture

```
  Oil sender ─┐                         ┌───────────────────────────────┐
  MAP sensor ─┼─► M5 Meter / INA3221 ──►│  ESP32-S3                     │
  Wideband ───┘      (I2C, GPIO15/7)    │                               │
                                        │  sensor reader ► conversions  │
                                        │        │            │         │
                                        │        ▼            ▼         │
                                        │   LVGL dials    alert logic   │──► round display
                                        │        ▲            │         │──► buzzer
                                        │   touch input       │         │
                                        │                                │
                                        │  Wi-Fi hotspot + web server    │◄──► phone
                                        │  settings in flash (NVS)       │
                                        └───────────────────────────────┘
```

Everything runs in one cooperative loop in a single task. That loop services the display and touch, then the sensor reader, then the web server. Because only that loop talks to the I2C bus, the touch chip, IO expander and sensor board never contend for it.

### 2.2 Design principles

- **It never shows a made-up number.** If the sensor is missing, unplugged, shorted or silent, the dial says so in words (`NO SENSOR`, `CHECK WIRING`, `NO DATA`). It does not show `0`.
- **Each gauge fails on its own.** An unplugged AFR lead does not affect the oil or boost readings.
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
| `oil_sensor.*` | Non-blocking readers for the M5 (ADS1115) and INA3221, with auto-detection |
| `oil_math.h` | Volts → psi / boost / AFR, and the colour and alert levels |
| `oil_ui.*` | Single and split dials, page dots, alert banner, touch gestures |
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

The gauge uses the screen's own I2C bus: **SDA = GPIO15, SCL = GPIO7**. The touch chip, IO expander, IMU and RTC share that bus. None of them use the sensor addresses (0x40–0x43, 0x49), so there is no clash.

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

If both boards are present, the M5 is used by default. You can force the choice on the page (Inputs → Board). Boost and AFR need the INA3221, so select it if you want them.

### 4.5 Resolution

The INA3221 reads in 8 mV steps:

| Sensor | Approximate step |
|---|---|
| Oil sender, 0.5–4.5 V = 0–10 bar | 0.3 psi |
| 3 bar MAP sensor | 0.08 psi |
| 0–5 V AFR, spanning 10 AFR | 0.02 AFR |

That is fine for gauges. Median filtering and smoothing are applied on top. The M5 (ADS1115) is finer, which is why it is preferred for oil when both are available.

---

## 5. How it works

### 5.1 Sensor reading

A sensor reader polls the board in small non-blocking steps, so the display and touch never wait on I2C.

- **Auto-detection.** At start-up it looks for an M5 at `0x49` and an INA3221 at `0x40`–`0x43`. If nothing is found it keeps looking every 2 seconds, so a board connected late is picked up. Boards can be unplugged and re-plugged while running.
- **Sampling.** Each cycle takes 9 readings per channel, takes the **median** (to reject spikes), then applies an exponential smoothing filter (factor 0.4). A new reading is produced about every 250 ms.
- **M5 range switching.** The ADS1115 starts on its fine ±0.256 V range and switches to ±4.096 V if the signal clips.
- **INA3221 channels.** Only the channels you assign are read. With one channel the chip averages 16 samples. With several it averages 4, to keep the update rate up.
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
| `NO SENSOR` | No sensor board answering on the bus (keeps looking) |
| `CHECK WIRING` | Signal outside the healthy window: unplugged or shorted to ground, or shorted high |
| `NO DATA` | Board present but no fresh reading for 1.5 s |
| `NEEDS INA3221` | Boost or AFR is switched on but only the M5 is in use |
| `OFF` / not shown | That gauge is not fitted |

Default fault limits: oil below 0.30 V or more than 0.4 V above sender full scale (4.9 V by default); boost below 0.10 V or above 5.25 V; AFR below 0.15 V or above 5.25 V.

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
- Settings saved by an earlier firmware version are upgraded automatically and keep your calibration, thresholds and units.

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
- A designed view can show five readings: oil pressure, the oil alarm state, boost, AFR and "engine running". With no reading it shows `--`, like the built-in dials. A designed screen that uses oil pressure takes part in the low-oil alarm the same way (the alarm banner appears on screens that do not show oil).
- If the stored layout later turns out to be unusable, or a screen points at a view that is no longer in it, that screen is skipped until a layout with the view is uploaded again. The page flags it.
- Not available on this gauge: clock and image gauges, and the full system's built-in view entries.

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
5. Work through the page ([section 7](#7-the-phone-settings-page)): choose your board and channels, check the live readings, pick screens, set limits, and calibrate.
6. **Change the Wi-Fi password** (Wi-Fi card). The page reminds you until you do.
7. Press **Save**, then swipe down to turn Wi-Fi off.

### 6.4 If you forget the Wi-Fi password

The password is never shown on the gauge. If you change it and forget it, hold a finger still on the dial for 5 seconds and press **RESET**. The name and password go back to the factory ones (`OilGauge-XXXX` / `JLbFc1h4`). Calibration, thresholds and units are untouched.

### 6.5 Tips

- Use Wi-Fi when parked. Switch it off before driving, because it also shares memory resources with the display.
- If the gauge says `LOW MEMORY` when you try to enable Wi-Fi, see [section 9](#9-before-you-trust-it).
- The serial monitor (115200) prints one line every 2 seconds with the oil, boost and AFR readings in use, the volts, and which board is in use.

---

## 7. The phone settings page

Open `http://192.168.4.1/` while joined to the gauge's Wi-Fi. Press **Save** to apply changes. Anything out of range is refused with a reason and nothing is applied.

| Card | What you can set |
|---|---|
| **Live** | Oil, boost and AFR readings and status, and which board is being read. Use it while checking wiring. |
| **Screens** | Up to 4 screen slots. Each is one of: Oil, Boost, AFR, Oil over Boost, Boost over Oil, Oil over AFR, AFR over Oil, Boost over AFR, AFR over Boost, or unused. A screen showing a gauge whose channel is "Not fitted" is refused. |
| **Designed screens** | Upload a layout file made in the layout editor, remove it, or download the one on the gauge. Uploaded views then appear in the Screens list. |
| **Inputs** | Board (Auto / M5 / INA3221) and which INA3221 channel each of oil, boost and AFR is on. Boost and AFR can be "Not fitted". Two sensors on one channel are refused. |
| **Display & alerts** | Units (psi / bar), brightness slider, oil warning, critical and arm levels, buzzer on/off, Wi-Fi auto-off time |
| **Boost sensor** | Preset picker, sensor type (absolute MAP or gauge pressure), the two-point curve with a live "this volt = this kPa" preview, atmospheric pressure and **Zero boost now**, warning and critical psi |
| **AFR controller** | Preset picker, the two-point curve with a live preview, stoichiometric AFR, lean warning and critical, rich level, and the under-load gate |
| **Calibration** | Scale and offset for the M5, the INA3221 (shared by all its channels) and the oil pressure. Also the sender range (volts at 0 bar, volts at full scale, full-scale bar). |
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
| Board | Auto (M5 preferred if both found) |
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
| Boost / AFR channel | Not fitted |
| Wi-Fi auto-off | 15 minutes |
| Hotspot | `OilGauge-XXXX`, factory password `JLbFc1h4` |
| Hold-to-reset time | 5 s |

### 8.2 Limits to know about

- The oil alarm cannot tell "engine off" from "pressure lost" until it has armed, because there is no RPM input.
- Boost and AFR do not alarm audibly, and a designed view's alert levels only change colours.
- Designed screens need the 16 MB flash and the `app3M_fat9M_16MB` partition scheme (the data partition is where the layout is kept). Without it the Designed screens card is switched off.
- The M5 Meter has one input, so boost and AFR need the INA3221.
- The INA3221 is not isolated, so it needs a common ground with the car.
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
├── oil_sensor.h/.cpp       M5 + INA3221 readers
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
| `tools/test_oil_settings.cpp` | Every setting's limits, junk input refused, nothing half-applied, channel clashes, screens using a gauge that is off, preset behaviour, and upgrading older saved settings |
| `tools/test_layout_store.cpp` | The flash slot format: checksum, picking the newest valid slot, a power cut at every step of a save, damaged slots, wrap-around |
| `tools/sim/` | The real sensor code against simulated M5 and INA3221 chips: detection, hot-plug, range switching, wiring faults, and several channels at once |
| `tools/webtest/` | The real phone page in headless Chromium against the gauge's real settings and live-data code (and a mock of the layout upload) (needs Python with Playwright, and g++) |

Run them from the project folder:

```
g++ -std=c++11 -Wall -I. tools/test_oil_math.cpp -o t_math && ./t_math
g++ -std=c++11 -Wall -I. tools/test_oil_settings.cpp -o t_settings && ./t_settings
g++ -std=c++11 -Wall -I. tools/test_layout_store.cpp -o t_store && ./t_store
g++ -std=c++14 -Itools/sim -I. tools/sim/sim_main.cpp oil_sensor.cpp -o t_sim && ./t_sim
python3 tools/webtest/run_page_test.py
```

The display, touch, buzzer and Wi-Fi radio can only be checked on the real board. Use the checklist in [section 9](#9-before-you-trust-it).
