// Clippings: the list itself, and the reader gestures that fill it.
#include <string>
#include <vector>

#include "core/edition/clippings.h"
#include "core/edition/edition.h"
#include "core/text/font_pack.h"
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

Clipping make(uint64_t key, const std::string& title) {
  Clipping c;
  c.key = key;
  c.title = title;
  c.section = "Technology";
  c.source = "The Source";
  c.saved = 1786864000;
  c.published = 1786800000;
  return c;
}

// --- a HAL whose storage is a map, so persistence is testable -------------

class MemStorage final : public IStorage {
 public:
  bool read(const std::string& path, std::string* out) override {
    for (const auto& kv : files) {
      if (kv.first == path) {
        *out = kv.second;
        return true;
      }
    }
    return false;
  }
  bool write(const std::string& path, const std::string& data) override {
    for (auto& kv : files) {
      if (kv.first == path) {
        kv.second = data;
        return true;
      }
    }
    files.push_back({path, data});
    return true;
  }
  bool exists(const std::string& path) override {
    std::string ignored;
    return read(path, &ignored);
  }
  bool remove(const std::string&) override { return true; }

  std::vector<std::pair<std::string, std::string>> files;
};

class NullDisplay final : public IDisplay {
 public:
  int width() const override { return fb_.width(); }
  int height() const override { return fb_.height(); }
  Framebuffer& framebuffer() override { return fb_; }
  void flush(RefreshMode) override {}
  void set_frontlight(int) override {}
  int frontlight() const override { return 0; }

 private:
  Framebuffer fb_;
};
class NullInput final : public IInput {
 public:
  size_t poll(TouchPoint*, size_t) override { return 0; }
  uint32_t millis() const override { return 0; }
};
class FixedClock final : public IClock {
 public:
  Epoch now() const override { return 1786864000; }
  void set_wake_alarm(Epoch) override {}
};
class NullPower final : public IPower {
 public:
  void deep_sleep_until(Epoch) override {}
  int battery_millivolts() const override { return 4000; }
};
class NullHttp final : public IHttpClient {
 public:
  std::unique_ptr<ByteSource> get(const HttpRequest&, HttpResponse*) override {
    return nullptr;
  }
};

struct Rig {
  NullDisplay display;
  NullInput input;
  FixedClock clock;
  NullPower power;
  MemStorage storage;
  NullHttp http;
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

Item story(const std::string& title, int day) {
  Item it;
  it.title = title;
  it.guid = title;
  it.source_name = "The Source";
  it.published = 1786000000 + day * 86400;
  it.summary_text = "Summary of " + title + ", a line or so long.";
  for (int i = 0; i < 4; ++i) {
    Block b;
    b.type = BlockType::Paragraph;
    b.text = "Paragraph " + std::to_string(i) + " of " + title +
             " with enough words to wrap across a few lines of the measure.";
    it.blocks.push_back(std::move(b));
  }
  return it;
}

Edition sample(const FontPack& fonts) {
  Section tech{"Technology", {}};
  for (int i = 0; i < 5; ++i) {
    tech.items.push_back(story("Story " + std::to_string(i), 10 - i));
  }
  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  return compose_edition({tech}, fonts, opts);
}

}  // namespace

TEST_CASE("the list holds each story once, newest first") {
  Clippings c;
  CHECK(c.add(make(1, "One")));
  CHECK(c.add(make(2, "Two")));
  CHECK_FALSE(c.add(make(1, "One again")));  // already clipped
  REQUIRE(c.size() == 2);
  CHECK(c.all()[0].title == "Two");  // most recent first
  CHECK(c.has(1));
  CHECK_FALSE(c.has(99));
}

TEST_CASE("toggle saves then unsaves") {
  Clippings c;
  CHECK(c.toggle(make(1, "One")));
  CHECK(c.has(1));
  CHECK_FALSE(c.toggle(make(1, "One")));
  CHECK_FALSE(c.has(1));
  CHECK(c.empty());
}

TEST_CASE("removing something that isn't there is not an error") {
  Clippings c;
  CHECK_FALSE(c.remove(42));
  c.add(make(1, "One"));
  CHECK(c.remove(1));
  CHECK(c.empty());
}

TEST_CASE("the oldest clipping falls off the end") {
  // A folder that only grows is a slow leak on a device with no eviction.
  Clippings c(3);
  for (uint64_t i = 1; i <= 6; ++i) c.add(make(i, "S" + std::to_string(i)));
  REQUIRE(c.size() == 3);
  CHECK(c.all()[0].key == 6);
  CHECK(c.all()[2].key == 4);
  CHECK_FALSE(c.has(1));
}

