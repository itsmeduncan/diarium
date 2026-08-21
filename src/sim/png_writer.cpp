#include "sim/png_writer.h"

#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO_SPRINTF
#include "stb_image_write.h"

namespace diarium {
namespace sim {

bool write_png(const Framebuffer& fb, Depth depth, const std::string& path) {
  Framebuffer copy = fb;
  if (depth == Depth::Grey3) reduce_to_grey3(&copy);
  if (depth == Depth::Mono1) reduce_to_mono1(&copy);

  return stbi_write_png(path.c_str(), copy.width(), copy.height(), 1,
                        copy.pixels(), copy.width()) != 0;
}

}  // namespace sim
}  // namespace diarium
