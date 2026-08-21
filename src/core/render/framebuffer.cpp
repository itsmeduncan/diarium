#include "core/render/framebuffer.h"

#include "core/base/str.h"

namespace diarium {

Framebuffer::Framebuffer(int width, int height)
    : w_(width > 0 ? width : 1), h_(height > 0 ? height : 1) {
  px_.assign(static_cast<size_t>(w_) * static_cast<size_t>(h_), kPaper);
}

void Framebuffer::fill(uint8_t grey) {
  for (uint8_t& p : px_) p = grey;
}

void Framebuffer::set(int x, int y, uint8_t grey) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
  px_[static_cast<size_t>(y) * w_ + x] = grey;
}

uint8_t Framebuffer::get(int x, int y) const {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return kPaper;
  return px_[static_cast<size_t>(y) * w_ + x];
}

void Framebuffer::fill_rect(int x, int y, int w, int h, uint8_t grey) {
  for (int yy = y; yy < y + h; ++yy) {
    for (int xx = x; xx < x + w; ++xx) set(xx, yy, grey);
  }
}

void Framebuffer::frame_rect(int x, int y, int w, int h, uint8_t grey) {
  if (w <= 0 || h <= 0) return;
  fill_rect(x, y, w, 1, grey);
  fill_rect(x, y + h - 1, w, 1, grey);
  fill_rect(x, y, 1, h, grey);
  fill_rect(x + w - 1, y, 1, h, grey);
}

void Framebuffer::blit_glyph(const Face& face, const Glyph& glyph, int pen_x,
                             int baseline, uint8_t ink) {
  const int gx = pen_x + glyph.bearing_x;
  const int gy = baseline - glyph.bearing_y;
  for (int y = 0; y < glyph.height; ++y) {
    const int py = gy + y;
    if (py < 0 || py >= h_) continue;
    for (int x = 0; x < glyph.width; ++x) {
      const int px = gx + x;
      if (px < 0 || px >= w_) continue;
      const uint8_t a = face.alpha_at(glyph, x, y);
      if (a == 0) continue;
      uint8_t& dst = px_[static_cast<size_t>(py) * w_ + px];
      // dst = dst*(1-a) + ink*a, with a in 0..15.
      const int blended =
          (static_cast<int>(dst) * (15 - a) + static_cast<int>(ink) * a) / 15;
      if (blended < dst) dst = static_cast<uint8_t>(blended);
    }
  }
}

int Framebuffer::draw_text_tracked(const Face& face, const std::string& utf8,
                                   int x, int baseline, uint8_t ink,
                                   int tracking) {
  if (!face.valid()) return x;
  int pen = x;
  uint32_t prev = 0;
  size_t i = 0;
  while (i < utf8.size()) {
    const uint32_t cp = utf8_next(utf8, i);
    if (prev != 0) pen += face.kern(prev, cp) + tracking;
    // Face::glyph already applies the fallback chain; a second one here
    // would let the renderer disagree with the line breaker.
    const Glyph* g = face.glyph(cp);
    if (g != nullptr) {
      blit_glyph(face, *g, to_px(pen), baseline, ink);
      pen += g->advance;
    }
    prev = cp;
  }
  return pen;
}

int Framebuffer::draw_text(const Face& face, const std::string& utf8, int x,
                           int baseline, uint8_t ink) {
  return draw_text_tracked(face, utf8, x, baseline, ink, 0);
}

}  // namespace diarium
