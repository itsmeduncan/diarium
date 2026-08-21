// Gesture recognition and reader navigation, driven through the same HAL the
// device will implement — with a fake display that records refresh modes
// instead of driving a panel.
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/edition/edition.h"
#include "core/edition/edition_stream.h"
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

  // A whole-string sink is enough for a fake: nothing here is trying to
  // prove the chunking, only to let a Reader that calls open_write compile
  // and behave. See RangedSource / StreamingEditionReader for the real
  // lazy-loading proof.
  class Sink final : public ByteSink {
   public:
    Sink(std::map<std::string, std::string>* files, std::string path)
        : files_(files), path_(std::move(path)) {}
    ~Sink() override { (*files_)[path_] = data_; }
    bool write(const void* data, size_t n) override {
      data_.append(static_cast<const char*>(data), n);
      return true;
    }
    size_t position() const override { return data_.size(); }
    bool ok() const override { return true; }

   private:
    std::map<std::string, std::string>* files_;
    std::string path_;
    std::string data_;
  };

  std::unique_ptr<ByteSink> open_write(const std::string& path) override {
    return std::unique_ptr<ByteSink>(new Sink(&files, path));
  }
  size_t size(const std::string& path) override {
    auto it = files.find(path);
    return it == files.end() ? 0 : it->second.size();
  }
  bool read_range(const std::string& path, size_t offset, size_t length,
                  std::string* out) override {
    auto it = files.find(path);
    if (it == files.end()) return false;
    const std::string& data = it->second;
    if (offset > data.size() || length > data.size() - offset) return false;
    *out = data.substr(offset, length);
    return true;
  }

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

// A ready-to-swipe reader: a fresh edition, a fresh rig, read state loaded
// and nothing else. Heap-allocated and deliberately never freed — the rig and
// edition have to outlive the returned Reader, which only holds references
// into them, and the test binary exits long before that would matter.
Reader fresh_reader(const FontPack& fonts) {
  Rig* rig = new Rig();
  Edition* ed = new Edition(make_edition(fonts));
  Reader reader(*ed, fonts, rig->hal());
  reader.load_read_state("read.dat");
  return reader;
}

GestureEvent swipe(Gesture kind) {
  GestureEvent e;
  e.kind = kind;
  return e;
}

// The bottom-left corner, where a long press means "take me home" — see
// `Reader::in_home_corner`.
GestureEvent long_press_home_corner() {
  GestureEvent e;
  e.kind = Gesture::LongPress;
  e.x = 40;
  e.y = page_height() - 40;
  return e;
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

// Home is the entry state, and the way there and back.
TEST_CASE("the reader wakes on the home page") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  Reader reader = fresh_reader(*fonts);
  CHECK(reader.mode() == ReaderMode::Home);
}

TEST_CASE("a right swipe from home opens the oldest unread story") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  Reader reader = fresh_reader(*fonts);
  REQUIRE(reader.handle(swipe(Gesture::SwipeRight)));
  CHECK(reader.mode() == ReaderMode::Article);
}

TEST_CASE("the home gesture returns to the home page mid-pass") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  Reader reader = fresh_reader(*fonts);
  reader.handle(swipe(Gesture::SwipeRight));            // into Article
  REQUIRE(reader.handle(long_press_home_corner()));     // long-press bottom-left
  CHECK(reader.mode() == ReaderMode::Home);
}

TEST_CASE("the reader describes where it is") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = make_edition(*fonts);
  Rig rig;
  Reader reader(ed, *fonts, rig.hal());
  reader.load_read_state("read.dat");

  CHECK(reader.position() == "home");

  REQUIRE(reader.handle(swipe(Gesture::SwipeRight)));
  CHECK(reader.position().find("article 1/") != std::string::npos);

  for (size_t i = 0; i < ed.stories.size() + 2; ++i) {
    reader.handle(swipe(Gesture::SwipeRight));
  }
  CHECK(reader.position() == "no more news");
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
    REQUIRE(r.mode() == ReaderMode::Home);

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

