// fontgen — bakes the faces declared in core/text/faces.h into a runtime pack.
//
// Build-time only. The device never sees a TTF: it loads the pack once into
// PSRAM and blits pre-rasterised 4-bit alpha bitmaps, which is the difference
// between a page turn that costs a few milliseconds and one that costs a
// hundred.
//
//   fontgen --fonts assets/fonts --out build/literata.rfp [--verbose]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/io/file_byte_source.h"
#include "core/text/faces.h"
#include "core/text/font_pack.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace {

using rsspaper::Charset;
using rsspaper::FaceSpec;
using rsspaper::kFaceSpecs;
using rsspaper::kFontPackFaceBytes;
using rsspaper::kFontPackGlyphBytes;
using rsspaper::kFontPackHeaderBytes;
using rsspaper::kFontPackKernBytes;
using rsspaper::kFontPackMagic;
using rsspaper::kSubpixel;

struct BakedGlyph {
  uint32_t codepoint = 0;
  int16_t bearing_x = 0;
  int16_t bearing_y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  int16_t advance = 0;  // 1/16 px
  uint32_t bitmap_offset = 0;
};

struct KernPair {
  uint16_t a = 0;  // glyph index within this face
  uint16_t b = 0;
  int16_t amount = 0;  // 1/16 px
};

struct BakedFace {
  std::string name;
  int px = 0;
  int16_t ascent = 0, descent = 0, line_gap = 0, x_height = 0, cap_height = 0;
  std::vector<BakedGlyph> glyphs;
  std::vector<KernPair> kerns;
  std::vector<uint8_t> bitmaps;
};

