#include "core/text/font_pack.h"

#include <cstring>

#include "core/base/str.h"
#include "core/io/file_byte_source.h"

namespace diarium {
namespace {

uint16_t rd_u16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
int16_t rd_i16(const uint8_t* p) {
  return static_cast<int16_t>(rd_u16(p));
}
uint32_t rd_u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

// --- Face -------------------------------------------------------------------

Glyph Face::glyph_at(int index) const {
  Glyph g;
  const uint8_t* p = glyphs_ + static_cast<size_t>(index) * kFontPackGlyphBytes;
  g.codepoint = rd_u32(p);
  g.bearing_x = rd_i16(p + 4);
  g.bearing_y = rd_i16(p + 6);
  g.width = rd_u16(p + 8);
  g.height = rd_u16(p + 10);
  g.advance = rd_i16(p + 12);
  g.bitmap_offset = rd_u32(p + 16);
  return g;
}

int Face::glyph_index(uint32_t cp) const {
  int lo = 0, hi = static_cast<int>(glyph_count_) - 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    const uint32_t at =
        rd_u32(glyphs_ + static_cast<size_t>(mid) * kFontPackGlyphBytes);
    if (at == cp) return mid;
    if (at < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}

int Face::resolve(uint32_t cp) const {
  const int direct = glyph_index(cp);
  if (direct >= 0) return direct;

  const uint32_t fallback = fallback_codepoint(cp);
  if (fallback == kDropGlyph) return -1;
  const int substituted = glyph_index(fallback);
  if (substituted >= 0) return substituted;
  // Even the fallback is absent — nothing sensible left to draw.
  return -1;
}

const Glyph* Face::glyph(uint32_t cp) const {
  if (!valid()) return nullptr;
  const int i = resolve(cp);
  if (i < 0) return nullptr;
  // Cached so callers can hold the pointer for the duration of one lookup,
  // which is all the line breaker needs.
  static thread_local Glyph scratch;
  scratch = glyph_at(i);
  return &scratch;
}

uint8_t Face::alpha_at(const Glyph& g, int x, int y) const {
  if (x < 0 || y < 0 || x >= g.width || y >= g.height) return 0;
  const size_t stride = (static_cast<size_t>(g.width) + 1) / 2;
  const size_t at = g.bitmap_offset + static_cast<size_t>(y) * stride +
                    static_cast<size_t>(x) / 2;
  if (at >= bitmaps_len_) return 0;
  const uint8_t byte = bitmaps_[at];
  return (x % 2 == 0) ? static_cast<uint8_t>(byte >> 4)
                      : static_cast<uint8_t>(byte & 0x0F);
}

int Face::kern(uint32_t a, uint32_t b) const {
  if (!valid() || kern_count_ == 0) return 0;
  const int ia = resolve(a);
  const int ib = resolve(b);
  if (ia < 0 || ib < 0) return 0;

  const uint32_t key = (static_cast<uint32_t>(ia) << 16) |
                       static_cast<uint32_t>(ib);
  int lo = 0, hi = static_cast<int>(kern_count_) - 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    const uint8_t* p = kerns_ + static_cast<size_t>(mid) * kFontPackKernBytes;
    const uint32_t at =
        (static_cast<uint32_t>(rd_u16(p)) << 16) | rd_u16(p + 2);
    if (at == key) return rd_i16(p + 4);
    if (at < key) lo = mid + 1;
    else hi = mid - 1;
  }
  return 0;
}

int Face::advance_of(uint32_t cp) const {
  if (!valid()) return 0;
  const int i = resolve(cp);
  // A dropped character costs nothing — that is what dropping means, and it
  // is why measurement has to use the same resolution the renderer does.
  return i >= 0 ? glyph_at(i).advance : 0;
}

int Face::measure(const std::string& utf8) const {
  if (!valid()) return 0;
  int total = 0;
  uint32_t prev = 0;
  size_t i = 0;
  while (i < utf8.size()) {
    const uint32_t cp = utf8_next(utf8, i);
    if (prev != 0) total += kern(prev, cp);
    total += advance_of(cp);
    prev = cp;
  }
  return total;
}

// --- FontPack ---------------------------------------------------------------

bool FontPack::load(std::vector<uint8_t> bytes, std::string* error) {
  auto fail = [&](const char* why) {
    if (error != nullptr) *error = why;
    data_.clear();
    for (Face& f : faces_) f = Face();
    return false;
  };

  data_ = std::move(bytes);
  for (Face& f : faces_) f = Face();

  if (data_.size() < kFontPackHeaderBytes) return fail("font pack is truncated");
  const uint8_t* base = data_.data();
  if (rd_u32(base) != kFontPackMagic) {
    return fail("not a font pack (bad magic) — rebuild with tools/fontgen");
  }
  const uint16_t version = rd_u16(base + 4);
  if (version != 1) return fail("font pack version is newer than this build");

  const uint16_t face_count = rd_u16(base + 6);
  const uint32_t declared = rd_u32(base + 8);
  if (declared != data_.size()) return fail("font pack size does not match its header");
  if (face_count == 0 || face_count > 64) return fail("font pack face count is implausible");
  if (kFontPackHeaderBytes + static_cast<size_t>(face_count) * kFontPackFaceBytes >
      data_.size()) {
    return fail("font pack face table runs past the end of the file");
  }

  for (uint16_t i = 0; i < face_count; ++i) {
    const uint8_t* p =
        base + kFontPackHeaderBytes + static_cast<size_t>(i) * kFontPackFaceBytes;

    char name[16];
    std::memcpy(name, p, 15);
    name[15] = '\0';
    const FaceId id = face_id_from_name(name);
    if (id == FaceId::Count) continue;  // a face this build doesn't know about

    Face f;
    f.px_size_ = static_cast<int16_t>(rd_u16(p + 16));
    f.ascent_ = rd_i16(p + 18);
    f.descent_ = rd_i16(p + 20);
    f.line_gap_ = rd_i16(p + 22);
    f.x_height_ = rd_i16(p + 24);
    f.cap_height_ = rd_i16(p + 26);
    f.glyph_count_ = rd_u16(p + 28);
    f.kern_count_ = rd_u16(p + 30);

    const uint32_t glyph_off = rd_u32(p + 32);
    const uint32_t kern_off = rd_u32(p + 36);
    const uint32_t bitmap_off = rd_u32(p + 40);
    const uint32_t bitmap_len = rd_u32(p + 44);

    // Validate every span before handing out pointers: a corrupt pack on an
    // SD card must degrade, not read out of bounds.
    const size_t glyph_bytes =
        static_cast<size_t>(f.glyph_count_) * kFontPackGlyphBytes;
    const size_t kern_bytes =
        static_cast<size_t>(f.kern_count_) * kFontPackKernBytes;
    if (glyph_off + glyph_bytes > data_.size()) return fail("glyph table out of range");
    if (kern_off + kern_bytes > data_.size()) return fail("kern table out of range");
    if (static_cast<size_t>(bitmap_off) + bitmap_len > data_.size()) {
      return fail("bitmap blob out of range");
    }

    const size_t slot = index_of(id);
    std::memcpy(names_[slot], name, 16);
    f.pack_ = this;
    f.name_ = names_[slot];
    f.glyphs_ = base + glyph_off;
    f.kerns_ = base + kern_off;
    f.bitmaps_ = base + bitmap_off;
    f.bitmaps_len_ = bitmap_len;
    faces_[slot] = f;
  }

  if (!faces_[static_cast<size_t>(FaceId::Body)].valid()) {
    return fail("font pack has no body face — rebuild with tools/fontgen");
  }
  return true;
}

bool FontPack::load_file(const std::string& path, std::string* error) {
  std::string raw;
  if (!read_file(path, &raw)) {
    if (error != nullptr) {
      *error = "cannot read font pack '" + path +
               "' — run `make fonts` to build it";
    }
    return false;
  }
  return load(std::vector<uint8_t>(raw.begin(), raw.end()), error);
}

const Face& FontPack::face(FaceId id) const {
  const Face& f = faces_[index_of(id)];
  // Falling back to Body keeps a missing display face from blanking a page.
  if (!f.valid() && id != FaceId::Body) return faces_[static_cast<size_t>(FaceId::Body)];
  return f;
}

}  // namespace diarium
