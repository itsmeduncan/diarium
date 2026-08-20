// The page a reader gets when there is nothing to read.
#include "core/ui/notice.h"

#include "core/render/framebuffer.h"
#include "core/text/font_pack.h"
#include "doctest.h"

using namespace rsspaper;

namespace {

bool has_ink(const Framebuffer& fb) {
  for (int y = 0; y < fb.height(); ++y) {
    for (int x = 0; x < fb.width(); ++x) {
      if (fb.get(x, y) != kPaper) return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("a notice marks the page even with no font pack") {
  // The card carries the font pack, so the one failure that matters most is
  // the one where no face is available to explain it.
  FontPack none;
  Framebuffer fb;
  fb.fill(kPaper);
  render_notice(none, "No card", "Insert a card.", &fb);
  CHECK(has_ink(fb));
}

TEST_CASE("a notice clears whatever was on the page before") {
  FontPack none;
  Framebuffer fb;
  fb.fill(kInk);
  render_notice(none, "No card", "Insert a card.", &fb);
  CHECK(fb.get(fb.width() - 1, fb.height() - 1) == kPaper);
}
