// Gesture recognition and reader navigation, driven through the same HAL the
// device will implement — with a fake display that records refresh modes
// instead of driving a panel.
#include <string>
#include <vector>

#include "core/edition/edition.h"
#include "core/text/font_pack.h"
#include "core/ui/gesture.h"
#include "core/ui/reader.h"
#include "doctest.h"
#include "hal/hal.h"

using namespace rsspaper;

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

class FakeDisplay final : public IDisplay {
 public:
  int width() const override { return fb_.width(); }
  int height() const override { return fb_.height(); }
  Framebuffer& framebuffer() override { return fb_; }
  void flush(RefreshMode mode) override { modes.push_back(mode); }
  void set_frontlight(int level) override { light_ = level; }
  int frontlight() const override { return light_; }

  size_t count(RefreshMode m) const {
    size_t n = 0;
    for (RefreshMode x : modes) {
      if (x == m) ++n;
    }
    return n;
  }

  std::vector<RefreshMode> modes;

 private:
  Framebuffer fb_;
  int light_ = 0;
};

class FakeInput final : public IInput {
 public:
  size_t poll(TouchPoint*, size_t) override { return 0; }
  uint32_t millis() const override { return 0; }
};

class FakeClock final : public IClock {
 public:
  Epoch now() const override { return 1786864000; }
  void set_wake_alarm(Epoch) override {}
};

class FakePower final : public IPower {
 public:
  void deep_sleep_until(Epoch) override {}
  int battery_millivolts() const override { return 4000; }
};

class FakeStorage final : public IStorage {
 public:
  bool read(const std::string&, std::string*) override { return false; }
  bool write(const std::string&, const std::string&) override { return true; }
  bool exists(const std::string&) override { return false; }
  bool remove(const std::string&) override { return true; }
};

class FakeHttp final : public IHttpClient {
 public:
  std::unique_ptr<ByteSource> get(const HttpRequest&, HttpResponse*) override {
    return nullptr;
  }
};

struct Rig {
  FakeDisplay display;
  FakeInput input;
  FakeClock clock;
  FakePower power;
  FakeStorage storage;
  FakeHttp http;

  Hal hal() {
    Hal h;
    h.display = &display;
    h.input = &input;
    h.clock = &clock;
    h.power = &power;
    h.storage = &storage;
    h.http = &http;
    return h;
  }
};

Item story(const std::string& title, int day, size_t paragraphs) {
  Item it;
  it.title = title;
  it.source_name = "The Source";
  it.guid = title;
  it.published = 1786000000 + day * 86400;
  it.summary_text = "Summary of " + title + " running to a line or so.";
  for (size_t i = 0; i < paragraphs; ++i) {
    Block b;
    b.type = BlockType::Paragraph;
    b.text = "Paragraph " + std::to_string(i) + " of " + title +
             ", long enough to wrap over several lines of the measure and "
             "contribute real height to whatever page it lands on.";
    it.blocks.push_back(std::move(b));
  }
  return it;
}

Edition make_edition(const FontPack& fonts) {
  Section tech{"Technology", {}};
  Section world{"World", {}};
  for (int i = 0; i < 5; ++i) {
    tech.items.push_back(story("Tech " + std::to_string(i), 10 - i, 14));
    world.items.push_back(story("World " + std::to_string(i), 10 - i, 6));
  }
  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  return compose_edition({tech, world}, fonts, opts);
}

}  // namespace

