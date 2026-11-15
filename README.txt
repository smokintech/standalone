OIL GAUGE 370Z  v1.5.0  -  standalone oil pressure gauge, with boost and AFR screens
==================================================================================
WHAT'S NEW IN v1.5.0
  * More input boards, chosen on the phone page (Inputs -> Board). Auto-detect and the M5 Meter / INA3221 setup are unchanged.
      - ADS1115 breakout: four inputs (A0-A3), address 0x48-0x4B, with an input-divider setting for 0.5-4.5 V senders.
      - M5 PaHUB (PCA9548A) with an M5 voltmeter on each port: several voltmeters, each on its own port.
      - M5 PbHUB (I/O Hub 1 to 6): its six ports as analog inputs (it is a GPIO hub, not an I2C hub, so voltmeters cannot be
        plugged into it). Check its readings against a multimeter - see OilGauge370Z_README.md section 4.5.
    Boost and AFR now work on any of these boards. Your saved settings are kept when you update.

WHAT'S NEW IN v1.3.5
  * Designed screens: each alert level (low red/amber, high amber/red) can have its own colour - its zone on the scale
    and the fill/reading while in it. E.g. a blue vacuum zone on a boost arc.
  * Designed screens: the Big number gauge keeps its digits still (width set by the biggest value on the scale, lined up on
    the decimal point) so 9 -> 10 doesn't shift them. Removed the gear format and the "brain verdict" alert option.
  * Designed arcs: blocks now have exactly even gaps (they were 1 or 2 degrees wide, unevenly).
  * Designed arcs can mark the scale in steps counted from zero (e.g. -10 -5 0 5 ... 20) so 0 is always shown.
  * Designed screens can use glowing seven-segment digits and a lit-lamp look on arcs and bars
    (glow, bright centre). Everything else from v1.3.0 below.
  * DESIGNED SCREENS: draw your own screens in the PC layout editor (370z-gauge-editor),
    press "Download for gauge", then upload the file on the phone page (card "Designed
    screens"). No reflash. The gauge checks the file, keeps it in flash, and the views
    appear as "Designed: <name>" in the Screens list next to the built-in screens.
  * The engine that draws them is bundled (Gauge_Layout.*, Gauge_Inputs.*, ArduinoJson.h).
    This version needs the 16 MB / app3M_fat9M_16MB partition scheme (as before) - the
    layout is kept in its data partition.

WHAT CAME IN v1.2.0
  * Boost (MAP sensor) and AFR (wideband controller) on INA3221 channels 2 and 3.
  * Swipe LEFT / RIGHT changes screen. You choose up to 4 screens on the phone page:
    a single gauge (Oil, Boost or AFR) or a split screen with two gauges one above
    the other (e.g. Oil over Boost, Boost over AFR, AFR over Oil ...).
  * Boost and AFR sensor PRESETS (picker on the page) or a custom two-point curve.
  * Brightness is now a slider on the phone page (it used to be swipe left/right).
  * Zero boost button (engine off) to learn the day's atmospheric pressure.


WIRING
------
I2C: the screen's own bus  -  SDA = GPIO15, SCL = GPIO7 (shared with the touch
chip, IO expander, IMU and RTC; none of them use 0x40-0x43 or 0x49).
Use the board's I2C connector / pads (check the SDA / SCL labels on your board)
and a 3.3 V supply for the sensor board.

 M5 Meter ISOLATED
   SDA/SCL/GND to the screen as above. Power it as you had it on the Pi
   (I have not verified the M5's supply voltage - use whatever worked before).
   Sender signal on the M5's measuring input exactly as before. The M5 is
   electrically isolated, so no common-ground concern.

 Adafruit INA3221
   SDA/SCL/GND/VS to the screen (VS = 3.3 V).
   Sender SIGNAL wire  -> channel 1 "Vin-"   (the bus-voltage input is the Vin-
                                              pin, measured against GND)
   Channel 1 "Vin+"    -> tie to Vin- (no shunt used)
   Sender GND          -> INA3221 GND, and this must be the same ground as the
                          screen / car (the INA3221 is NOT isolated)
   Use channel 2 or 3 instead? Choose it on the phone page (Inputs).
   BOOST sensor -> another channel's Vin- (suggested channel 2), AFR controller's
   analogue output -> another (suggested channel 3). Same rules as the oil sender:
   Vin+ tied to Vin-, and a COMMON GROUND with the INA3221 / screen / car. Give each
   sensor its own channel - the page refuses two on the same one. The M5 Meter has
   only one input, so boost and AFR need the INA3221 (set Board = INA3221 on the
   page if both boards are plugged in).
   A MAP sensor needs its own 5 V feed (check your sensor's pinout); the
   controller's analogue output is usually 0-5 V (use the preset / curve that
   matches how YOUR controller is set up).
   Address: A0 open/GND = 0x40, A0 to VS = 0x41, SDA = 0x42, SCL = 0x43. The
   gauge scans all four.
   Inputs tolerate up to 26 V.
   RESOLUTION: the INA3221 reads in 8 mV steps. On a 0.5-4.5 V / 10 bar sender
   that is 0.3 psi per step; on the 3 bar MAP curve about 0.08 psi; on a 0-5 V
   AFR curve spanning 10 AFR about 0.02 AFR. Fine for gauges (median + smoothing
   are applied), but coarser than the M5/ADS1115. With more than one channel in
   use the chip's own averaging drops from 16 to 4 samples to keep the update rate;
   the median and smoothing are still applied. If both boards are connected the
   M5 wins (OG_SENSOR_PREFER).
   The sender's own 5 V feed is unchanged from your current wiring.


BUILD  (Arduino IDE, esp32 core 3.0.x - built and compiled with 3.0.7)
-----
1. Libraries: ONLY lvgl 8.3.10 (not 9.x). In the Arduino IDE: Sketch > Include
   Library > Manage Libraries, search "lvgl", pick version 8.3.10, Install.
   ArduinoJson is bundled in this folder (ArduinoJson.h) - nothing to install.
   Wi-Fi, WebServer and Update come with the esp32 core.
2. Copy  lv_conf.h  from this folder over  libraries/lvgl/src/lv_conf.h
   (fonts 20/48 enabled, 128 KB LVGL heap). If your gauge screens already build
   on this PC you already have the same file - nothing to change.
3. Open OilGauge370Z.ino. Tools menu - same as the screens:
      Board            ESP32S3 Dev Module
      Flash Size       16MB      Partition Scheme  16MB Flash (3MB APP / 9.9MB FATFS)
      PSRAM            OPI PSRAM Flash Mode        QIO 80MHz  (NOT OPI)
      USB CDC On Boot  Enabled   USB Mode          Hardware CDC and JTAG
4. oil_config.h holds the FACTORY DEFAULTS. You can leave it alone and set
   everything from the phone page instead.
5. Upload over USB the first time. If it won't start: hold BOOT, tap RESET,
   release BOOT, upload again. After that, updates can go over Wi-Fi (see below).
   (The 16MB partition scheme in step 3 is what makes Wi-Fi updates possible.)

The board support code (the board_*.h/.cpp files) is original to this project; it
needs nothing from Waveshare's demo package. It drives the panel at a 12 MHz pixel
clock with a single frame buffer and a 20-line bounce buffer, and LVGL draws into
small internal-RAM buffers. Nothing else to patch.


USING IT
--------
  swipe LEFT / RIGHT   next / previous screen (dots show where you are)
  tap                  silences a sounding alert; otherwise switches psi <-> bar (saved;
                       applies to oil and boost)
  swipe UP             Wi-Fi ON   (name and address appear on the dial; the password is never shown)
  swipe DOWN           Wi-Fi OFF
  hold 5 s, still      prompt: "RESET WI-FI SETTINGS?"  - RESET or CANCEL
                       (times out and cancels itself after 10 s)
Brightness is set on the phone page (slider; applies when you let go).

SCREENS. Pick them on the phone page (Screens card): up to 4 slots, each one of
  Oil | Boost | AFR | Oil over Boost | Boost over Oil | Oil over AFR | AFR over Oil
  | Boost over AFR | AFR over Boost, or unused. A split screen shows two gauges with
  a divider across the middle, as on the full 370Z gauge screens. The page refuses a
  screen that shows a gauge whose channel is "Not fitted". With only one screen set,
  swiping says "ONLY ONE SCREEN SET".

Dials, colours and limits match the gauge screens (Gauge_Config.h):
  OIL    0-120 psi (8 bar). Amber below 20 psi, red below 10 psi.
  BOOST  -15 to +30 psi (-1 to +2 bar), zero marked brighter. Amber from 18 psi, red
         from 22 psi (set YOUR safe limit). Number shows vacuum as negative.
  AFR    10-20, stoichiometric (14.7) marked brighter. Under load: amber above 13.4,
         red above 14.0. Below 11.0 the number turns blue (very rich, information
         only). All adjustable on the page.
Boost and AFR have no buzzer or ring flash - colour only. Only low oil pressure alarms.

What the gauge shows instead of a number - it never shows a made-up 0:
  NO SENSOR      nothing answering at 0x49 / 0x40-0x43 (keeps looking every 2 s)
  CHECK WIRING   sender voltage under 0.30 V (unplugged / shorted to ground)
                 or more than 0.4 V above full scale, 4.9 V by default
                 (shorted to its 5 V feed)
  NO DATA        board present but no fresh reading for 1.5 s
  NEEDS INA3221  boost / AFR is switched on but the M5 Meter is the board in use
Each gauge fails on its own: an unplugged AFR lead does not affect oil or boost.
Serial monitor (115200) prints one line every 2 s:  oil, boost, AFR (the ones in use),
volts, and which board. On every screen change it prints the LVGL heap in use - if
that number creeps up as you swipe round and round, tell me.


THE PHONE PAGE (Wi-Fi)
----------------------
1. Swipe UP on the dial. The gauge becomes a hotspot and shows, on the dial:
        WI-FI  OilGauge-XXXX
        192.168.4.1
   The password is deliberately NOT shown on the dial, so nobody who sees the
   gauge (a garage, a valet, a passenger) can read it off the screen.
   The name ends in four letters/digits from the chip (different on each gauge).
   The factory password is  JLbFc1h4  (set as OG_WIFI_DEFAULT_PASS in
   oil_config.h). The page reminds you to change it until you do.
2. On your phone join that Wi-Fi (it has no internet - that is normal), then open
   http://192.168.4.1/  in the phone's browser.
3. The page has:
     Live          oil, boost and AFR readings and status, and which board is read
     Screens       the 4 screen slots (single or split gauges)
     Inputs        board (auto / M5 / INA3221) and which INA3221 channel each of
                   oil, boost and AFR is on (boost / AFR can be "Not fitted")
     Display & alerts   units, brightness slider, oil warning / critical / arm
                   levels (psi), buzzer on/off, Wi-Fi auto-off
     Boost sensor  PRESET picker, sensor type (absolute MAP / gauge pressure), the
                   two-point curve, a live "this volt = this kPa" preview, the
                   atmospheric pressure + "Zero boost now", warn / critical psi
     AFR controller  PRESET picker, two-point curve with live preview, stoichiometric
                   AFR, lean warn / critical, rich level, and the "under load" gate
     Calibration   scale + offset for the M5, for the INA3221 (shared by all its
                   channels) and for the oil pressure; sender range (volts at 0 bar, volts at full scale,
                   full-scale bar). Put a multimeter on the sender wire, type
                   its reading in, press MATCH: it works out the active board's
                   scale from the live volts. Then press Save.
     Wi-Fi         change the hotspot name and password
     Firmware      update over Wi-Fi
   SENSOR PRESETS. Boost: Bosch-style 3 bar MAP (the Pi's volt_publisher.py default,
   and this gauge's starting point), GM 3 bar MAP (12223861), generic 0.5-4.5 V
   absolute 2 / 3 / 4 bar, and generic 0.5-4.5 V gauge 0-30 psi. AFR: linear 0-5 V =
   10-20 (the Pi's default), Innovate LC-1 / LC-2 / LM-2 default (7.35-22.39), AEM
   X-Series gasoline (8.5-18.0). Picking a preset fills the curve; editing any
   number turns the label into Custom. The generic ones are defined by their stated
   range, not by a part number, and every preset is only as right as the manual it
   came from - check against YOUR sensor or controller. NOTE: the Pi's code comment
   calls 10-20 the "AEM 30-0300" mapping, but AEM's published X-Series output is
   8.5-18.0 - if you have an AEM, use that preset or check its output setting.
   ZERO BOOST. An absolute (MAP) sensor reads atmospheric pressure with the engine
   off, key on. "Zero boost now" stores that reading so boost reads 0 psi at
   ambient (the default is 101.3 kPa, sea level). It is refused if the reading is not
   80-110 kPa, so a wrong preset or a running engine cannot set a nonsense zero.
   Gauge-pressure sensors read 0 at ambient already.
   AFR LEAN LIMITS: lean is normal at cruise, so the lean colours only apply "under
   load". The full app uses throttle > 55%; this gauge has no throttle, so it uses the
   boost sensor: the limits apply while manifold pressure is at or above the gate
   (default -2 psi, about 87 kPa absolute - works on a naturally aspirated engine
   too). If the boost sensor is missing or faulty the lean colours stay OFF (it will
   not guess). Set the gate to -15 to apply them always.
4. Press Save. A value that is out of range is REFUSED with a reason (nothing is
   half-applied). If the gauge cannot write to its flash it says so - it never
   says "saved" when it is not.
5. Swipe DOWN to turn Wi-Fi off. It also switches itself off after 15 minutes
   without use of the page (set on the page; 0 = never). The timer starts when
   you switch Wi-Fi on - a phone merely joining the network does not reset it, but
   loading the page does, and so does keeping the page open on screen (it stops
   counting when the phone screen locks or the browser goes to the background).
   There is no short timeout: you have the full 15 minutes to join and open the page.

Changing the Wi-Fi name / password: type the new ones on the page and press Save
Wi-Fi. The gauge restarts its hotspot a few seconds later and your phone drops
off - rejoin with the new details. Leave the password blank to keep the old one.
New name 1-32 characters, password 8-63. Only the name is shown on the dial. If
you forget a password you set yourself, use the 5-second-hold reset below to put
it back to the factory one.

RESET THE WI-FI ONLY: hold a finger still on the dial for 5 seconds, then press
RESET. The hotspot name and password go back to the factory ones (OilGauge-XXXX /
JLbFc1h4). Calibration, thresholds, units - everything else - is untouched. If
Wi-Fi is on, it restarts with the new details. "Defaults" on the page does the
opposite: resets the settings but leaves the Wi-Fi alone.

FIRMWARE UPDATE OVER WI-FI: in the Arduino IDE use Sketch > Export Compiled
Binary, then upload  OilGauge370Z.ino.bin  on the page (NOT the file with
"bootloader" or "merged" in the name - the gauge refuses those, and anything that
is not an application image). The gauge restarts by itself. It stops reading
pressure while it installs: only do this when parked. If an update fails the old
firmware keeps running. Your saved settings and Wi-Fi details survive an update.

Safety notes
  * Anyone who can join the hotspot (they need the password) can
    change settings and install firmware. There is no second login.
  * Wi-Fi is OFF at power-up, always.
  * If the gauge refuses to switch Wi-Fi on and shows LOW MEMORY, internal RAM is
    short (needs about 45 KB free; the serial log prints the figure). Fix: in
    lv_conf.h set LV_MEM_SIZE to (64U * 1024U) for this gauge only - do not put
    that file in the library used by gauge screens - and rebuild.


THE LOW-PRESSURE ALERT
-------------------------------------------------------------
  * It ARMS the first time pressure reaches 15 psi (the engine has run). Until
    then the dial stays green/neutral - key on, engine off does not alarm.
  * Once armed, below 10 psi for 1.5 s = CRITICAL: ring flashes red, LOW OIL
    PRESSURE shown, buzzer beeps. A tap silences the buzzer/flash (the dial stays
    red). Below 20 psi = amber.
  * Armed stays armed until power is removed.
  * Defaults are the flat 20 / 10 psi the ESP32 screens use (adjustable on the page) - hot idle at low
    RPM is judged against the same line as hard driving.
  The buzzer can be switched off on the page.


CHECK BEFORE YOU TRUST IT
-------------------------
  [ ] Calibration - set on the page (or as defaults in oil_config.h):
        M5 scale / offset      from ~/.m5_vmeter_cal.json on the Pi 4
        pressure scale / offset from ~/.oil_sender_cal.json on the Pi 4
      Both are 1.0 / 0.0 (uncalibrated) 
  [ ] Sender range (0.5-4.5 V, 10 bar).
  [ ] With a multimeter on the sender wire, compare against the page's live volts
      and use MATCH if they differ (the INA3221 especially).
  [ ] Pull the sensor lead: expect NO SENSOR / CHECK WIRING, never "0".
  [ ] Run the engine: it arms (serial prints "[oil] armed"), then check the
      pressure against a mechanical gauge once.
  [ ] Swipe up, join the Wi-Fi, open the page, change something, power-cycle and
      check it stuck. Then try the 5-second hold and CANCEL.


FILES
-----
OilGauge370Z.ino    setup + loop
oil_config.h        factory defaults
oil_settings.*      the phone-editable settings: validation, presets, screens, saving to flash
oil_api.h           pure logic behind the phone page API (live JSON, zero boost, applying a form)
oil_types.h         shared fault / reading types
oil_web.*           Wi-Fi hotspot, web server, firmware update, Wi-Fi name/password
oil_page.h          the phone page (one self-contained HTML document)
oil_sensor.*        M5 (ADS1115) and INA3221 readers (3 channels), non-blocking, auto-detect
oil_math.h          volts -> psi / boost / AFR arithmetic and alert levels (no hardware)
oil_ui.*            screens (single / split dials), alert, touch
board_pins.h        every pin, address and panel timing number for the Waveshare board
board_i2c.*         the shared I2C bus
board_expander.*    the IO expander (panel / touch resets, panel chip select, buzzer)
board_panel.*       the ST7701 round panel (set-up commands, RGB output) and backlight
board_touch.*       the CST820 touch controller
board_lvgl.*        connects LVGL to the panel and touch
lv_conf.h           LVGL config for the library (see step 2)
tools/              PC tests