void put_u16(std::vector<uint8_t>& out, size_t at, uint16_t v) {
  out[at] = static_cast<uint8_t>(v & 0xFF);
  out[at + 1] = static_cast<uint8_t>(v >> 8);
}
void put_u32(std::vector<uint8_t>& out, size_t at, uint32_t v) {
  out[at] = static_cast<uint8_t>(v & 0xFF);
  out[at + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  out[at + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  out[at + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}
void append_u16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>(v >> 8));
}
void append_u32(std::vector<uint8_t>& out, uint32_t v) {
  append_u16(out, static_cast<uint16_t>(v & 0xFFFF));
  append_u16(out, static_cast<uint16_t>(v >> 16));
}

// 8-bit coverage to 4-bit, rounding rather than truncating: a straight >>4
// loses the lightest antialiasing, which is exactly the coverage that keeps
// small serifs from breaking up on a reflective panel.
uint8_t to_alpha4(uint8_t v) {
  return static_cast<uint8_t>((static_cast<int>(v) * 15 + 127) / 255);
}

bool bake_face(const FaceSpec& spec, const std::string& fonts_dir,
               BakedFace* out, bool verbose, std::string* error) {
  std::string ttf;
  const std::string path = fonts_dir + "/" + spec.ttf;
  if (!rsspaper::read_file(path, &ttf)) {
    *error = "cannot read " + path;
    return false;
  }

  stbtt_fontinfo font;
  const unsigned char* data = reinterpret_cast<const unsigned char*>(ttf.data());
  if (stbtt_InitFont(&font, data, stbtt_GetFontOffsetForIndex(data, 0)) == 0) {
    *error = path + " is not a font stb_truetype can read";
    return false;
  }

  // ScaleForMappingEmToPixels, not ScaleForPixelHeight: we want the em square
  // to be `px` tall so that a size in pixels means the same thing it means in
  // every other typographic context.
  const float scale = stbtt_ScaleForMappingEmToPixels(&font,
                                                      static_cast<float>(spec.px));

  int ascent = 0, descent = 0, line_gap = 0;
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);

  out->name = spec.name;
  out->px = spec.px;
  out->ascent = static_cast<int16_t>(ascent * scale + 0.5f);
  out->descent = static_cast<int16_t>(-descent * scale + 0.5f);  // positive
  out->line_gap = static_cast<int16_t>(line_gap * scale + 0.5f);

  uint32_t codepoints[512];
  const size_t n = rsspaper::charset_codepoints(spec.charset, codepoints, 512);

  // Glyph indices in the pack, parallel to out->glyphs, for the kern pass.
  std::vector<int> stb_indices;
  std::vector<uint32_t> missing;

  for (size_t i = 0; i < n; ++i) {
    const uint32_t cp = codepoints[i];
    const int gi = stbtt_FindGlyphIndex(&font, static_cast<int>(cp));
    if (gi == 0 && cp != 0xFFFD) {
      // U+FFFD is deliberately baked as .notdef — tofu is the honest signal
      // for a character we cannot draw. Everything else is simply absent.
      missing.push_back(cp);
      continue;
    }

    int advance = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&font, gi, &advance, &lsb);

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(&font, gi, scale, scale, &x0, &y0, &x1, &y1);

    BakedGlyph g;
    g.codepoint = cp;
    g.advance = static_cast<int16_t>(advance * scale * kSubpixel + 0.5f);
    g.bearing_x = static_cast<int16_t>(x0);
    g.bearing_y = static_cast<int16_t>(-y0);  // stb's y0 is down-positive
    g.width = static_cast<uint16_t>(x1 > x0 ? x1 - x0 : 0);
    g.height = static_cast<uint16_t>(y1 > y0 ? y1 - y0 : 0);
    g.bitmap_offset = static_cast<uint32_t>(out->bitmaps.size());

    if (g.width > 0 && g.height > 0) {
      const int w = g.width, h = g.height;
      std::vector<unsigned char> mono(static_cast<size_t>(w) * h, 0);
      stbtt_MakeGlyphBitmap(&font, mono.data(), w, h, w, scale, scale, gi);

      const size_t stride = (static_cast<size_t>(w) + 1) / 2;
      out->bitmaps.resize(out->bitmaps.size() + stride * static_cast<size_t>(h),
                          0);
      uint8_t* dst = out->bitmaps.data() + g.bitmap_offset;
      for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
          const uint8_t a = to_alpha4(mono[static_cast<size_t>(y) * w + x]);
          uint8_t& cell = dst[static_cast<size_t>(y) * stride + x / 2];
          if (x % 2 == 0) {
            cell = static_cast<uint8_t>((cell & 0x0F) | (a << 4));
          } else {
            cell = static_cast<uint8_t>((cell & 0xF0) | a);
          }
        }
      }
    }

    if (cp == 'x') out->x_height = static_cast<int16_t>(g.height);
    if (cp == 'H') out->cap_height = static_cast<int16_t>(g.height);

    out->glyphs.push_back(g);
    stb_indices.push_back(gi);
  }

  // charset_codepoints emits ascending codepoints, so the table is already
  // sorted for the runtime's binary search — assert rather than re-sort.
  for (size_t i = 1; i < out->glyphs.size(); ++i) {
    if (out->glyphs[i - 1].codepoint >= out->glyphs[i].codepoint) {
      *error = "glyph table is not sorted; charset_codepoints changed";
      return false;
    }
  }

  // Kerning. stb_truetype reads GPOS as well as the legacy kern table, which
  // matters because modern fonts including Literata ship GPOS only.
  size_t probed = 0;
  for (size_t a = 0; a < out->glyphs.size(); ++a) {
    for (size_t b = 0; b < out->glyphs.size(); ++b) {
      ++probed;
      const int raw = stbtt_GetGlyphKernAdvance(&font, stb_indices[a],
                                                stb_indices[b]);
      if (raw == 0) continue;
      const int amount = static_cast<int>(raw * scale * kSubpixel +
                                          (raw < 0 ? -0.5f : 0.5f));
      if (amount == 0) continue;
      if (out->kerns.size() >= 65535) break;
      out->kerns.push_back(KernPair{static_cast<uint16_t>(a),
                                    static_cast<uint16_t>(b),
                                    static_cast<int16_t>(amount)});
    }
  }

  if (verbose) {
    std::printf("  %-8s %3d px  %3zu glyphs  %5zu kern pairs (of %zu probed)  "
                "%6zu KB bitmaps\n",
                spec.name, spec.px, out->glyphs.size(), out->kerns.size(),
                probed, out->bitmaps.size() / 1024);
    if (!missing.empty()) {
      // Worth saying out loud: a codepoint the charset asks for and the face
      // cannot supply renders as tofu, and that is far cheaper to notice here
      // than in a page render.
      std::printf("           %zu requested codepoints absent from the face:",
                  missing.size());
      for (size_t i = 0; i < missing.size() && i < 12; ++i) {
        std::printf(" U+%04X", missing[i]);
      }
      std::printf("%s\n", missing.size() > 12 ? " ..." : "");
    }
  }
  return true;
}

