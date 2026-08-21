// Gesture recognition and reader navigation, driven through the same HAL the
// device will implement — with a fake display that records refresh modes
// instead of driving a panel.
#include <map>
#include <string>
#include <vector>

#include "core/edition/edition.h"
#include "core/text/font_pack.h"
#include "core/ui/gesture.h"
#include "core/ui/reader.h"
#include "doctest.h"
#include "hal/hal.h"

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
  int battery_millivolts() const override { return mv; }
  int mv = 4000;
};

// A real store rather than a black hole: the read state is only worth
// anything if it survives being written and read back.
class FakeStorage final : public IStorage {
 public:
  bool read(const std::string& path, std::string* out) override {
    auto it = files.find(path);
    if (it == files.end()) return false;
    *out = it->second;
    return true;
  }
  bool write(const std::string& path, const std::string& data) override {
    files[path] = data;
    return true;
  }
  bool exists(const std::string& path) override {
    return files.count(path) != 0;
  }
  bool remove(const std::string& path) override { return files.erase(path) != 0; }

  std::map<std::string, std::string> files;
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
  // The last browse page is the colophon, which says the paper has ended.
  // There is nothing past it — and crucially not page one of the story text,
  // which is what a naive "page_ + 1" would walk into.
  CHECK(reader.current_page() == ed.colophon_page);
  CHECK_FALSE(reader.next_page());
  CHECK(reader.current_page() == ed.colophon_page);
  CHECK(reader.mode() == ReaderMode::Browse);

  CHECK(reader.previous_page());
  CHECK(reader.current_page() == ed.colophon_page - 1);
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
  CHECK_FALSE(reader.open_story_at(5, page_height() - 5));
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

// The continuous pass: an overview you land on, then every unread article
// oldest-first, one swipe at a time, until the news runs out.
TEST_CASE("reading: the continuous oldest-first pass") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;  // no font pack built; nothing to lay out
  Edition ed = make_edition(*fonts);
  REQUIRE_FALSE(ed.stories.empty());

  SUBCASE("reading order runs oldest first") {
    const std::vector<size_t> order = ed.reading_order();
    REQUIRE(order.size() == ed.stories.size());
    for (size_t i = 1; i < order.size(); ++i) {
      const Epoch a = ed.stories[order[i - 1]].published;
      const Epoch b = ed.stories[order[i]].published;
      if (a != kNoDate && b != kNoDate) CHECK(a <= b);
    }
  }

  SUBCASE("swiping right from the overview enters the oldest article") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");
    r.render();
    REQUIRE(r.mode() == ReaderMode::Browse);

    GestureEvent e;
    e.kind = Gesture::SwipeRight;
    REQUIRE(r.handle(e));
    CHECK(r.mode() == ReaderMode::Article);