TEST_CASE("clippings round-trip through storage") {
  Clippings c;
  c.add(make(1, "First"));
  c.add(make(2, "Second — with an em dash and a curly ’"));

  Clippings restored;
  std::string error;
  REQUIRE_MESSAGE(
      deserialize_clippings(serialize_clippings(c), &restored, &error), error);
  REQUIRE(restored.size() == 2);
  CHECK(restored.all()[0].title == c.all()[0].title);
  CHECK(restored.all()[1].title == c.all()[1].title);
  CHECK(restored.all()[0].key == c.all()[0].key);
  CHECK(restored.all()[0].saved == c.all()[0].saved);
  CHECK(restored.all()[0].source == c.all()[0].source);
}

TEST_CASE("a corrupt clippings file is refused, not crashed on") {
  Clippings c;
  c.add(make(1, "First"));
  const std::string good = serialize_clippings(c);

  Clippings out;
  std::string error;
  CHECK_FALSE(deserialize_clippings("", &out, &error));
  std::string bad = good;
  bad[0] = 'X';
  CHECK_FALSE(deserialize_clippings(bad, &out, &error));
  for (size_t n = 1; n < good.size(); ++n) {
    Clippings partial;
    std::string ignored;
    CHECK_FALSE(deserialize_clippings(good.substr(0, n), &partial, &ignored));
  }
}

TEST_CASE("holding a lede clips its story, and holding again releases it") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = sample(*fonts);
  REQUIRE_FALSE(ed.stories.empty());

  Rig rig;
  Reader reader(ed, *fonts, rig.hal());
  reader.load_clippings("clippings.dat");
  CHECK(reader.clippings().empty());

  const StoryRef& s = ed.stories[0];
  while (reader.current_page() < s.lede_page) REQUIRE(reader.next_page());
  const int cx = s.lede_bounds.x + s.lede_bounds.w / 2;
  const int cy = s.lede_bounds.y + s.lede_bounds.h / 2;

  CHECK(reader.toggle_clipping_at(cx, cy));
  REQUIRE(reader.clippings().size() == 1);
  CHECK(reader.clippings().all()[0].title == s.title);
  CHECK(reader.clippings().all()[0].section == s.section);

  CHECK_FALSE(reader.toggle_clipping_at(cx, cy));
  CHECK(reader.clippings().empty());
}

TEST_CASE("holding empty space clips nothing") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = sample(*fonts);
  Rig rig;
  Reader reader(ed, *fonts, rig.hal());
  reader.load_clippings("clippings.dat");
  CHECK_FALSE(reader.toggle_clipping_at(5, kPageHeight - 5));
  CHECK(reader.clippings().empty());
}

TEST_CASE("clippings survive the reader that made them") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = sample(*fonts);
  const StoryRef& s = ed.stories[0];

  Rig rig;
  {
    Reader reader(ed, *fonts, rig.hal());
    reader.load_clippings("clippings.dat");
    while (reader.current_page() < s.lede_page) reader.next_page();
    REQUIRE(reader.toggle_clipping_at(s.lede_bounds.x + 10,
                                      s.lede_bounds.y + 10));
  }
  // A new reader over the same storage — tomorrow's edition, in effect.
  Reader fresh(ed, *fonts, rig.hal());
  fresh.load_clippings("clippings.dat");
  REQUIRE(fresh.clippings().size() == 1);
  CHECK(fresh.clippings().all()[0].title == s.title);
}

TEST_CASE("holding inside a story clips the story you are reading") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = sample(*fonts);
  const StoryRef& s = ed.stories[0];

  Rig rig;
  Reader reader(ed, *fonts, rig.hal());
  reader.load_clippings("clippings.dat");
  while (reader.current_page() < s.lede_page) reader.next_page();
  REQUIRE(reader.open_story_at(s.lede_bounds.x + 10, s.lede_bounds.y + 10));
  REQUIRE(reader.mode() == ReaderMode::Story);

  CHECK(reader.toggle_clipping_at(kPageWidth / 2, kPageHeight / 2));
  REQUIRE(reader.clippings().size() == 1);
  CHECK(reader.clippings().all()[0].title == s.title);
}

TEST_CASE("the clippings view opens, closes and blocks page turns") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = sample(*fonts);
  Rig rig;
  Reader reader(ed, *fonts, rig.hal());
  reader.load_clippings("clippings.dat");

  CHECK(reader.toggle_clippings_view());
  CHECK(reader.mode() == ReaderMode::Clippings);
  CHECK_FALSE(reader.next_page());
  CHECK_FALSE(reader.previous_page());
  CHECK(reader.position().find("clippings") != std::string::npos);

  CHECK(reader.back());
  CHECK(reader.mode() == ReaderMode::Browse);
}
