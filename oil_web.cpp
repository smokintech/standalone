// ---------------------------------------------------------------------------
// oil_web.cpp - the gauge as its own Wi-Fi hotspot with a phone settings page.
//
// Runs on the SAME task as LVGL, the touch driver and the sensor reader
// (server.handleClient() is called from loop()), so nothing here needs locking
// and there is only ever one user of the I2C bus. The cost: a long request
// (a firmware upload) stalls the dial while it runs - which is why the page
// tells you to update only when parked.
//
// Security model: the hotspot is WPA2 with a factory password (OG_WIFI_DEFAULT_PASS,
// NOT shown on the dial) that the owner is prompted on the page to change. Anyone who can join it can change the settings; there is no second
// login. Wi-Fi is off at power-up and switches itself off when unused.
// ---------------------------------------------------------------------------
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include "oil_config.h"
#include "oil_settings.h"
#include "oil_math.h"
#include "oil_api.h"
#include "oil_demo.h"
#include "oil_sensor.h"
#include "oil_ui.h"
#include "oil_layout.h"
#include "Gauge_Layout.h"
#include "oil_web.h"
#include "oil_page.h"

static WebServer server(80);
static bool      g_on = false;
static char      g_ssid[33] = "";
static char      g_pass[64] = "";
static uint32_t  g_last_act = 0;
static bool      g_restart_ap = false;   static uint32_t g_restart_ap_at = 0;
static bool      g_reboot = false;       static uint32_t g_reboot_at = 0;

// firmware upload state
static bool      g_ota_active = false, g_ota_ok = false, g_ota_first = false;
static char      g_ota_err[96] = "";

// ---------------------------------------------------------------------------
// Wi-Fi name / password (own NVS namespace: nothing else can overwrite them)
// ---------------------------------------------------------------------------
static_assert(sizeof(OG_WIFI_DEFAULT_PASS) >= 9 && sizeof(OG_WIFI_DEFAULT_PASS) <= 64,
              "OG_WIFI_DEFAULT_PASS must be 8 to 63 characters");

static void makeDefaults() {
  uint64_t mac = ESP.getEfuseMac();
  snprintf(g_ssid, sizeof g_ssid, "OilGauge-%04X", (unsigned)((mac >> 32) & 0xFFFF));
  strlcpy(g_pass, OG_WIFI_DEFAULT_PASS, sizeof g_pass);
}

static bool wifiStore() {
  Preferences p;
  if (!p.begin("oilwifi", false)) return false;
  bool ok = p.putString("ssid", g_ssid) > 0 && p.putString("pass", g_pass) > 0;
  p.end();
  return ok;
}

static void wifiLoad() {
  Preferences p;
  bool have = false;
  if (p.begin("oilwifi", true)) {
    String s = p.getString("ssid", ""), w = p.getString("pass", "");
    p.end();
    char err[96];
    if (s.length() <= 32 && w.length() <= 63 && og_wifi_valid(s.c_str(), w.c_str(), true, err, sizeof err)) {
      strlcpy(g_ssid, s.c_str(), sizeof g_ssid); strlcpy(g_pass, w.c_str(), sizeof g_pass); have = true;
    }
  }
  if (!have) {
    makeDefaults();
    if (!wifiStore()) Serial.println("[wifi] WARNING: could not save the Wi-Fi name/password - a new one is made each boot");
    Serial.printf("[wifi] factory Wi-Fi details set: '%s'\n", g_ssid);
  }
}

const char* OilWeb_Ssid() { return g_ssid; }
bool        OilWeb_On()   { return g_on; }