    const std::vector<size_t> order = ed.reading_order();
    CHECK(r.open_story() != nullptr);
    CHECK(r.open_story()->key == ed.stories[order[0]].key);
  }

  SUBCASE("swiping right walks onward and never revisits") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");
    GestureEvent right;
    right.kind = Gesture::SwipeRight;
    REQUIRE(r.handle(right));

    std::vector<uint64_t> seen;
    for (int i = 0; i < 40 && r.mode() == ReaderMode::Article; ++i) {
      seen.push_back(r.open_story()->key);
      r.handle(right);
    }
    CHECK(r.mode() == ReaderMode::Finished);

    for (size_t i = 0; i < seen.size(); ++i) {
      for (size_t j = i + 1; j < seen.size(); ++j) CHECK(seen[i] != seen[j]);
    }
  }

  SUBCASE("swiping down keeps reading and swiping up goes back") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");
    GestureEvent right;
    right.kind = Gesture::SwipeRight;
    REQUIRE(r.handle(right));

    // Find an article long enough to have somewhere to scroll to.
    while (r.mode() == ReaderMode::Article && r.open_story()->page_count < 2) {
      r.handle(right);
    }
    if (r.mode() != ReaderMode::Article) return;

    const size_t top = r.current_page();
    GestureEvent down;
    down.kind = Gesture::SwipeDown;
    CHECK(r.handle(down));
    CHECK(r.current_page() == top + 1);

    GestureEvent up;
    up.kind = Gesture::SwipeUp;
    CHECK(r.handle(up));
    CHECK(r.current_page() == top);
    CHECK_FALSE(r.handle(up));  // already at the top of the article
  }

  SUBCASE("scrolling stops at the end of the article rather than running on") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");
    GestureEvent right;
    right.kind = Gesture::SwipeRight;
    REQUIRE(r.handle(right));

    const size_t pages = r.open_story()->page_count;
    GestureEvent down;
    down.kind = Gesture::SwipeDown;
    for (size_t i = 0; i + 1 < pages; ++i) CHECK(r.handle(down));
    CHECK_FALSE(r.handle(down));
    CHECK(r.mode() == ReaderMode::Article);  // scrolling never advances
  }

  SUBCASE("what has been read is remembered across a session") {
    Rig rig;
    GestureEvent right;
    right.kind = Gesture::SwipeRight;

    uint64_t first_key = 0;
    {
      Reader r(ed, *fonts, rig.hal());
      r.load_read_state("read.dat");
      REQUIRE(r.handle(right));
      first_key = r.open_story()->key;
      REQUIRE(r.handle(right));  // move on, marking the first read
    }

    Reader again(ed, *fonts, rig.hal());
    again.load_read_state("read.dat");
    REQUIRE(again.handle(right));
    CHECK(again.open_story()->key != first_key);
  }

  SUBCASE("the pass ends rather than looping when everything is read") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");
    GestureEvent right;
    right.kind = Gesture::SwipeRight;
    for (int i = 0; i < 60; ++i) r.handle(right);
    CHECK(r.mode() == ReaderMode::Finished);

    Reader fresh(ed, *fonts, rig.hal());
    fresh.load_read_state("read.dat");
    fresh.handle(right);
    CHECK(fresh.mode() == ReaderMode::Finished);
  }
}

// A discreet mark when the battery is genuinely low, and nothing at all when
// it is fine. No percentage, because lithium discharge is flat through most
// of its range and any percentage would be a lie exactly where it matters.
TEST_CASE("reading: the battery mark") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  Edition ed = make_edition(*fonts);

  // Counts ink in the middle of the bottom margin, where the mark lives. The
  // folio's own text is at the margins, so this strip is empty without it.
  auto corner_ink = [](const Framebuffer& fb) {
    size_t n = 0;
    for (int y = fb.height() - 34; y < fb.height() - 14; ++y) {
      for (int x = fb.width() / 2 - 20; x < fb.width() / 2 + 20; ++x) {
        if (fb.get(x, y) != kPaper) ++n;
      }
    }
    return n;
  };

  SUBCASE("a healthy cell is not mentioned") {
    Rig rig;
    rig.power.mv = 4100;
    Reader r(ed, *fonts, rig.hal());
    r.render();
    CHECK(corner_ink(rig.display.framebuffer()) == 0);
  }

  SUBCASE("a low cell gets a mark") {
    Rig rig;
    rig.power.mv = 3400;
    Reader r(ed, *fonts, rig.hal());
    r.render();
    CHECK(corner_ink(rig.display.framebuffer()) > 0);
  }

  SUBCASE("no measurement is not an empty battery") {
    Rig rig;
    rig.power.mv = 0;
    Reader r(ed, *fonts, rig.hal());
    r.render();
    CHECK(corner_ink(rig.display.framebuffer()) == 0);
  }

  SUBCASE("the mark appears on every kind of page, not just the ledes") {
    Rig rig;
    rig.power.mv = 3400;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");
    GestureEvent right;
    right.kind = Gesture::SwipeRight;
    REQUIRE(r.handle(right));
    r.render();
    CHECK(r.mode() == ReaderMode::Article);
    CHECK(corner_ink(rig.display.framebuffer()) > 0);
  }
}

