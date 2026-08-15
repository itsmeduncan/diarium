// The runtime font format: pre-rasterised glyph bitmaps, one file, mapped
// straight into memory.
//
// Rasterising on device would mean shipping a TTF interpreter and paying for
// it on every page turn. Instead `tools/fontgen` bakes exactly the faces,
// sizes and codepoints in `faces.h` into a pack the device loads once into
// PSRAM. Coverage is a build-time decision, which is the right place for it.
//
// Layout (all little-endian):
//
//   header      "RFP1", u16 version, u16 face_count, u32 file_size
//   face[N]     fixed 52-byte records, in FaceId order
//   glyphs      per face: u16-sorted-by-codepoint records of 20 bytes
//   kern        per face: 8-byte records sorted by (glyph_a, glyph_b)
//   bitmaps     per face: 4-bit alpha, rows padded to whole bytes
//
// 4 bits of alpha is not a compromise: the panel renders 3-bit greyscale, so
// the extra bit is already more than the hardware can show.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/text/faces.h"

namespace rsspaper {

// Advances and kerning are stored in 1/16 px, not whole pixels.
//
// Rounding each advance to an integer accumulates up to half a pixel of error
// per character; over a 65-character measure that is enough drift to make
// justified text visibly uneven. Positions are kept in subpixel units through
// line breaking and rounded once, at blit time.
constexpr int kSubpixel = 16;
constexpr int to_px(int subpixel_units) {
  return (subpixel_units + kSubpixel / 2) / kSubpixel;
}

constexpr uint32_t kFontPackMagic = 0x31504652;  // "RFP1"
constexpr size_t kFontPackHeaderBytes = 12;
constexpr size_t kFontPackFaceBytes = 52;
constexpr size_t kFontPackGlyphBytes = 20;
constexpr size_t kFontPackKernBytes = 8;

struct Glyph {
  uint32_t codepoint = 0;
  int16_t bearing_x = 0;  // left side bearing, px
  int16_t bearing_y = 0;  // baseline to bitmap top, positive up
  uint16_t width = 0;
  uint16_t height = 0;
  int16_t advance = 0;  // 1/16 px
  uint32_t bitmap_offset = 0;  // into the face's bitmap blob
};

class FontPack;

// A single face at a single size. Cheap to copy; it's a view into the pack.
class Face {
 public:
  Face() = default;

  bool valid() const { return pack_ != nullptr; }
  const char* name() const { return name_; }
  int px_size() const { return px_size_; }
  int ascent() const { return ascent_; }
  int descent() const { return descent_; }
  int line_gap() const { return line_gap_; }
  int x_height() const { return x_height_; }
  int cap_height() const { return cap_height_; }
  // The face's natural line height. The type scale overrides this per role.
  int natural_leading() const { return ascent_ + descent_ + line_gap_; }

  // Null when the codepoint isn't in the pack.
  const Glyph* glyph(uint32_t cp) const;
  // Alpha 0-15 for a pixel of `g`'s bitmap. Out-of-range reads return 0.
  uint8_t alpha_at(const Glyph& g, int x, int y) const;
  // Kerning adjustment in 1/16 px between two codepoints, usually negative.
  int kern(uint32_t a, uint32_t b) const;

  // Advance width of a UTF-8 string in 1/16 px, kerning included. This is
  // the measurement the line breaker runs on.
  int measure(const std::string& utf8) const;
  // Advance of a single codepoint in 1/16 px, falling back to U+FFFD or a
  // space so a missing glyph never collapses a line.
  int advance_of(uint32_t cp) const;

 private:
  friend class FontPack;

  const FontPack* pack_ = nullptr;
  const char* name_ = "";
  const uint8_t* glyphs_ = nullptr;
  const uint8_t* kerns_ = nullptr;
  const uint8_t* bitmaps_ = nullptr;
  size_t bitmaps_len_ = 0;
  uint16_t glyph_count_ = 0;
  uint16_t kern_count_ = 0;
  int16_t px_size_ = 0;
  int16_t ascent_ = 0;
  int16_t descent_ = 0;
  int16_t line_gap_ = 0;
  int16_t x_height_ = 0;
  int16_t cap_height_ = 0;

  int glyph_index(uint32_t cp) const;
  Glyph glyph_at(int index) const;
};

class FontPack {
 public:
  // Takes ownership of the bytes; every Face points into them, so the pack
  // must outlive any Face handed out.
  bool load(std::vector<uint8_t> bytes, std::string* error);
  bool load_file(const std::string& path, std::string* error);

  bool loaded() const { return !data_.empty(); }
  size_t size_bytes() const { return data_.size(); }

  // Faces are addressed by FaceId; a pack missing one returns an invalid Face
  // rather than crashing, so a stale pack degrades instead of bricking.
  const Face& face(FaceId id) const;
  bool has_face(FaceId id) const { return faces_[index_of(id)].valid(); }

 private:
  static size_t index_of(FaceId id) {
    const size_t i = static_cast<size_t>(id);
    return i < kFaceCount ? i : 0;
  }

  std::vector<uint8_t> data_;
  Face faces_[kFaceCount];
  Face invalid_;
  char names_[kFaceCount][16] = {};
};

}  // namespace rsspaper