void OilWeb_FactoryResetWifi() {
  Preferences p;
  if (p.begin("oilwifi", false)) { p.clear(); p.end(); }
  makeDefaults();
  if (!wifiStore()) Serial.println("[wifi] WARNING: could not save the reset Wi-Fi name/password");
  Serial.printf("[wifi] Wi-Fi settings reset: '%s'\n", g_ssid);
  if (g_on) { g_restart_ap = true; g_restart_ap_at = millis() + 300; }
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static void touch() { g_last_act = millis(); }

static void jsonEsc(const char* in, char* out, size_t n) {   // for text we do not fully control
  size_t o = 0;
  for (; *in && o + 2 < n; in++) {
    if (*in == '"' || *in == '\\') out[o++] = '\\';
    out[o++] = (*in >= 0x20) ? *in : '?';
  }
  out[o] = 0;
}
static void sendJson(int code, const char* body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}
static void sendErr(int code, const char* msg) {
  char e[160], b[200]; jsonEsc(msg, e, sizeof e);
  snprintf(b, sizeof b, "{\"ok\":false,\"error\":\"%s\"}", e);
  sendJson(code, b);
}
static const char* formArg(const char* name, void*) {
  static String hold;
  if (!server.hasArg(name)) return NULL;
  hold = server.arg(name);
  return hold.c_str();
}

// ---------------------------------------------------------------------------
// request handlers
// ---------------------------------------------------------------------------
static void hPage() {
  touch();
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", OIL_PAGE);
}

static void hLive() {                       // polled twice a second while the page is open AND visible
  touch();                                  // (a locked / backgrounded phone stops polling, so the timer then runs)
  ChanReading r[OG_NCH];
  for (int c = 0; c < OG_NCH; c++) r[c] = OilSensor_Get((OgChan)c);
  char b[720];
  if (og_live_json(b, sizeof b, r, OilSensor_Name(), (int)OilSensor_Kind(), OilUI_Armed(), OilUI_DemoLeft()) < 0) { sendErr(500, "live data did not fit"); return; }
  sendJson(200, b);
}

static void hInfo() {
  touch();
  char ss[80], b[260];
  jsonEsc(g_ssid, ss, sizeof ss);
  snprintf(b, sizeof b, "{\"version\":\"%s\",\"build\":\"%s %s\",\"ssid\":\"%s\",\"defaultPass\":%s,\"freeHeap\":%u}",
           OG_VERSION, __DATE__, __TIME__, ss,
           strcmp(g_pass, OG_WIFI_DEFAULT_PASS) == 0 ? "true" : "false",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  sendJson(200, b);
}

static void hSettingsGet() {
  touch();
  char b[1400];
  if (og_settings_json(g_set, b, sizeof b) < 0) { sendErr(500, "settings did not fit"); return; }
  sendJson(200, b);
}

static void hSettingsPost() {
  touch();
  char err[96];
  OgPostResult res;
  if (!og_settings_post(g_set, formArg, nullptr, err, sizeof err, res, OilLayout_ViewUsable)) { sendErr(400, err); return; }
  const bool saved = OilSettings_Save();
  OilUI_SettingsChanged(!res.only_backlight);
  if (res.sensor_changed) OilSensor_Reconfigure();
  Serial.printf("[web] settings applied%s%s%s\n", res.only_backlight ? " (brightness only)" : "",
                res.sensor_changed ? " (sensor restarted)" : "", saved ? "" : " - NOT SAVED");
  if (!saved) { sendErr(500, "Applied, but could not be saved to flash - it will be lost at power-off"); return; }
  sendJson(200, "{\"ok\":true}");
}

static void hPresets() {
  touch();
  char b[1400];
  if (og_presets_json(b, sizeof b) < 0) { sendErr(500, "presets did not fit"); return; }
  sendJson(200, b);
}

// "Zero boost now": see og_zero_boost() - refused unless the reading really looks like atmosphere.
static void hZeroBoost() {
  touch();
  char err[200]; float kpa = 0;
  if (!og_zero_boost(OilSensor_Get(OG_CH_BOOST), err, sizeof err, kpa)) { sendErr(400, err); return; }
  const float old = g_set.boost_atm_kpa;
  g_set.boost_atm_kpa = kpa;
  if (!OilSettings_Save()) {
    g_set.boost_atm_kpa = old;
    sendErr(500, "Could not save the new zero to flash - nothing changed"); return;
  }
  OilSensor_RestartSmoothing();
  char m[96]; snprintf(m, sizeof m, "{\"ok\":true,\"kpa\":%.1f}", (double)kpa);
  sendJson(200, m);
  Serial.printf("[web] boost zeroed: atmosphere = %.1f kPa\n", (double)kpa);
}

// "Run demo": the screen shows invented readings for every sensor for up to two minutes (see oil_demo.h).
static void hDemo() {
  touch();
  const int on = og_demo_parse(formArg("on", nullptr));
  if (on < 0) { sendErr(400, "on must be 1 or 0"); return; }
  OilUI_DemoSet(on == 1);
  char m[64]; snprintf(m, sizeof m, "{\"ok\":true,\"demo\":%d}", on == 1 ? (int)(OG_DEMO_MS / 1000) : 0);
  sendJson(200, m);
}

static void hDefaults() {
  touch();
  OilSettings_ResetToDefaults();
  OilUI_SettingsChanged();
  OilSensor_Reconfigure();
  Serial.println("[web] settings reset to defaults");
  sendJson(200, "{\"ok\":true}");
}

static void hWifiPost() {
  touch();
  String s = server.arg("ssid"), w = server.arg("pass");
  char err[96];
  const bool keep_pass = (w.length() == 0);
  if (!og_wifi_valid(s.c_str(), w.c_str(), false, err, sizeof err)) { sendErr(400, err); return; }
  char old_ssid[33], old_pass[64];
  strlcpy(old_ssid, g_ssid, sizeof old_ssid); strlcpy(old_pass, g_pass, sizeof old_pass);
  strlcpy(g_ssid, s.c_str(), sizeof g_ssid);
  if (!keep_pass) strlcpy(g_pass, w.c_str(), sizeof g_pass);
  if (!wifiStore()) {                       // could not save: keep the working details
    strlcpy(g_ssid, old_ssid, sizeof g_ssid); strlcpy(g_pass, old_pass, sizeof g_pass);
    sendErr(500, "Could not save the new Wi-Fi details to flash - nothing changed");
    return;
  }
  g_restart_ap = true; g_restart_ap_at = millis() + 2500;
  char e[80], b[260]; jsonEsc(g_ssid, e, sizeof e);
  snprintf(b, sizeof b, "{\"ok\":true,\"msg\":\"Saved. The gauge restarts its Wi-Fi in a few seconds - rejoin '%s' %s.\"}",
           e, keep_pass ? "with the same password" : "with the new password");
  sendJson(200, b);
  Serial.printf("[web] Wi-Fi details changed: '%s'\n", g_ssid);
}

// ---- designed screens (layout file made in the layout editor) ---------------------
// GET  /api/layout         what is stored, and the views in it
// POST /api/layout         upload a layout file (multipart, like the firmware): checked, then kept in flash
// POST /api/layout/clear   forget the layout
// GET  /api/layout.json    download the stored layout
static uint8_t* g_lay_buf = nullptr;          // upload buffer (PSRAM)
static size_t   g_lay_len = 0;
static char     g_lay_err[120] = "";
static bool     g_lay_ok = false;

static void layFree() { if (g_lay_buf) { free(g_lay_buf); g_lay_buf = nullptr; } g_lay_len = 0; }

static void hLayoutGet() {
  touch();
  static char b[4096];
  char err[200]; jsonEsc(OilLayout_LoadError(), err, sizeof err);
  int w = snprintf(b, sizeof b, "{\"available\":%s,\"loaded\":%s,\"bytes\":%u,\"maxBytes\":%u,\"error\":\"%s\",\"views\":[",
                   OilLayout_Available() ? "true" : "false", OilLayout_Loaded() ? "true" : "false",
                   (unsigned)OilLayout_StoredBytes(), (unsigned)OG_LAYOUT_MAX_JSON, err);
  bool first = true;
  const int n = OilLayout_Loaded() ? GL_ViewCount() : 0;
  for (int v = 0; v < n && w > 0 && (size_t)w < sizeof b - 160; v++) {
    if (GL_ViewBuiltin(v)[0]) continue;                       // the full system's built-in entries mean nothing here
    char nm[64]; jsonEsc(GL_ViewName(v), nm, sizeof nm);
    const bool on = OilLayout_ViewUsable(v);
    w += snprintf(b + w, sizeof b - w, "%s{\"i\":%d,\"code\":%d,\"name\":\"%s\",\"usable\":%s}", first ? "" : ",", v,
                  (int)og_page_custom_make(v), nm, on ? "true" : "false");
    first = false;
  }
  if (w > 0 && (size_t)w < sizeof b - 4) w += snprintf(b + w, sizeof b - w, "]}");
  if (w <= 0 || (size_t)w >= sizeof b - 2) { sendErr(500, "layout list did not fit"); return; }
  sendJson(200, b);
}

static void hLayoutUpload() {
  HTTPUpload& up = server.upload();
  touch();
  if (up.status == UPLOAD_FILE_START) {
    layFree(); g_lay_ok = true; g_lay_err[0] = 0;
    Serial.printf("[layout] receiving '%s'\n", up.filename.c_str());
    g_lay_buf = (uint8_t*)heap_caps_malloc(OG_LAYOUT_MAX_JSON + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_lay_buf) g_lay_buf = (uint8_t*)malloc(OG_LAYOUT_MAX_JSON + 1);
    if (!g_lay_buf) { g_lay_ok = false; strlcpy(g_lay_err, "not enough memory to receive the file", sizeof g_lay_err); }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!g_lay_ok) return;
    if (g_lay_len + up.currentSize > OG_LAYOUT_MAX_JSON) {
      g_lay_ok = false; snprintf(g_lay_err, sizeof g_lay_err, "file too large (limit %u bytes)", (unsigned)OG_LAYOUT_MAX_JSON); return; }
    memcpy(g_lay_buf + g_lay_len, up.buf, up.currentSize);
    g_lay_len += up.currentSize;
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    g_lay_ok = false; strlcpy(g_lay_err, "upload aborted", sizeof g_lay_err);
  }
}

static void hLayoutDone() {
  touch();
  char err[160] = "";
  bool ok = g_lay_ok, saved = false;
  if (!ok) strlcpy(err, g_lay_err[0] ? g_lay_err : "no file received", sizeof err);
  else ok = OilLayout_Apply((const char*)g_lay_buf, g_lay_len, err, sizeof err, &saved);
  layFree();
  if (!ok) { char m[220]; snprintf(m, sizeof m, "Layout rejected: %s", err); sendErr(400, m); Serial.printf("[layout] %s\n", m); return; }
  Serial.printf("[layout] accepted, %s\n", saved ? "saved" : "NOT SAVED");
  sendJson(200, saved ? "{\"ok\":true,\"saved\":true}" : "{\"ok\":true,\"saved\":false}");
}

static void hLayoutClear() {
  touch();
  char err[120];
  if (!OilLayout_Clear(err, sizeof err)) { sendErr(500, err); return; }
  sendJson(200, "{\"ok\":true}");
}

static void hLayoutDownload() {
  touch();
  const size_t n = OilLayout_StoredBytes();
  if (!n) { sendErr(404, "no layout stored"); return; }
  uint8_t* buf = (uint8_t*)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) buf = (uint8_t*)malloc(n + 1);
  if (!buf) { sendErr(500, "not enough memory"); return; }
  const size_t got = OilLayout_ReadStored(buf, n);
  if (got != n) { free(buf); sendErr(500, "could not read the stored layout"); return; }
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition", "attachment; filename=\"gauge-layouts.json\"");
  server.send_P(200, "application/json", (const char*)buf, n);   // send_P copies as it streams; buf stays valid until return
  free(buf);
}

