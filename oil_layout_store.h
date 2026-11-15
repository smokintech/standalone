#pragma once
// ---------------------------------------------------------------------------
// oil_layout_store.h - how an uploaded layout file is kept in flash (pure logic,
// tested on a PC; oil_layout.cpp does the actual flash reads and writes).
//
// The layout JSON is written raw into the data partition, in one of two
// fixed-size slots used alternately:
//
//     [ 16-byte header ][ JSON text ......................... unused (0xFF) ]
//
// The header is written LAST, after the JSON, so a power cut half-way through a
// save leaves the new slot without a valid header and the previous layout in the
// other slot untouched. At start-up the slot with a valid header and the highest
// sequence number wins.
// ---------------------------------------------------------------------------
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define OG_LAYOUT_SLOT_BYTES   65536u                      // one slot (16 flash sectors)
#define OG_LAYOUT_HDR_BYTES    16u
#define OG_LAYOUT_MAX_JSON     (OG_LAYOUT_SLOT_BYTES - OG_LAYOUT_HDR_BYTES)
#define OG_LAYOUT_MAGIC        0x314C474Fu                 // "OGL1" little-endian

struct OgSlotHeader { uint32_t magic, seq, len, crc; };

static inline uint32_t og_crc32(const uint8_t* d, size_t n, uint32_t crc = 0) {
  crc = ~crc;
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

static inline void og_slot_header_make(OgSlotHeader& h, uint32_t seq, const uint8_t* json, size_t len) {
  h.magic = OG_LAYOUT_MAGIC; h.seq = seq; h.len = (uint32_t)len; h.crc = og_crc32(json, len);
}

// Is this slot (header + the JSON that followed it) a complete, intact layout?
static inline bool og_slot_valid(const OgSlotHeader& h, const uint8_t* json /* h.len bytes, or null */) {
  if (h.magic != OG_LAYOUT_MAGIC) return false;
  if (h.len == 0 || h.len > OG_LAYOUT_MAX_JSON) return false;
  if (!json) return false;
  return og_crc32(json, h.len) == h.crc;
}

// Which of the two slots to use: the valid one with the newer sequence number
// (wrap-safe). -1 if neither is valid.
static inline int og_slot_pick(bool validA, uint32_t seqA, bool validB, uint32_t seqB) {
  if (validA && validB) return (int32_t)(seqA - seqB) >= 0 ? 0 : 1;
  if (validA) return 0;
  if (validB) return 1;
  return -1;
}

// Where a new save goes: the slot that is NOT the current one, with the next sequence number.
static inline int      og_slot_next(int current)               { return current == 0 ? 1 : 0; }
static inline uint32_t og_seq_next(bool have, uint32_t seq)    { return have ? seq + 1u : 1u; }