TEST_CASE("gestures: taps, swipes and long presses") {
  GestureRecognizer g;

  SUBCASE("a short press in one place is a tap") {
    CHECK(g.update(true, 500, 300, 1000).kind == Gesture::None);
    const GestureEvent e = g.update(false, 502, 301, 1080);
    CHECK(e.kind == Gesture::Tap);
    CHECK(e.x == 500);
    CHECK(e.y == 300);
  }

  // The stroke has to be polled while the finger is down: a released panel
  // reports no coordinates, so that is the only position the recogniser can
  // honestly measure from.
  SUBCASE("moving left is a page forward, right is back") {
    g.update(true, 700, 300, 1000);
    g.update(true, 400, 305, 1150);
    CHECK(g.update(false, 0, 0, 1200).kind == Gesture::SwipeLeft);
    g.reset();
    g.update(true, 300, 300, 3000);
    g.update(true, 700, 296, 3150);
    CHECK(g.update(false, 0, 0, 3200).kind == Gesture::SwipeRight);
  }

  SUBCASE("vertical movement wins when it dominates") {
    g.update(true, 500, 200, 1000);
    g.update(true, 510, 500, 1150);
    CHECK(g.update(false, 0, 0, 1200).kind == Gesture::SwipeDown);
    g.reset();
    g.update(true, 500, 500, 3000);
    g.update(true, 495, 200, 3150);
    CHECK(g.update(false, 0, 0, 3200).kind == Gesture::SwipeUp);
  }

  SUBCASE("a release with no prior movement is a tap, not a swipe to 0,0") {
    g.update(true, 700, 300, 1000);
    CHECK(g.update(false, 0, 0, 1100).kind == Gesture::Tap);
  }

  SUBCASE("holding still fires a long press while the finger is down") {
    g.update(true, 400, 400, 1000);
    CHECK(g.update(true, 402, 401, 1300).kind == Gesture::None);
    CHECK(g.update(true, 403, 402, 1700).kind == Gesture::LongPress);
    // And it doesn't also report a tap on release.
    CHECK(g.update(false, 403, 402, 1800).kind == Gesture::None);
  }

  SUBCASE("a long press is cancelled by moving") {
    g.update(true, 400, 400, 1000);
    g.update(true, 500, 400, 1200);
    CHECK(g.update(true, 500, 400, 1900).kind == Gesture::None);
  }

  SUBCASE("movement under the threshold is still a tap") {
    g.update(true, 500, 300, 1000);
    g.update(true, 520, 310, 1050);
    CHECK(g.update(false, 0, 0, 1100).kind == Gesture::Tap);
  }

  SUBCASE("a slow drag is nothing at all") {
    g.update(true, 700, 300, 1000);
    g.update(true, 300, 300, 3900);
    const GestureEvent e = g.update(false, 0, 0, 4000);
    CHECK(e.kind == Gesture::None);
  }

  SUBCASE("a touch immediately after a release is ignored") {
    g.update(true, 500, 300, 1000);
    CHECK(g.update(false, 500, 300, 1080).kind == Gesture::Tap);
    // Bounce: down again 20 ms later.
    g.update(true, 500, 300, 1100);
    CHECK(g.update(false, 500, 300, 1180).kind == Gesture::None);
  }
}

TEST_CASE("the reader turns pages and stops at the end of the paper") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = make_edition(*fonts);
  Rig rig;
  Reader reader(ed, *fonts, rig.hal());

  CHECK(reader.mode() == ReaderMode::Browse);
  CHECK(reader.current_page() == 0);

  for (size_t i = 1; i < ed.browse_page_count; ++i) {
    CHECK(reader.next_page());
    CHECK(reader.current_page() == i);
  }
  // One past the last browse page is the end of the paper, not page zero of
  // the story text.
  CHECK(reader.next_page());
  CHECK(reader.mode() == ReaderMode::End);
  CHECK_FALSE(reader.next_page());

  CHECK(reader.previous_page());
  CHECK(reader.mode() == ReaderMode::Browse);
}

TEST_CASE("you cannot page backwards off the front") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = make_edition(*fonts);
  Rig rig;
  Reader reader(ed, *fonts, rig.hal());
  CHECK_FALSE(reader.previous_page());
  CHECK(reader.current_page() == 0);
}

TEST_CASE("tapping a lede opens the story and back returns to it") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = make_edition(*fonts);
  REQUIRE_FALSE(ed.stories.empty());

  const StoryRef& target = ed.stories[0];
  Rig rig;
  Reader reader(ed, *fonts, rig.hal());

  // Get to the page the lede is on.
  while (reader.current_page() < target.lede_page) REQUIRE(reader.next_page());
  const size_t lede_page = reader.current_page();

  const int cx = target.lede_bounds.x + target.lede_bounds.w / 2;
  const int cy = target.lede_bounds.y + target.lede_bounds.h / 2;
  REQUIRE(reader.open_story_at(cx, cy));

  CHECK(reader.mode() == ReaderMode::Story);
  CHECK(reader.current_page() == target.first_page);
  REQUIRE(reader.open_story() != nullptr);
  CHECK(reader.open_story()->title == target.title);

  CHECK(reader.back());
  CHECK(reader.mode() == ReaderMode::Browse);
  CHECK(reader.current_page() == lede_page);
}

TEST_CASE("tapping empty space opens nothing") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = make_edition(*fonts);
  Rig rig;
  Reader reader(ed, *fonts, rig.hal());
  CHECK_FALSE(reader.open_story_at(5, kPageHeight - 5));
  CHECK(reader.mode() == ReaderMode::Browse);
}

