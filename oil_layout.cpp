#include "oil_layout.h"
#include "oil_layout_store.h"
#include "Gauge_Layout.h"
#include "oil_ui.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"

namespace {

const esp_partition_t* s_part = nullptr;
int      s_slot = -1;                   // slot holding the stored layout, -1 = none
uint32_t s_seq = 0;
size_t   s_stored = 0;
bool     s_loaded = false;
char     s_load_err[120] = "";

constexpr int kScreenId = 1;            // this gauge is "screen 1" in a layout's "screens" list

void* big_alloc(size_t n) {
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(n);
}

size_t slot_base(int i) { return (size_t)i * OG_LAYOUT_SLOT_BYTES; }

// Read one slot; on success *json holds h.len bytes (caller frees) and h is filled.
bool read_slot(int i, OgSlotHeader& h, uint8_t** json) {
  *json = nullptr;
  if (!s_part) return false;
  if (esp_partition_read(s_part, slot_base(i), &h, sizeof h) != ESP_OK) return false;
  if (h.magic != OG_LAYOUT_MAGIC || h.len == 0 || h.len > OG_LAYOUT_MAX_JSON) return false;
  uint8_t* buf = (uint8_t*)big_alloc(h.len + 1);
  if (!buf) return false;
  if (esp_partition_read(s_part, slot_base(i) + OG_LAYOUT_HDR_BYTES, buf, h.len) != ESP_OK ||
      !og_slot_valid(h, buf)) { free(buf); return false; }
  buf[h.len] = 0;
  *json = buf;
  return true;
}

bool write_slot(int i, uint32_t seq, const uint8_t* json, size_t len) {
  if (!s_part) return false;
  const size_t base = slot_base(i);
  if (esp_partition_erase_range(s_part, base, OG_LAYOUT_SLOT_BYTES) != ESP_OK) return false;
  // JSON first, in word-aligned pieces; the header goes last (see oil_layout_store.h)
  uint8_t chunk[1024];
  for (size_t off = 0; off < len; off += sizeof chunk) {
    size_t n = len - off < sizeof chunk ? len - off : sizeof chunk;
    memcpy(chunk, json + off, n);
    size_t padded = (n + 3) & ~(size_t)3;
    memset(chunk + n, 0xFF, padded - n);
    if (esp_partition_write(s_part, base + OG_LAYOUT_HDR_BYTES + off, chunk, padded) != ESP_OK) return false;
  }
  OgSlotHeader h; og_slot_header_make(h, seq, json, len);
  return esp_partition_write(s_part, base, &h, sizeof h) == ESP_OK;
}

}  // namespace

void OilLayout_Begin() {
  s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, nullptr);
  if (!s_part || s_part->size < 2u * OG_LAYOUT_SLOT_BYTES) {
    s_part = nullptr;
    Serial.println("[layout] no data partition to keep layouts in (use partition scheme app3M_fat9M_16MB)");
    return;
  }
  OgSlotHeader h[2]; uint8_t* j[2]; bool v[2];
  for (int i = 0; i < 2; i++) v[i] = read_slot(i, h[i], &j[i]);
  const int pick = og_slot_pick(v[0], h[0].seq, v[1], h[1].seq);
  if (pick < 0) { Serial.println("[layout] none stored"); }
  else {
    s_slot = pick; s_seq = h[pick].seq; s_stored = h[pick].len;
    char err[120] = "";
    if (GL_Load((const char*)j[pick], h[pick].len, err, sizeof err)) {
      s_loaded = true;
      Serial.printf("[layout] loaded: %d view(s), %u bytes (slot %d)\n", GL_ViewCount(), (unsigned)s_stored, pick);
    } else {
      strlcpy(s_load_err, err, sizeof s_load_err);
      Serial.printf("[layout] stored layout NOT usable: %s\n", err);
    }
  }
  for (int i = 0; i < 2; i++) free(j[i]);
}

bool   OilLayout_Available()        { return s_part != nullptr; }
bool   OilLayout_Loaded()           { return s_loaded; }
const char* OilLayout_LoadError()   { return s_load_err; }
size_t OilLayout_StoredBytes()      { return s_stored; }

bool OilLayout_Apply(const char* json, size_t len, char* err, size_t errlen, bool* saved) {
  if (saved) *saved = false;
  if (err && errlen) err[0] = 0;
  if (!s_part) { snprintf(err, errlen, "this firmware has no room to keep layouts (partition scheme)"); return false; }
  if (len == 0)                 { snprintf(err, errlen, "the file is empty"); return false; }
  if (len > OG_LAYOUT_MAX_JSON) { snprintf(err, errlen, "layout too large (%u bytes, limit %u)", (unsigned)len, (unsigned)OG_LAYOUT_MAX_JSON); return false; }

  OilUI_LayoutWillChange();                          // the engine swaps its views: the screen must not hold the old one
  if (!GL_Load(json, len, err, errlen)) {
    OilUI_SettingsChanged(true);                     // put the screen back as it was
    return false;
  }
  s_loaded = true; s_load_err[0] = 0;

  const int target = (s_slot < 0) ? 0 : og_slot_next(s_slot);
  const uint32_t seq = og_seq_next(s_slot >= 0, s_seq);
  const bool ok = write_slot(target, seq, (const uint8_t*)json, len);
  if (ok) { s_slot = target; s_seq = seq; s_stored = len; }
  else Serial.println("[layout] flash write FAILED - layout is active but will be lost at power-off");
  if (saved) *saved = ok;
  OilUI_SettingsChanged(true);
  return true;
}

bool OilLayout_Clear(char* err, size_t errlen) {
  if (err && errlen) err[0] = 0;
  if (!s_part) { snprintf(err, errlen, "no layout storage"); return false; }
  OilUI_LayoutWillChange();
  static const char kEmpty[] = "{\"format\":\"370z-gauge-layout\",\"version\":1,\"views\":[]}";
  { char e[64]; GL_Load(kEmpty, sizeof kEmpty - 1, e, sizeof e); }
  bool ok = true;
  for (int i = 0; i < 2; i++)                        // wipe both slots: no valid header anywhere
    ok &= (esp_partition_erase_range(s_part, slot_base(i), OG_LAYOUT_SLOT_BYTES) == ESP_OK);
  s_slot = -1; s_seq = 0; s_stored = 0; s_loaded = false; s_load_err[0] = 0;
  OilUI_SettingsChanged(true);
  if (!ok) snprintf(err, errlen, "could not erase the stored layout");
  return ok;
}

size_t OilLayout_ReadStored(uint8_t* dst, size_t cap) {
  if (s_slot < 0 || !s_stored || s_stored > cap) return 0;
  OgSlotHeader h; uint8_t* j;
  if (!read_slot(s_slot, h, &j)) return 0;
  memcpy(dst, j, h.len);
  free(j);
  return h.len;
}

bool OilLayout_ViewUsable(int v) {
  if (v < 0 || v >= GL_ViewCount()) return false;
  if (GL_ViewBuiltin(v)[0]) return false;            // the full system's built-in entries mean nothing here
  return GL_ViewOnScreen(v, kScreenId);
}