// choose_refresh(): a context change is always a full refresh, and enough
// partial turns without one force a full refresh anyway, to keep e-ink
// ghosting from building up. Re-homed on the surviving continuous-pass
// gestures after the Browse-mode page-turning tests that used to cover this
// were deleted along with next_page().
TEST_CASE("reading: the refresh policy") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = make_edition(*fonts);

  SUBCASE("entering an article is a full refresh") {
    Rig rig;
    ReaderPolicy policy;
    policy.partial_turns_before_full = 100;  // isolate context changes
    Reader reader(ed, *fonts, rig.hal(), policy);
    reader.load_read_state("read.dat");

    reader.render();  // first paint is a full refresh
    CHECK(rig.display.count(RefreshMode::Full) == 1);

    // Home -> Article is a context change.
    REQUIRE(reader.handle(swipe(Gesture::SwipeRight)));
    reader.render();
    CHECK(rig.display.count(RefreshMode::Full) == 2);

    // Article -> next Article is a context change too.
    REQUIRE(reader.handle(swipe(Gesture::SwipeRight)));
    reader.render();
    CHECK(rig.display.count(RefreshMode::Full) == 3);
  }

  SUBCASE("ghosting is cleaned up after enough partial scrolls") {
    // A dedicated long article rather than `ed`: the shared fixture's
    // stories top out at 3 pages, too short to exercise a threshold of 2
    // more than once. One long story is all this needs.
    Section tech{"Technology", {}};
    tech.items.push_back(story("A Long Read", 5, 40));
    ComposeOptions opts;
    opts.now = 1786864000;
    opts.max_age_days = 3650;
    const Edition long_ed = compose_edition({tech}, *fonts, opts);
    REQUIRE_FALSE(long_ed.stories.empty());
    REQUIRE(long_ed.stories[0].page_count >= 4);

    Rig rig;
    ReaderPolicy policy;
    policy.partial_turns_before_full = 2;
    Reader reader(long_ed, *fonts, rig.hal(), policy);
    reader.load_read_state("read.dat");

    REQUIRE(reader.handle(swipe(Gesture::SwipeRight)));
    reader.render();  // the full refresh from entering this article
    const size_t fulls_before = rig.display.count(RefreshMode::Full);

    int turns = 0;
    for (int i = 0; i < 20; ++i) {
      if (!reader.handle(swipe(Gesture::SwipeDown))) break;
      reader.render();
      ++turns;
    }
    REQUIRE(turns >= 3);
    // With a threshold of 2, three scrolls must have triggered at least one
    // clean beyond the initial paint. That is the entire point of the policy.
    CHECK(rig.display.count(RefreshMode::Partial) >= 2);
    CHECK(rig.display.count(RefreshMode::Full) > fulls_before);
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
    CHECK(r.mode() == ReaderMode::Home);
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

  SUBCASE("and from the end of the news") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");
    GestureEvent right;
    right.kind = Gesture::SwipeRight;
    for (size_t i = 0; i < ed.stories.size() + 2; ++i) r.handle(right);
    REQUIRE(r.mode() == ReaderMode::Finished);

    REQUIRE(r.handle(corner(Gesture::LongPress)));
    CHECK(r.mode() == ReaderMode::Home);
    CHECK(r.current_page() == 0);
  }

  SUBCASE("already home is not a page turn") {
    Rig rig;
    Reader r(ed, *fonts, rig.hal());
    r.load_read_state("read.dat");
    REQUIRE(r.mode() == ReaderMode::Home);
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
    REQUIRE(r.mode() == ReaderMode::Home);

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

// Clearing the whole backlog used to live in the deleted Sections overlay;
// it is reachable from home now, as the same two-tap.
TEST_CASE("reading: clearing the backlog from home") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  GestureEvent tap;
  tap.kind = Gesture::Tap;
  GestureEvent right;
  right.kind = Gesture::SwipeRight;

  SUBCASE("a tap on home arms the clear, but doesn't fire it") {
    Reader r = fresh_reader(*fonts);
    REQUIRE(r.handle(tap));
    CHECK(r.mode() == ReaderMode::Home);

    // Still armed rather than fired: reading picks up at the oldest article,
    // not the empty end of the pass a clear would have left behind.
    REQUIRE(r.handle(right));
    CHECK(r.mode() == ReaderMode::Article);
  }

  SUBCASE("a second tap fires it, and reading stays on home") {
    Reader r = fresh_reader(*fonts);
    REQUIRE(r.handle(tap));   // arms
    REQUIRE(r.handle(tap));   // fires
    CHECK(r.mode() == ReaderMode::Home);  // not forced to Finished

    // Nothing left unread: beginning the pass now runs straight off the end.
    REQUIRE(r.handle(right));
    CHECK(r.mode() == ReaderMode::Finished);
  }

  SUBCASE("a swipe cancels the arm instead of clearing") {
    Reader r = fresh_reader(*fonts);
    REQUIRE(r.handle(tap));  // arms
    REQUIRE(r.handle(right));  // cancels the arm and begins reading

    // The backlog is intact: this is the same oldest-first opening as an
    // ordinary swipe from home, not the empty pass a clear would leave.
    CHECK(r.mode() == ReaderMode::Article);
  }
}

