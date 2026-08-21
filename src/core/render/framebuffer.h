// An 8-bit greyscale canvas the size of the panel.
//
// Layout and drawing work in 8-bit even though the hardware shows 3-bit, and
// 1-bit for partial refreshes. Compositing antialiased glyphs at full depth
// and reducing once at the end is both simpler and better-looking than trying
// to draw directly in the target depth.
//
// 1024 x 758 = 776 KB, which lives in PSRAM on the device.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/layout/page.h"
#include "core/text/font_pack.h"

namespace diarium {

// 0 is black, 255 is the bare panel. Named because "white" on e-ink is a
// light grey and the distinction matters when choosing rule weights.
constexpr uint8_t kInk = 0;
constexpr uint8_t kPaper = 255;

class Framebuffer {
 public:
  Framebuffer(int width = page_width(), int height = page_height());

  int width() const { return w_; }
  int height() const { return h_; }
  const uint8_t* pixels() const { return px_.data(); }
  uint8_t* pixels() { return px_.data(); }

  void fill(uint8_t grey);
  void set(int x, int y, uint8_t grey);
  uint8_t get(int x, int y) const;

  void fill_rect(int x, int y, int w, int h, uint8_t grey);
  void frame_rect(int x, int y, int w, int h, uint8_t grey);

  // Composites a glyph with `alpha` in 0-15 against the existing pixel, so
  // overlapping marks darken rather than replace.
  void blit_glyph(const Face& face, const Glyph& glyph, int pen_x, int baseline,
                  uint8_t ink);

  // Draws a UTF-8 run with kerning. `x` is in 1/16 px; the return value is the
  // pen position after the run, also in 1/16 px.
  int draw_text(const Face& face, const std::string& utf8, int x, int baseline,
                uint8_t ink);

  // Same, with a fixed extra advance between glyphs — used for the nameplate,
  // where letterspaced caps are the point.
  int draw_text_tracked(const Face& face, const std::string& utf8, int x,
                        int baseline, uint8_t ink, int tracking);

 private:
  int w_;
  int h_;
  std::vector<uint8_t> px_;
};

}  // namespace diarium
