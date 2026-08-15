// The type palette, shared by the font packer and the runtime so the two can
// never disagree about what's in a pack.
//
// Sizes are pixels on a 1024x758 panel at roughly 212 PPI, so 27 px body text
// is about 9.2 pt physical — a normal book size at reading distance, not the
// oversized text that "looks fine on a monitor" and reads as large print on
// the device.
//
// Optical sizes are used the way Literata intends: the 7 pt cut (open, sturdy,
// large x-height) for text, the 36 pt cut (tighter, finer) for display. That
// distinction is most of why the page looks like a newspaper rather than a
// website.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rsspaper {

enum class FaceId : uint8_t {
  Body,        // running text
  BodyItalic,
  BodyBold,
  Meta,        // bylines, captions, folios
  MetaBold,    // section labels, kickers
  Deck,        // front-page standfirst
  Head,        // article and section headlines
  HeadItalic,
  Lead,        // front-page lead headline, and the nameplate
  Count,
};

constexpr size_t kFaceCount = static_cast<size_t>(FaceId::Count);

enum class Charset : uint8_t {
  Latin1,    // U+0020-U+00FF plus the typographic punctuation feeds use
  Display,   // ASCII printable plus the same punctuation; for the big sizes
};

struct FaceSpec {
  FaceId id;
  const char* name;  // as stored in the pack, <= 15 bytes
  const char* ttf;   // filename under assets/fonts
  int px;
  Charset charset;
};

extern const FaceSpec kFaceSpecs[kFaceCount];

const FaceSpec& face_spec(FaceId id);
// Returns FaceId::Count when the name isn't one of ours.
FaceId face_id_from_name(const char* name);

// Codepoints a charset covers, written into `out` (which must hold at least
// 256 entries). Returns the count.
size_t charset_codepoints(Charset cs, uint32_t* out, size_t cap);

// What to draw when the pack has no glyph for `cp`.
//
// A book face has no geometric shapes or dingbats, but publishers use them:
// Daring Fireball prefixes link posts with ★, feeds carry ✓ and ▸ and the
// occasional emoji. Three outcomes, in order of preference:
//
//   a substitute  — a character Literata does have and that means the same
//                   thing (★ becomes •, ▸ becomes ›)
//   kDropGlyph    — draw nothing, advance nothing. Right for decoration: a
//                   box in the middle of a headline is worse than no mark.
//   kTofuGlyph    — U+FFFD. Right for *letters* we can't draw, because a
//                   headline in a script we have no glyphs for should look
//                   missing rather than silently become blank.
constexpr uint32_t kDropGlyph = 0;
constexpr uint32_t kTofuGlyph = 0xFFFD;

uint32_t fallback_codepoint(uint32_t cp);

}  // namespace rsspaper