TEST_CASE("reaching the end of a story returns to its lede") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = make_edition(*fonts);

  // A story of more than one page, so the walk is real.
  const StoryRef* target = nullptr;
  for (const StoryRef& s : ed.stories) {
    if (s.page_count > 1) {
      target = &s;
      break;
    }
  }
  REQUIRE(target != nullptr);

  Rig rig;
  Reader reader(ed, *fonts, rig.hal());
  while (reader.current_page() < target->lede_page) REQUIRE(reader.next_page());
  const size_t lede_page = reader.current_page();

  REQUIRE(reader.open_story_at(target->lede_bounds.x + 10,
                               target->lede_bounds.y + 10));
  for (size_t i = 1; i < target->page_count; ++i) {
    CHECK(reader.next_page());
    CHECK(reader.mode() == ReaderMode::Story);
  }
  // One more turn walks out of the story, back to where it was chosen.
  CHECK(reader.next_page());
  CHECK(reader.mode() == ReaderMode::Browse);
  CHECK(reader.current_page() == lede_page);
}

TEST_CASE("paging back from a story's first page leaves the story") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = make_edition(*fonts);
  const StoryRef& target = ed.stories[0];

  Rig rig;
  Reader reader(ed, *fonts, rig.hal());
  while (reader.current_page() < target.lede_page) REQUIRE(reader.next_page());
  REQUIRE(reader.open_story_at(target.lede_bounds.x + 10,
                               target.lede_bounds.y + 10));
  CHECK(reader.previous_page());
  CHECK(reader.mode() == ReaderMode::Browse);
}

TEST_CASE("the section overlay jumps and closes") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = make_edition(*fonts);
  REQUIRE(ed.section_marks.size() >= 2);

  Rig rig;
  Reader reader(ed, *fonts, rig.hal());

  CHECK(reader.toggle_sections());
  CHECK(reader.mode() == ReaderMode::Sections);
  // Page turns do nothing while the overlay is up.
  CHECK_FALSE(reader.next_page());

  CHECK(reader.jump_to_section(1));
  CHECK(reader.mode() == ReaderMode::Browse);
  CHECK(reader.current_page() == ed.section_marks[1].first_page);

  CHECK(reader.toggle_sections());
  CHECK(reader.toggle_sections());
  CHECK(reader.mode() == ReaderMode::Browse);
  CHECK_FALSE(reader.jump_to_section(99));
}

TEST_CASE("page turns are partial; changing context is a full refresh") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = make_edition(*fonts);
  const StoryRef& target = ed.stories[0];

  Rig rig;
  ReaderPolicy policy;
  policy.partial_turns_before_full = 100;  // isolate context changes
  Reader reader(ed, *fonts, rig.hal(), policy);

  reader.render();  // first paint is a full refresh
  CHECK(rig.display.count(RefreshMode::Full) == 1);

  while (reader.current_page() < target.lede_page) {
    reader.next_page();
    reader.render();
  }
  const size_t fulls_before = rig.display.count(RefreshMode::Full);
  CHECK(rig.display.count(RefreshMode::Partial) >= 1);

  reader.open_story_at(target.lede_bounds.x + 10, target.lede_bounds.y + 10);
  reader.render();
  CHECK(rig.display.count(RefreshMode::Full) == fulls_before + 1);
}

TEST_CASE("ghosting is cleaned up after enough partial turns") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = make_edition(*fonts);

  Rig rig;
  ReaderPolicy policy;
  policy.partial_turns_before_full = 2;
  Reader reader(ed, *fonts, rig.hal(), policy);
  reader.render();

  int turns = 0;
  for (int i = 0; i < 20; ++i) {
    if (!reader.next_page()) break;
    reader.render();
    ++turns;
  }
  REQUIRE(turns >= 3);
  // With a threshold of 2, three turns must have triggered at least one clean
  // beyond the initial paint. That is the entire point of the policy.
  CHECK(rig.display.count(RefreshMode::Full) >= 2);
  CHECK(rig.display.count(RefreshMode::Partial) >= 2);
  CHECK(static_cast<int>(rig.display.modes.size()) == turns + 1);
}

TEST_CASE("the reader describes where it is") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = make_edition(*fonts);
  Rig rig;
  Reader reader(ed, *fonts, rig.hal());

  CHECK(reader.position().find("browsing page 1/") != std::string::npos);
  reader.toggle_sections();
  CHECK(reader.position() == "sections");
}