// The way out of a backlog. Two taps, because a carry-over pile with no exit
// is an inbox and an exit that fires on a mis-tap is worse than one.
TEST_CASE("reading: marking everything read") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  Edition ed = make_edition(*fonts);

  auto tap_row = [&](Reader& r, size_t row) {
    GestureEvent e;
    e.kind = Gesture::Tap;
    e.x = page_width() / 2;
    e.y = kOverlayFirstY + static_cast<int>(row) * kOverlayRowHeight + 4;
    return r.handle(e);
  };

  SUBCASE("one tap arms it, a second clears the backlog") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");

    GestureEvent down;
    down.kind = Gesture::SwipeDown;
    REQUIRE(r.handle(down));
    REQUIRE(r.mode() == ReaderMode::Sections);

    const size_t row = ed.section_marks.size();
    REQUIRE(tap_row(r, row));
    CHECK(r.mode() == ReaderMode::Sections);  // armed, not fired

    REQUIRE(tap_row(r, row));
    CHECK(r.mode() == ReaderMode::Finished);
  }

  SUBCASE("everything really is read afterwards") {
    Rig rig;
    {
      Reader r(ed, *fonts, rig.hal());
      r.load_read_state("read.dat");
      GestureEvent down;
      down.kind = Gesture::SwipeDown;
      REQUIRE(r.handle(down));
      const size_t row = ed.section_marks.size();
      REQUIRE(tap_row(r, row));
      REQUIRE(tap_row(r, row));
    }

    Reader fresh(ed, *fonts, rig.hal());
    fresh.load_read_state("read.dat");
    GestureEvent right;
    right.kind = Gesture::SwipeRight;
    fresh.handle(right);
    CHECK(fresh.mode() == ReaderMode::Finished);
  }

  SUBCASE("tapping a section still jumps rather than arming anything") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");
    GestureEvent down;
    down.kind = Gesture::SwipeDown;
    REQUIRE(r.handle(down));
    if (ed.section_marks.empty()) return;
    REQUIRE(tap_row(r, 0));
    CHECK(r.mode() == ReaderMode::Browse);
  }
}

// A light with a switch. Nothing reacts to the room, nothing ramps, and the
// level survives being put down.
TEST_CASE("reading: the frontlight") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  Edition ed = make_edition(*fonts);

  auto corner = [](Gesture kind) {
    GestureEvent e;
    e.kind = kind;
    e.x = page_width() - 40;
    e.y = 40;
    return e;
  };

  SUBCASE("a corner tap switches it on and off again") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_frontlight("light.dat");
    CHECK(rig.display.frontlight() == 0);

    r.handle(corner(Gesture::Tap));
    CHECK(rig.display.frontlight() > 0);

    r.handle(corner(Gesture::Tap));
    CHECK(rig.display.frontlight() == 0);
  }

  SUBCASE("a long press in the corner steps the brightness") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_frontlight("light.dat");
    r.handle(corner(Gesture::LongPress));
    const int first = rig.display.frontlight();
    CHECK(first > 0);
    r.handle(corner(Gesture::LongPress));
    CHECK(rig.display.frontlight() > first);
  }

  SUBCASE("stepping past the top comes back round to off") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_frontlight("light.dat");
    bool saw_zero_again = false;
    for (int i = 0; i < 12; ++i) {
      r.handle(corner(Gesture::LongPress));
      if (i > 0 && rig.display.frontlight() == 0) saw_zero_again = true;
    }
    CHECK(saw_zero_again);
  }

  SUBCASE("the level survives the reader being rebuilt") {
    Rig rig;
    {
      Reader r(ed, *fonts, rig.hal());
      r.load_frontlight("light.dat");
      r.handle(corner(Gesture::Tap));
    }
    const int was = rig.display.frontlight();
    REQUIRE(was > 0);

    rig.display.set_frontlight(0);
    Reader again(ed, *fonts, rig.hal());
    again.load_frontlight("light.dat");
    CHECK(rig.display.frontlight() == was);
  }

  SUBCASE("a tap away from the corner is not the light") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_frontlight("light.dat");
    GestureEvent e;
    e.kind = Gesture::Tap;
    e.x = page_width() / 2;
    e.y = page_height() / 2;
    r.handle(e);
    CHECK(rig.display.frontlight() == 0);
  }
}