// ---- firmware upload -------------------------------------------------------
static void otaFail(const char* why) {
  if (g_ota_ok) { strlcpy(g_ota_err, why, sizeof g_ota_err); Update.abort(); }
  g_ota_ok = false;
}

static void hUpdateUpload() {
  HTTPUpload& up = server.upload();
  touch();
  if (up.status == UPLOAD_FILE_START) {
    g_ota_active = true; g_ota_ok = true; g_ota_first = true; g_ota_err[0] = 0;
    String fn = up.filename; fn.toLowerCase();
    Serial.printf("[ota] receiving '%s'\n", up.filename.c_str());
    if (!fn.endsWith(".bin")) { g_ota_ok = false; strlcpy(g_ota_err, "that is not a .bin file", sizeof g_ota_err); return; }
    if (fn.indexOf("merged") >= 0 || fn.indexOf("bootloader") >= 0) {
      g_ota_ok = false; strlcpy(g_ota_err, "that is the full-flash image - use OilGauge370Z.ino.bin", sizeof g_ota_err); return; }
    OilUI_Busy("UPDATING FIRMWARE");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) otaFail(Update.errorString());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!g_ota_ok) return;
    if (g_ota_first) {
      g_ota_first = false;
      // An app image starts 0xE9, and its descriptor (magic 0xABCD5432) sits at byte 32.
      static const uint8_t magic[4] = { 0x32, 0x54, 0xCD, 0xAB };
      if (up.currentSize < 36 || up.buf[0] != 0xE9 || memcmp(up.buf + 32, magic, 4) != 0) {
        otaFail("this is not an application image for this chip"); return; }
    }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) otaFail(Update.errorString());
  } else if (up.status == UPLOAD_FILE_END) {
    if (g_ota_ok && !Update.end(true)) otaFail(Update.errorString());
    else if (g_ota_ok) Serial.printf("[ota] %u bytes written\n", (unsigned)up.totalSize);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    otaFail("upload aborted");
  }
}

