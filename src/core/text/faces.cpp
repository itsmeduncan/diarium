#include "core/text/faces.h"

#include <cstring>

namespace rsspaper {

const FaceSpec kFaceSpecs[kFaceCount] = {
    {FaceId::Body, "body", "Literata7pt-Regular.ttf", 27, Charset::Latin1},
    {FaceId::BodyItalic, "body-i", "Literata7pt-Italic.ttf", 27,
     Charset::Latin1},
    {FaceId::BodyBold, "body-b", "Literata7pt-Bold.ttf", 27, Charset::Latin1},
    {FaceId::Meta, "meta", "Literata7pt-Italic.ttf", 20, Charset::Latin1},
    {FaceId::MetaBold, "meta-b", "Literata7pt-Bold.ttf", 20, Charset::Latin1},
    {FaceId::Deck, "deck", "Literata7pt-Regular.ttf", 32, Charset::Latin1},
    {FaceId::Head, "head", "Literata36pt-Bold.ttf", 44, Charset::Latin1},
    {FaceId::HeadItalic, "head-i", "Literata36pt-Italic.ttf", 44,
     Charset::Display},
    {FaceId::Lead, "lead", "Literata36pt-Bold.ttf", 66, Charset::Display},
};

const FaceSpec& face_spec(FaceId id) {
  const size_t i = static_cast<size_t>(id);
  return kFaceSpecs[i < kFaceCount ? i : 0];
}

FaceId face_id_from_name(const char* name) {
  for (const FaceSpec& s : kFaceSpecs) {
    if (std::strcmp(s.name, name) == 0) return s.id;
  }
  return FaceId::Count;
}

size_t charset_codepoints(Charset cs, uint32_t* out, size_t cap) {
  // Punctuation feeds actually emit, beyond Latin-1: curly quotes, dashes,
  // ellipsis, bullet, prime, and the arrows that turn up in link posts.
  //
  // Only characters Literata actually contains. Geometric shapes — ★ ▣ ▪ ✓ —
  // are not in a book face, and asking for them buys a tofu box rather than a
  // symbol. `fontgen --verbose` reports anything requested and absent, so this
  // list stays honest.
  static const uint32_t kExtra[] = {
      0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E,
      0x2020, 0x2021, 0x2022, 0x2026, 0x2030, 0x2039, 0x203A, 0x2044,
      0x20AC, 0x2122, 0x2190, 0x2192, 0x2212, 0xFFFD,
  };

  size_t n = 0;
  auto push = [&](uint32_t cp) {
    if (n < cap) out[n++] = cp;
  };

  const uint32_t last_ascii = 0x7E;
  for (uint32_t cp = 0x20; cp <= last_ascii; ++cp) push(cp);
  if (cs == Charset::Latin1) {
    for (uint32_t cp = 0xA0; cp <= 0xFF; ++cp) push(cp);
  }
  for (uint32_t cp : kExtra) push(cp);
  return n;
}

uint32_t fallback_codepoint(uint32_t cp) {
  struct Substitute {
    uint32_t from;
    uint32_t to;
  };
  // Only where the replacement genuinely carries the same meaning. A wrong
  // character is worse than a missing one.
  static const Substitute kSubstitutes[] = {
      {0x2605, 0x2022},  // ★ black star        -> • bullet
      {0x2606, 0x2022},  // ☆ white star        -> • bullet
      {0x25A0, 0x2022},  // ■ black square      -> • bullet
      {0x25A3, 0x2022},  // ▣ square in square  -> • bullet
      {0x25AA, 0x2022},  // ▪ small square      -> • bullet
      {0x25CF, 0x2022},  // ● black circle      -> • bullet
      {0x2043, 0x2013},  // ⁃ hyphen bullet     -> – en dash
      {0x25B8, 0x203A},  // ▸ small triangle    -> › single guillemet
      {0x25B6, 0x203A},  // ▶ black triangle    -> › single guillemet
      {0x00AB, 0x00AB},  // « already in Latin-1, listed for symmetry
      {0x2212, 0x2013},  // − minus             -> – en dash
      {0x2010, 0x002D},  // ‐ hyphen            -> - hyphen-minus
      {0x2011, 0x002D},  // ‑ non-breaking      -> - hyphen-minus
      {0x00A0, 0x0020},  // no-break space      -> space
      {0x202F, 0x0020},  // narrow no-break     -> space
      {0x2009, 0x0020},  // thin space          -> space
      {0x2028, 0x0020},  // line separator      -> space
  };
  for (const Substitute& s : kSubstitutes) {
    if (s.from == cp) return s.to;
  }

  // Decoration we have no stand-in for: drop it rather than punch a box
  // through a headline. Covers dingbats, arrows, geometric shapes, emoji and
  // the private use area.
  const bool is_symbol =
      (cp >= 0x2190 && cp <= 0x2BFF) ||  // arrows through misc symbols
      (cp >= 0x2E00 && cp <= 0x2E7F) ||  // supplemental punctuation
      (cp >= 0xE000 && cp <= 0xF8FF) ||  // private use
      (cp >= 0xFE00 && cp <= 0xFE0F) ||  // variation selectors
      (cp >= 0x1F000);                   // emoji and friends
  if (is_symbol) return kDropGlyph;

  // Anything else is presumed to be a letter. Show that it is missing.
  return kTofuGlyph;
}

}  // namespace rsspaper