// The way out. Reading is a line rather than a tree, so there is nothing to
// go "back" to mid-pass — but there has to be a way to put the paper down and
// see what is left.
TEST_CASE("reading: the way home") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  Edition ed = make_edition(*fonts);

  auto corner = [](Gesture kind) {
    GestureEvent e;
    e.kind = kind;
    e.x = 40;
    e.y = page_height() - 40;
    return e;
  };
  auto middle = [](Gesture kind) {
    GestureEvent e;
    e.kind = kind;
    e.x = page_width() / 2;
    e.y = page_height() / 2;
    return e;
  };

  SUBCASE("a long press in the bottom corner leaves an article for the contents") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");

    GestureEvent right;
    right.kind = Gesture::SwipeRight;
    REQUIRE(r.handle(right));  // into the pass
    REQUIRE(r.handle(right));  // and onward, so this is not page one already
    REQUIRE(r.mode() == ReaderMode::Article);

    REQUIRE(r.handle(corner(Gesture::LongPress)));
    CHECK(r.mode() == ReaderMode::Browse);
    CHECK(r.current_page() == 0);
  }

  SUBCASE("a tap in the corner is not the way home") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");
    GestureEvent right;
    right.kind = Gesture::SwipeRight;
    REQUIRE(r.handle(right));
    REQUIRE(r.mode() == ReaderMode::Article);

    r.handle(corner(Gesture::Tap));
    CHECK(r.mode() == ReaderMode::Article);
  }

  SUBCASE("a long press elsewhere on the page is not the way home") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");
    GestureEvent right;
    right.kind = Gesture::SwipeRight;
    REQUIRE(r.handle(right));
    REQUIRE(r.mode() == ReaderMode::Article);

    r.handle(middle(Gesture::LongPress));
    CHECK(r.mode() == ReaderMode::Article);
  }

  SUBCASE("it comes back from a lede page too") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");

    GestureEvent left;
    left.kind = Gesture::SwipeLeft;
    REQUIRE(r.handle(left));
    REQUIRE(r.current_page() == 1);

    REQUIRE(r.handle(corner(Gesture::LongPress)));
    CHECK(r.mode() == ReaderMode::Browse);
    CHECK(r.current_page() == 0);
  }

  SUBCASE("and from the end of the news") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");
    GestureEvent right;
    right.kind = Gesture::SwipeRight;
    for (size_t i = 0; i < ed.stories.size() + 2; ++i) r.handle(right);
    REQUIRE(r.mode() == ReaderMode::Finished);

    REQUIRE(r.handle(corner(Gesture::LongPress)));
    CHECK(r.mode() == ReaderMode::Browse);
    CHECK(r.current_page() == 0);
  }

  SUBCASE("already home is not a page turn") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");
    REQUIRE(r.mode() == ReaderMode::Browse);
    REQUIRE(r.current_page() == 0);

    CHECK_FALSE(r.handle(corner(Gesture::LongPress)));
  }

  SUBCASE("going home does not lose your place in the pass") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");

    GestureEvent right;
    right.kind = Gesture::SwipeRight;
    REQUIRE(r.handle(right));
    REQUIRE(r.handle(right));
    const StoryRef* third = nullptr;
    REQUIRE(r.handle(right));
    third = r.open_story();
    REQUIRE(third != nullptr);
    const uint64_t was = third->key;

    REQUIRE(r.handle(corner(Gesture::LongPress)));
    REQUIRE(r.mode() == ReaderMode::Browse);

    // Resuming picks up the oldest thing still unread, which is the one after
    // the article that was on screen — it was marked read on arrival.
    REQUIRE(r.handle(right));
    REQUIRE(r.mode() == ReaderMode::Article);
    const StoryRef* next = r.open_story();
    REQUIRE(next != nullptr);
    CHECK(next->key != was);
  }

  SUBCASE("the light is still the other corner") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");
    r.load_frontlight("light.dat");

    GestureEvent right;
    right.kind = Gesture::SwipeRight;
    REQUIRE(r.handle(right));
    REQUIRE(r.mode() == ReaderMode::Article);

    GestureEvent light;
    light.kind = Gesture::Tap;
    light.x = page_width() - 40;
    light.y = 40;
    r.handle(light);
    CHECK(rig.display.frontlight() > 0);
    CHECK(r.mode() == ReaderMode::Article);
  }
}
