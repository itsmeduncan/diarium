#include "core/layout/page.h"

namespace diarium {
namespace {

// The panel's long and short edges. Orientation only decides which one is the
// width; the product of the two — the raster the panel actually shows — is the
// same 1024x758 either way.
constexpr int kLongEdge = 1024;
constexpr int kShortEdge = 758;

// Landscape until told. See the note in page.h: the shipped default is
// portrait, but it is expressed in config and applied at startup, not baked in
// here, so that reading a landscape edition never depends on the compose that
// wrote it having agreed with a hidden constant.
Orientation g_orientation = Orientation::Landscape;

}  // namespace

void set_orientation(Orientation o) { g_orientation = o; }

Orientation orientation() { return g_orientation; }

int page_width() {
  return g_orientation == Orientation::Portrait ? kShortEdge : kLongEdge;
}

int page_height() {
  return g_orientation == Orientation::Portrait ? kLongEdge : kShortEdge;
}

}  // namespace diarium
