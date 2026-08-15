#include "tools/fontgen/gpos_kerning.h"

#include <cstring>

namespace rsspaper {
namespace fontgen {
namespace {

// OpenType is big-endian throughout.
uint16_t u16(const uint8_t* p) {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
int16_t i16(const uint8_t* p) {
  return static_cast<int16_t>(u16(p));
}
uint32_t u32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// Bytes a ValueRecord occupies under `format`: one int16 per set flag.
int value_record_size(uint16_t format) {
  int n = 0;
  for (int bit = 0; bit < 8; ++bit) {
    if ((format & (1u << bit)) != 0) ++n;
  }
  return n * 2;
}

// Byte offset of X_ADVANCE within a ValueRecord, or -1 when absent.
// X_PLACEMENT (0x0001) and Y_PLACEMENT (0x0002) precede it.
int x_advance_offset(uint16_t format) {
  if ((format & 0x0004) == 0) return -1;
  int off = 0;
  if ((format & 0x0001) != 0) off += 2;
  if ((format & 0x0002) != 0) off += 2;
  return off;
}

constexpr uint32_t kTagGpos = 0x47504F53;  // 'GPOS'
constexpr uint32_t kTagKern = 0x6B65726E;  // 'kern'

uint32_t find_table(const uint8_t* d, size_t len, uint32_t tag) {
  if (len < 12) return 0;
  const uint16_t num_tables = u16(d + 4);
  for (uint16_t i = 0; i < num_tables; ++i) {
    const size_t rec = 12 + static_cast<size_t>(i) * 16;
    if (rec + 16 > len) break;
    if (u32(d + rec) == tag) return u32(d + rec + 8);
  }
  return 0;
}

}  // namespace

bool GposKerning::init(const uint8_t* data, size_t length) {
  d_ = data;
  len_ = length;
  subtables_.clear();
  if (d_ == nullptr) return false;

  const uint32_t gpos = find_table(d_, len_, kTagGpos);
  if (gpos == 0 || gpos + 10 > len_) return false;

  const uint32_t feature_list = gpos + u16(d_ + gpos + 6);
  const uint32_t lookup_list = gpos + u16(d_ + gpos + 8);
  if (feature_list + 2 > len_ || lookup_list + 2 > len_) return false;

  // Collect the lookups belonging to the 'kern' feature. Script and language
  // are deliberately ignored: for a Latin text face every script shares the
  // same kern lookups, and walking the full ScriptList to prove it would be a
  // lot of code for no different answer.
  std::vector<uint16_t> kern_lookups;
  const uint16_t feature_count = u16(d_ + feature_list);
  for (uint16_t i = 0; i < feature_count; ++i) {
    const size_t rec = feature_list + 2 + static_cast<size_t>(i) * 6;
    if (rec + 6 > len_) break;
    if (u32(d_ + rec) != kTagKern) continue;

    const uint32_t feature = feature_list + u16(d_ + rec + 4);
    if (feature + 4 > len_) continue;
    const uint16_t index_count = u16(d_ + feature + 2);
    for (uint16_t k = 0; k < index_count; ++k) {
      const size_t at = feature + 4 + static_cast<size_t>(k) * 2;
      if (at + 2 > len_) break;
      kern_lookups.push_back(u16(d_ + at));
    }
  }
  if (kern_lookups.empty()) return false;

  const uint16_t lookup_count = u16(d_ + lookup_list);
  for (const uint16_t index : kern_lookups) {
    if (index >= lookup_count) continue;
    const size_t off_at = lookup_list + 2 + static_cast<size_t>(index) * 2;
    if (off_at + 2 > len_) continue;
    const uint32_t lookup = lookup_list + u16(d_ + off_at);
    if (lookup + 6 > len_) continue;

    const uint16_t lookup_type = u16(d_ + lookup);
    const uint16_t subtable_count = u16(d_ + lookup + 4);

    for (uint16_t s = 0; s < subtable_count; ++s) {
      const size_t sub_at = lookup + 6 + static_cast<size_t>(s) * 2;
      if (sub_at + 2 > len_) break;
      uint32_t subtable = lookup + u16(d_ + sub_at);
      uint16_t type = lookup_type;

      // Type 9 is Extension Positioning: a two-field indirection to the real
      // subtable, and precisely what stb_truetype declines to follow.
      if (type == 9) {
        if (subtable + 8 > len_) continue;
        const uint16_t ext_format = u16(d_ + subtable);
        if (ext_format != 1) continue;
        type = u16(d_ + subtable + 2);
        subtable = subtable + u32(d_ + subtable + 4);
      }
      if (type != 2) continue;  // not pair positioning
      if (subtable + 2 > len_) continue;

      const uint16_t format = u16(d_ + subtable);
      if (format != 1 && format != 2) continue;
      subtables_.push_back(Subtable{subtable, format});
    }
  }
  return !subtables_.empty();
}

int GposKerning::coverage_index(uint32_t coverage_offset, uint16_t glyph) const {
  if (coverage_offset + 4 > len_) return -1;
  const uint16_t format = u16(d_ + coverage_offset);

  if (format == 1) {
    const uint16_t count = u16(d_ + coverage_offset + 2);
    int lo = 0, hi = static_cast<int>(count) - 1;
    while (lo <= hi) {
      const int mid = lo + (hi - lo) / 2;
      const size_t at = coverage_offset + 4 + static_cast<size_t>(mid) * 2;
      if (at + 2 > len_) return -1;
      const uint16_t g = u16(d_ + at);
      if (g == glyph) return mid;
      if (g < glyph) lo = mid + 1;
      else hi = mid - 1;
    }
    return -1;
  }

  if (format == 2) {
    const uint16_t range_count = u16(d_ + coverage_offset + 2);
    int lo = 0, hi = static_cast<int>(range_count) - 1;
    while (lo <= hi) {
      const int mid = lo + (hi - lo) / 2;
      const size_t at = coverage_offset + 4 + static_cast<size_t>(mid) * 6;
      if (at + 6 > len_) return -1;
      const uint16_t start = u16(d_ + at);
      const uint16_t end = u16(d_ + at + 2);
      if (glyph < start) {
        hi = mid - 1;
      } else if (glyph > end) {
        lo = mid + 1;
      } else {
        return u16(d_ + at + 4) + (glyph - start);
      }
    }
    return -1;
  }
  return -1;
}

uint16_t GposKerning::class_of(uint32_t classdef_offset, uint16_t glyph) const {
  if (classdef_offset == 0 || classdef_offset + 4 > len_) return 0;
  const uint16_t format = u16(d_ + classdef_offset);

  if (format == 1) {
    const uint16_t start = u16(d_ + classdef_offset + 2);
    const uint16_t count = u16(d_ + classdef_offset + 4);
    if (glyph < start || glyph >= start + count) return 0;
    const size_t at =
        classdef_offset + 6 + static_cast<size_t>(glyph - start) * 2;
    if (at + 2 > len_) return 0;
    return u16(d_ + at);
  }

  if (format == 2) {
    const uint16_t range_count = u16(d_ + classdef_offset + 2);
    int lo = 0, hi = static_cast<int>(range_count) - 1;
    while (lo <= hi) {
      const int mid = lo + (hi - lo) / 2;
      const size_t at = classdef_offset + 4 + static_cast<size_t>(mid) * 6;
      if (at + 6 > len_) return 0;
      const uint16_t start = u16(d_ + at);
      const uint16_t end = u16(d_ + at + 2);
      if (glyph < start) {
        hi = mid - 1;
      } else if (glyph > end) {
        lo = mid + 1;
      } else {
        return u16(d_ + at + 4);
      }
    }
    return 0;
  }
  return 0;
}

int GposKerning::lookup_format1(const Subtable& st, uint16_t left,
                                uint16_t right) const {
  const uint32_t base = st.offset;
  if (base + 10 > len_) return 0;

  const uint32_t coverage = base + u16(d_ + base + 2);
  const uint16_t value_format1 = u16(d_ + base + 4);
  const uint16_t value_format2 = u16(d_ + base + 6);
  const uint16_t pair_set_count = u16(d_ + base + 8);

  const int index = coverage_index(coverage, left);
  if (index < 0 || index >= static_cast<int>(pair_set_count)) return 0;

  const size_t set_at = base + 10 + static_cast<size_t>(index) * 2;
  if (set_at + 2 > len_) return 0;
  const uint32_t pair_set = base + u16(d_ + set_at);
  if (pair_set + 2 > len_) return 0;

  const int adv_off = x_advance_offset(value_format1);
  const int rec_size =
      2 + value_record_size(value_format1) + value_record_size(value_format2);
  const uint16_t pair_count = u16(d_ + pair_set);

  // PairValueRecords are ordered by secondGlyph.
  int lo = 0, hi = static_cast<int>(pair_count) - 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    const size_t at = pair_set + 2 + static_cast<size_t>(mid) * rec_size;
    if (at + rec_size > len_) return 0;
    const uint16_t second = u16(d_ + at);
    if (second == right) {
      if (adv_off < 0) return 0;
      return i16(d_ + at + 2 + adv_off);
    }
    if (second < right) lo = mid + 1;
    else hi = mid - 1;
  }
  return 0;
}

int GposKerning::lookup_format2(const Subtable& st, uint16_t left,
                                uint16_t right) const {
  const uint32_t base = st.offset;
  if (base + 16 > len_) return 0;

  const uint32_t coverage = base + u16(d_ + base + 2);
  const uint16_t value_format1 = u16(d_ + base + 4);
  const uint16_t value_format2 = u16(d_ + base + 6);
  const uint32_t classdef1 = base + u16(d_ + base + 8);
  const uint32_t classdef2 = base + u16(d_ + base + 10);
  const uint16_t class1_count = u16(d_ + base + 12);
  const uint16_t class2_count = u16(d_ + base + 14);

  // Format 2 still only applies to glyphs the coverage table names, even
  // though the adjustment is looked up by class.
  if (coverage_index(coverage, left) < 0) return 0;

  const uint16_t c1 = class_of(classdef1, left);
  const uint16_t c2 = class_of(classdef2, right);
  if (c1 >= class1_count || c2 >= class2_count) return 0;

  const int adv_off = x_advance_offset(value_format1);
  if (adv_off < 0) return 0;

  const size_t rec_size =
      value_record_size(value_format1) + value_record_size(value_format2);
  if (rec_size == 0) return 0;

  const size_t at = base + 16 +
                    (static_cast<size_t>(c1) * class2_count + c2) * rec_size;
  if (at + rec_size > len_) return 0;
  return i16(d_ + at + adv_off);
}

int GposKerning::lookup(uint16_t left, uint16_t right) const {
  int total = 0;
  for (const Subtable& st : subtables_) {
    total += st.format == 1 ? lookup_format1(st, left, right)
                            : lookup_format2(st, left, right);
  }
  return total;
}

}  // namespace fontgen
}  // namespace rsspaper