static void hUpdateDone() {
  touch();
  g_ota_active = false;
  if (g_ota_ok) {
    sendJson(200, "{\"ok\":true}");
    g_reboot = true; g_reboot_at = millis() + 1200;
    Serial.println("[ota] success - restarting");
  } else {
    char m[120]; snprintf(m, sizeof m, "Update failed: %s", g_ota_err[0] ? g_ota_err : "unknown error");
    sendErr(500, m);
    Serial.printf("[ota] FAILED: %s\n", m);
    OilUI_Busy("");                          // clear the UPDATING text
  }
}

static void hNotFound() { server.send(404, "text/plain", "not found"); }

// ---------------------------------------------------------------------------
// on / off
// ---------------------------------------------------------------------------
static bool apStart() {
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  return WiFi.softAP(g_ssid, g_pass, 1, 0, 2);          // channel 1, visible, max 2 phones
}

bool OilWeb_Enable(bool on, const char** why) {
  static const char* none = "";
  if (why) *why = none;
  if (on == g_on) return true;
  if (on) {
    uint32_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    Serial.printf("[wifi] switching on - internal RAM free: %u bytes\n", (unsigned)freeInt);
    if (freeInt < OG_WIFI_MIN_HEAP) {
      if (why) *why = "LOW MEMORY";
      Serial.printf("[wifi] REFUSED: needs %u bytes free\n", (unsigned)OG_WIFI_MIN_HEAP);
      return false;
    }
    WiFi.mode(WIFI_AP);
    if (!apStart()) {
      WiFi.mode(WIFI_OFF);
      if (why) *why = "WI-FI FAILED";
      Serial.println("[wifi] softAP failed to start");
      return false;
    }
    server.begin();
    g_on = true; touch();
    Serial.printf("[wifi] ON  '%s'  http://%s/   RAM free now: %u   auto-off after %u min idle\n", g_ssid,
                  WiFi.softAPIP().toString().c_str(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)g_set.wifi_auto_off_min);
  } else {
    if (g_ota_active) { if (why) *why = "UPDATING"; return false; }
    server.stop();
    WiFi.mode(WIFI_OFF);                     // the core stops the AP and the radio in the right order
    g_on = false; g_restart_ap = false;
    Serial.printf("[wifi] OFF  RAM free now: %u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  }
  return true;
}

// ---------------------------------------------------------------------------
void OilWeb_Begin() {
  WiFi.persistent(false);                  // never write Wi-Fi state to flash on its own
  WiFi.mode(WIFI_OFF);
  wifiLoad();
  server.on("/",             HTTP_GET,  hPage);
  server.on("/api/live",     HTTP_GET,  hLive);
  server.on("/api/info",     HTTP_GET,  hInfo);
  server.on("/api/settings", HTTP_GET,  hSettingsGet);
  server.on("/api/settings", HTTP_POST, hSettingsPost);
  server.on("/api/presets",  HTTP_GET,  hPresets);
  server.on("/api/zero",     HTTP_POST, hZeroBoost);
  server.on("/api/demo",     HTTP_POST, hDemo);
  server.on("/api/defaults", HTTP_POST, hDefaults);
  server.on("/api/wifi",     HTTP_POST, hWifiPost);
  server.on("/update",       HTTP_POST, hUpdateDone, hUpdateUpload);
  server.on("/api/layout",      HTTP_GET,  hLayoutGet);
  server.on("/api/layout",      HTTP_POST, hLayoutDone, hLayoutUpload);
  server.on("/api/layout/clear",HTTP_POST, hLayoutClear);
  server.on("/api/layout.json", HTTP_GET,  hLayoutDownload);
  server.onNotFound(hNotFound);
}

void OilWeb_Poll() {
  uint32_t now = millis();
  if (g_reboot && (int32_t)(now - g_reboot_at) >= 0) { Serial.flush(); ESP.restart(); }
  if (!g_on) return;
  server.handleClient();
  now = millis();                            // handleClient() may have taken a while, and touch() has
                                             // stamped g_last_act with a time later than the 'now' above
  if (g_restart_ap && (int32_t)(now - g_restart_ap_at) >= 0) {
    g_restart_ap = false;
    esp_wifi_deauth_sta(0);                  // drop every phone, then apply the new name/password
    if (!apStart()) Serial.println("[wifi] softAP restart FAILED");
    else Serial.printf("[wifi] restarted as '%s'\n", g_ssid);
  }
  if (!g_ota_active && og_idle_expired(now, g_last_act, g_set.wifi_auto_off_min)) {
    Serial.printf("[wifi] auto-off: idle %u s, limit %u min\n",
                  (unsigned)((now - g_last_act) / 1000), (unsigned)g_set.wifi_auto_off_min);
    OilWeb_Enable(false, nullptr);
    OilUI_Toast("WI-FI OFF");
  }
}
