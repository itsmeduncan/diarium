// PNG output for the simulator, including the panel's actual bit depths.
//
// Looking at an 8-bit render tells you whether the layout is right. Looking at
// the 3-bit and 1-bit reductions tells you whether it will still be right on
// the device, which is a different question — thin serifs and hairline rules
// survive one and not the other.
#pragma once

#include <string>

#include "core/render/framebuffer.h"
#include "core/render/reduce.h"

namespace diarium {
namespace sim {

enum class Depth {
  Grey8,   // what the layout engine produced
  Grey3,   // the panel's full-refresh mode, 8 levels
  Mono1,   // the panel's fast partial-refresh mode
};

// Writes `fb` reduced to `depth`. Returns false if the file can't be written.
bool write_png(const Framebuffer& fb, Depth depth, const std::string& path);

// The reductions themselves live in core/render/reduce.h, because the device
// performs the identical ones — a mono1 PNG is only worth looking at if it is
// what the panel will actually show.

}  // namespace sim
}  // namespace diarium
