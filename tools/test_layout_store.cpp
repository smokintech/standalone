// Host test for oil_layout_store.h. g++ -std=c++11 -Wall -I. tools/test_layout_store.cpp
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include "oil_layout_store.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

// A tiny model of the two flash slots and the save/load procedure used by oil_layout.cpp
struct Flash {
  std::vector<uint8_t> s[2];
  Flash() { s[0].assign(OG_LAYOUT_SLOT_BYTES, 0xFF); s[1].assign(OG_LAYOUT_SLOT_BYTES, 0xFF); }
  void erase(int i) { s[i].assign(OG_LAYOUT_SLOT_BYTES, 0xFF); }
  void write_json(int i, const std::string& j) { memcpy(&s[i][OG_LAYOUT_HDR_BYTES], j.data(), j.size()); }
  void write_hdr(int i, uint32_t seq, const std::string& j) {
    OgSlotHeader h; og_slot_header_make(h, seq, (const uint8_t*)j.data(), j.size());
    memcpy(&s[i][0], &h, sizeof h);
  }
  // returns the JSON that boot would load, or "" and sets *slot / *seq
  std::string boot(int* slot, uint32_t* seq) const {
    bool v[2]; uint32_t q[2];
    for (int i = 0; i < 2; i++) {
      OgSlotHeader h; memcpy(&h, &s[i][0], sizeof h);
      v[i] = og_slot_valid(h, h.len <= OG_LAYOUT_MAX_JSON ? &s[i][OG_LAYOUT_HDR_BYTES] : nullptr);
      q[i] = h.seq;
    }
    int p = og_slot_pick(v[0], q[0], v[1], q[1]);
    *slot = p; *seq = p >= 0 ? q[p] : 0;
    if (p < 0) return "";
    OgSlotHeader h; memcpy(&h, &s[p][0], sizeof h);
    return std::string((const char*)&s[p][OG_LAYOUT_HDR_BYTES], h.len);
  }
  // one complete save, with an optional power cut after `cut_after` steps (0 erase, 1 json, 2 header)
  void save(const std::string& j, int cut_after = 99) {
    int cur; uint32_t seq; boot(&cur, &seq);
    const int t = cur < 0 ? 0 : og_slot_next(cur);
    const uint32_t ns = og_seq_next(cur >= 0, seq);
    erase(t);                         if (cut_after == 0) return;
    write_json(t, j);                 if (cut_after == 1) return;
    write_hdr(t, ns, j);
  }
};

int main() {
  // crc32 of "123456789" is the standard check value
  CHECK(og_crc32((const uint8_t*)"123456789", 9) == 0xCBF43926u);
  CHECK(sizeof(OgSlotHeader) == OG_LAYOUT_HDR_BYTES);

  Flash f; int slot; uint32_t seq;
  CHECK(f.boot(&slot, &seq).empty() && slot == -1);           // blank flash: no layout

  f.save("{\"a\":1}");
  CHECK(f.boot(&slot, &seq) == "{\"a\":1}" && slot == 0 && seq == 1);
  f.save("{\"b\":22}");
  CHECK(f.boot(&slot, &seq) == "{\"b\":22}" && slot == 1 && seq == 2);
  f.save("{\"c\":333}");
  CHECK(f.boot(&slot, &seq) == "{\"c\":333}" && slot == 0 && seq == 3);

  // power cut at every step of a save: the previous layout must survive
  for (int cut = 0; cut <= 1; cut++) {
    Flash g; g.save("{\"old\":true}");
    g.save("{\"new\":true}", cut);
    CHECK(g.boot(&slot, &seq) == "{\"old\":true}");
  }
  // header written but a byte of the JSON is damaged: rejected, old one wins
  { Flash g; g.save("{\"old\":true}"); g.save("{\"new\":true}");
    g.s[1][OG_LAYOUT_HDR_BYTES + 3] ^= 0x40;
    CHECK(g.boot(&slot, &seq) == "{\"old\":true}" && slot == 0); }
  // both slots damaged: nothing
  { Flash g; g.save("{\"x\":1}"); g.save("{\"y\":2}");
    g.s[0][OG_LAYOUT_HDR_BYTES] ^= 1; g.s[1][OG_LAYOUT_HDR_BYTES] ^= 1;
    CHECK(g.boot(&slot, &seq).empty()); }
  // absurd length in a header cannot read past the slot
  { Flash g; OgSlotHeader h; h.magic = OG_LAYOUT_MAGIC; h.seq = 5; h.len = 0xFFFFFFF0u; h.crc = 0;
    memcpy(&g.s[0][0], &h, sizeof h);
    CHECK(og_slot_valid(h, &g.s[0][OG_LAYOUT_HDR_BYTES]) == false);
    h.len = 0; CHECK(og_slot_valid(h, &g.s[0][OG_LAYOUT_HDR_BYTES]) == false);
    h.len = OG_LAYOUT_MAX_JSON + 1; CHECK(og_slot_valid(h, &g.s[0][OG_LAYOUT_HDR_BYTES]) == false); }
  // sequence wrap-around: 0xFFFFFFFF then 0 counts as newer
  CHECK(og_slot_pick(true, 0xFFFFFFFFu, true, 0u) == 1);
  CHECK(og_slot_pick(true, 0u, true, 0xFFFFFFFFu) == 0);
  CHECK(og_seq_next(false, 77) == 1 && og_seq_next(true, 41) == 42);

  printf(fails ? "\n%d of %d checks FAILED\n" : "ALL PASSED (%d checks)\n", fails ? fails : checks, checks);
  return fails ? 1 : 0;
}
