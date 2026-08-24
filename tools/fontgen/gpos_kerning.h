// Reading kerning pairs out of an OpenType GPOS table.
//
// stb_truetype has GPOS support, but `stbtt_GetGlyphKernAdvance` bails on any
// lookup that isn't type 2 (`stb_truetype.h`: `if (lookupType != 2) continue`).
// Modern fonts, Literata among them, wrap their kerning in type 9 Extension
// Positioning lookups, so stb finds nothing and every pair measures zero. That
// is not a cosmetic loss: unkerned display type is the most obvious tell of
// amateur typesetting, and this project's whole claim is the typography.
//
// This reader follows Extension lookups to the PairPos subtable underneath and
// handles both pair formats — format 1 (explicit pairs) and format 2 (class
// based, which is how most of a Latin font's kerning is actually stored).
//
// Build-time only. The device never parses a font; it reads the baked pack.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace diarium {
namespace fontgen {

class GposKerning {
 public:
  // Parses the font's GPOS table, collecting the pair-positioning subtables
  // reachable from the 'kern' feature. Returns false when the font has no
  // usable kerning, which is not an error.
  bool init(const uint8_t* data, size_t length);

  bool available() const { return !subtables_.empty(); }
  size_t subtable_count() const { return subtables_.size(); }

  // Kerning adjustment for a glyph pair, in font design units. Zero when the
  // pair is not kerned.
  int lookup(uint16_t left, uint16_t right) const;

 private:
  // Absolute offset of a PairPos subtable, plus its format.
  struct Subtable {
    uint32_t offset;
    uint16_t format;
  };

  int lookup_format1(const Subtable& st, uint16_t left, uint16_t right) const;
  int lookup_format2(const Subtable& st, uint16_t left, uint16_t right) const;

  // Coverage index of `glyph`, or -1 if the table doesn't cover it.
  int coverage_index(uint32_t coverage_offset, uint16_t glyph) const;
  uint16_t class_of(uint32_t classdef_offset, uint16_t glyph) const;

  const uint8_t* d_ = nullptr;
  size_t len_ = 0;
  std::vector<Subtable> subtables_;
};

}  // namespace fontgen
}  // namespace diarium