namespace {

// A RangedSource over a borrowed string that counts bytes read, so a test
// can prove the reader never holds more than the current story's pages
// resident — edition_stream_test.cpp has this same double for the reader
// underneath.
class CountingRangedSource final : public RangedSource {
 public:
  explicit CountingRangedSource(const std::string& data) : data_(data) {}

  size_t size() const override { return data_.size(); }
  bool read(size_t offset, size_t length, std::string* out) const override {
    if (offset > data_.size() || length > data_.size() - offset) return false;
    *out = data_.substr(offset, length);
    total_read_ += length;
    return true;
  }

  size_t total_read() const { return total_read_; }

 private:
  const std::string& data_;
  mutable size_t total_read_ = 0;
};

}  // namespace

// Stage C: the reader over a lazily-opened v5 file, driven exactly like the
// whole-Edition reader above — same gestures, same navigation — but backed
// by a StreamingEditionReader instead of a resident Edition.
TEST_CASE("the streaming reader holds only the current story's pages") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  Section tech{"Technology", {}};
  Section world{"World", {}};
  for (int i = 0; i < 5; ++i) {
    tech.items.push_back(story("Tech " + std::to_string(i), 10 - i, 14));
    world.items.push_back(story("World " + std::to_string(i), 10 - i, 6));
  }

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;

  std::string blob;
  StringSink sink(&blob);
  ComposeStats stats;
  REQUIRE(compose_streaming({tech, world}, *fonts, opts, sink, &stats));
  REQUIRE(stats.items_published > 5);

  CountingRangedSource counting(blob);
  StreamingEditionReader stream;
  std::string error;
  REQUIRE_MESSAGE(stream.open(counting, &error), error);
  const size_t after_open = counting.total_read();
  CHECK(after_open < blob.size() / 2);  // header + index, not the pages

  Rig rig;
  Reader r(stream, *fonts, rig.hal());
  r.load_read_state("read.dat");
  r.render();
  CHECK(r.mode() == ReaderMode::Home);
  CHECK(counting.total_read() == after_open);  // home never touches a story

  GestureEvent right;
  right.kind = Gesture::SwipeRight;
  REQUIRE(r.handle(right));
  CHECK(r.mode() == ReaderMode::Article);
  const uint64_t first_key = r.open_story()->key;
  const size_t first_page_count = r.open_story()->page_count;
  const size_t after_first_story = counting.total_read();
  CHECK(after_first_story > after_open);  // exactly one story's worth loaded

  // Scrolling within the story never issues another read — current_pages_
  // already holds everything it needs.
  if (first_page_count > 1) {
    GestureEvent down;
    down.kind = Gesture::SwipeDown;
    REQUIRE(r.handle(down));
    CHECK(r.current_page() == 1);
    CHECK(counting.total_read() == after_first_story);
  }

  // Moving to the next story replaces current_pages_ with a second read,
  // not an accumulation on top of the first.
  REQUIRE(r.handle(right));
  CHECK(r.mode() == ReaderMode::Article);
  CHECK(r.open_story()->key != first_key);
  CHECK(counting.total_read() > after_first_story);

  // Two of many stories were ever resident, one at a time — nowhere near
  // the whole file.
  CHECK(counting.total_read() < blob.size() / 2);
}
