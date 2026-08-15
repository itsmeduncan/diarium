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
  static const uint32_t kExtra[] = {
      0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E,
      0x2020, 0x2021, 0x2022, 0x2026, 0x2030, 0x2039, 0x203A, 0x2044,
      0x20AC, 0x2122, 0x2190, 0x2192, 0x2212, 0x2605, 0x2606, 0x25A3,
      0x25AA, 0x25B8, 0x2713, 0xFFFD,
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

}  // namespace rsspaper
