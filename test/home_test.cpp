#include <string>
#include <vector>

#include "core/edition/edition.h"
#include "core/render/framebuffer.h"
#include "core/text/font_pack.h"
#include "core/ui/home.h"
#include "doctest.h"

using namespace diarium;

namespace {
const FontPack* pack() {
  static FontPack fonts;
  static bool tried = false;
  if (!tried) {
    tried = true;
    std::string error;
    fonts.load_file("build/literata.rfp", &error);
  }
  return fonts.loaded() ? &fonts : nullptr;
}

Edition three_story_edition() {
  Edition ed;
  ed.title = "Diarium";
  ed.stories.push_back(StoryRef{});
  ed.stories.back().section = "Technology";
  ed.stories.push_back(StoryRef{});
  ed.stories.back().section = "Technology";
  ed.stories.push_back(StoryRef{});
  ed.stories.back().section = "World";
  return ed;
}
}  // namespace

TEST_CASE("home breakdown counts unread stories per section, in order") {
  const Edition ed = three_story_edition();
  const std::vector<size_t> order = {0, 1, 2};
  const std::vector<bool> unread = {true, false, true};  // one Tech read

  const HomeSummary s = summarize_home(ed, order, unread);
  CHECK(s.unread_total == 2);
  REQUIRE(s.sections.size() == 2);
  CHECK(s.sections[0].name == "Technology");
  CHECK(s.sections[0].count == 1);
  CHECK(s.sections[1].name == "World");
  CHECK(s.sections[1].count == 1);
}

TEST_CASE("home render fills the page and does not crash on a real pack") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;  // no pack built; nothing to draw
  const Edition ed = three_story_edition();
  Framebuffer fb;
  render_home(*fonts, ed, {0, 1, 2}, {true, true, true}, "composed on device",
              &fb);
  // A drawn page is not blank paper: at least one inked pixel exists.
  bool any_ink = false;
  for (int y = 0; y < fb.height() && !any_ink; ++y) {
    for (int x = 0; x < fb.width(); ++x) {
      if (fb.pixels()[y * fb.width() + x] < 128) { any_ink = true; break; }
    }
  }
  CHECK(any_ink);
}
