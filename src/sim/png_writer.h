// PNG output for the simulator, including the panel's actual bit depths.
//
// Looking at an 8-bit render tells you whether the layout is right. Looking at
// the 3-bit and 1-bit reductions tells you whether it will still be right on
// the device, which is a different question — thin serifs and hairline rules
// survive one and not the other.
#pragma once

#include <string>

#include "core/render/framebuffer.h"

namespace rsspaper {
namespace sim {

enum class Depth {
  Grey8,   // what the layout engine produced
  Grey3,   // the panel's full-refresh mode, 8 levels
  Mono1,   // the panel's fast partial-refresh mode
};

// Writes `fb` reduced to `depth`. Returns false if the file can't be written.
bool write_png(const Framebuffer& fb, Depth depth, const std::string& path);

// Reduces in place, so the caller can inspect exactly what the panel shows.
void reduce_to_grey3(Framebuffer* fb);
// Atkinson dithering: fewer artefacts than Floyd-Steinberg on text, and it's
// what e-ink devices have historically used.
void reduce_to_mono1(Framebuffer* fb);

}  // namespace sim
}  // namespace rsspaper
