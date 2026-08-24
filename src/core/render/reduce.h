// Reducing the 8-bit framebuffer to what the panel can actually show.
//
// This lives in core rather than in the simulator because the device and the
// simulator must reduce identically — otherwise `--depth mono1` is a picture
// of a page nobody will ever see. Thin serifs survive one reduction and not
// the other, so the reduction is part of the product, not a preview.
#pragma once

#include "core/render/framebuffer.h"

namespace diarium {

// The panel's full-refresh mode: 8 levels, no dithering needed.
void reduce_to_grey3(Framebuffer* fb);

// The panel's fast partial-refresh mode. Atkinson dithering: it distributes
// 6/8 of the error and drops the rest, which keeps text crisper than
// Floyd-Steinberg at the cost of slightly flatter greys, and it is what e-ink
// devices have historically used.
//
// Error is carried in three rolling rows rather than a full-size buffer,
// because Atkinson never reaches further than two rows down and a whole-image
// buffer would be 3 MB — more than the device has to spare.
void reduce_to_mono1(Framebuffer* fb);

}  // namespace diarium
