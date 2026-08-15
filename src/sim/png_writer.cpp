#include "sim/png_writer.h"

#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO_SPRINTF
#include "stb_image_write.h"

namespace rsspaper {
namespace sim {
namespace {

uint8_t quantize3(uint8_t v) {
  // 8 levels, expanded back across the full range so the PNG looks like the
  // panel rather than like a dimmed version of it.
  const int level = (static_cast<int>(v) * 7 + 127) / 255;
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

  // Atkinson: distributes 6/8 of the error and drops the rest, which keeps
  // text crisper than Floyd-Steinberg at the cost of slightly flatter greys.
  std::vector<int> buf(static_cast<size_t>(w) * h);
  for (size_t i = 0; i < buf.size(); ++i) buf[i] = p[i];

  auto spread = [&](int x, int y, int err) {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    buf[static_cast<size_t>(y) * w + x] += err;
  };

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y) * w + x;
      const int old = buf[i];
      const int nv = old < 128 ? 0 : 255;
      buf[i] = nv;
      const int err = (old - nv) / 8;
      spread(x + 1, y, err);
      spread(x + 2, y, err);
      spread(x - 1, y + 1, err);
      spread(x, y + 1, err);
      spread(x + 1, y + 1, err);
      spread(x, y + 2, err);
    }
  }
  for (size_t i = 0; i < buf.size(); ++i) {
    p[i] = static_cast<uint8_t>(buf[i] <= 0 ? 0 : (buf[i] >= 255 ? 255 : buf[i]));
  }
}

bool write_png(const Framebuffer& fb, Depth depth, const std::string& path) {
  Framebuffer copy = fb;
  if (depth == Depth::Grey3) reduce_to_grey3(&copy);
  if (depth == Depth::Mono1) reduce_to_mono1(&copy);

  return stbi_write_png(path.c_str(), copy.width(), copy.height(), 1,
                        copy.pixels(), copy.width()) != 0;
}

}  // namespace sim
}  // namespace rsspaper