std::vector<uint8_t> assemble(const std::vector<BakedFace>& faces) {
  std::vector<uint8_t> out;
  const size_t face_table = kFontPackHeaderBytes;
  const size_t body_start = face_table + faces.size() * kFontPackFaceBytes;

  out.resize(body_start, 0);
  put_u32(out, 0, kFontPackMagic);
  put_u16(out, 4, 1);
  put_u16(out, 6, static_cast<uint16_t>(faces.size()));

  for (size_t i = 0; i < faces.size(); ++i) {
    const BakedFace& f = faces[i];

    const uint32_t glyph_off = static_cast<uint32_t>(out.size());
    for (const BakedGlyph& g : f.glyphs) {
      append_u32(out, g.codepoint);
      append_u16(out, static_cast<uint16_t>(g.bearing_x));
      append_u16(out, static_cast<uint16_t>(g.bearing_y));
      append_u16(out, g.width);
      append_u16(out, g.height);
      append_u16(out, static_cast<uint16_t>(g.advance));
      append_u16(out, 0);  // reserved
      append_u32(out, g.bitmap_offset);
    }

    const uint32_t kern_off = static_cast<uint32_t>(out.size());
    for (const KernPair& k : f.kerns) {
      append_u16(out, k.a);
      append_u16(out, k.b);
      append_u16(out, static_cast<uint16_t>(k.amount));
      append_u16(out, 0);  // reserved
    }

    const uint32_t bitmap_off = static_cast<uint32_t>(out.size());
    out.insert(out.end(), f.bitmaps.begin(), f.bitmaps.end());

    const size_t rec = face_table + i * kFontPackFaceBytes;
    std::memcpy(out.data() + rec, f.name.c_str(),
                f.name.size() < 15 ? f.name.size() : 15);
    put_u16(out, rec + 16, static_cast<uint16_t>(f.px));
    put_u16(out, rec + 18, static_cast<uint16_t>(f.ascent));
    put_u16(out, rec + 20, static_cast<uint16_t>(f.descent));
    put_u16(out, rec + 22, static_cast<uint16_t>(f.line_gap));
    put_u16(out, rec + 24, static_cast<uint16_t>(f.x_height));
    put_u16(out, rec + 26, static_cast<uint16_t>(f.cap_height));
    put_u16(out, rec + 28, static_cast<uint16_t>(f.glyphs.size()));
    put_u16(out, rec + 30, static_cast<uint16_t>(f.kerns.size()));
    put_u32(out, rec + 32, glyph_off);
    put_u32(out, rec + 36, kern_off);
    put_u32(out, rec + 40, bitmap_off);
    put_u32(out, rec + 44, static_cast<uint32_t>(f.bitmaps.size()));
  }

  put_u32(out, 8, static_cast<uint32_t>(out.size()));
  return out;
}

std::string arg_value(int argc, char** argv, const char* name,
                      const char* fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
  }
  return fallback;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string fonts_dir = arg_value(argc, argv, "--fonts", "assets/fonts");
  const std::string out_path =
      arg_value(argc, argv, "--out", "build/literata.rfp");
  bool verbose = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--verbose") == 0) verbose = true;
  }

  if (verbose) std::printf("fontgen: baking from %s\n", fonts_dir.c_str());

  std::vector<BakedFace> faces;
  faces.reserve(rsspaper::kFaceCount);
  for (const FaceSpec& spec : kFaceSpecs) {
    BakedFace baked;
    std::string error;
    if (!bake_face(spec, fonts_dir, &baked, verbose, &error)) {
      std::fprintf(stderr, "fontgen: %s\n", error.c_str());
      return 1;
    }
    faces.push_back(std::move(baked));
  }

  const std::vector<uint8_t> pack = assemble(faces);
  if (!rsspaper::write_file(out_path,
                            std::string(pack.begin(), pack.end()))) {
    std::fprintf(stderr, "fontgen: cannot write %s\n", out_path.c_str());
    return 1;
  }

  std::printf("fontgen: %s — %zu faces, %zu KB\n", out_path.c_str(),
              faces.size(), pack.size() / 1024);
  return 0;
}
