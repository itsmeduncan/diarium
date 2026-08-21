#include "core/render/reduce.h"

#include <cstdint>
#include <vector>

namespace diarium {
namespace {

uint8_t quantize3(uint8_t v) {
  // Eight evenly spaced levels, rounded to nearest.
  const int level = (v * 7 + 127) / 255;
  return static_cast<uint8_t>(level * 255 / 7);
}

}  // namespace

void reduce_to_grey3(Framebuffer* fb) {
  uint8_t* p = fb->pixels();
  const size_t n = static_cast<size_t>(fb->width()) * fb->height();
  for (size_t i = 0; i < n; ++i) p[i] = quantize3(p[i]);
}

void reduce_to_mono1(Framebuffer* fb) {
  const int w = fb->width();
  const int h = fb->height();
  uint8_t* p = fb->pixels();

  // Three rolling rows of accumulated error: the current row, and the two
  // Atkinson can reach forward into.
  std::vector<int16_t> err(static_cast<size_t>(w) * 3, 0);
  int16_t* row[3] = {err.data(), err.data() + w, err.data() + 2 * w};

  for (int y = 0; y < h; ++y) {
    int16_t* cur = row[y % 3];
    int16_t* nxt = row[(y + 1) % 3];
    int16_t* nxt2 = row[(y + 2) % 3];

    for (int x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y) * w + x;
      const int old = static_cast<int>(p[i]) + cur[x];
      const int nv = old < 128 ? 0 : 255;
      p[i] = static_cast<uint8_t>(nv);

      const int e = (old - nv) / 8;
      if (e != 0) {
        if (x + 1 < w) cur[x + 1] = static_cast<int16_t>(cur[x + 1] + e);
        if (x + 2 < w) cur[x + 2] = static_cast<int16_t>(cur[x + 2] + e);
        if (x - 1 >= 0) nxt[x - 1] = static_cast<int16_t>(nxt[x - 1] + e);
        nxt[x] = static_cast<int16_t>(nxt[x] + e);
        if (x + 1 < w) nxt[x + 1] = static_cast<int16_t>(nxt[x + 1] + e);
        nxt2[x] = static_cast<int16_t>(nxt2[x] + e);
      }
    }
    // This row is finished; recycle it as the row two below the next one.
    for (int x = 0; x < w; ++x) cur[x] = 0;
  }
}

}  // namespace diarium
