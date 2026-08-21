// The edition's shape: nothing but story text, walked oldest-first. These
// assertions are the product thesis in executable form.
#include <string>
#include <vector>

#include "core/edition/edition.h"
#include "core/text/font_pack.h"
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

Item story(const std::string& title, int day, size_t paragraphs) {
  Item it;
  it.title = title;
  it.author = "A Reporter";
  it.source_name = "The Source";
  it.link = "https://example.com/" + title;
  it.guid = title;
  it.published = 1786000000 + day * 86400;
  it.summary_text =
      "A summary of " + title +
      " that runs long enough to occupy a line or two beneath the headline.";
  for (size_t i = 0; i < paragraphs; ++i) {
    Block b;
    b.type = BlockType::Paragraph;
    b.text =
        "Body paragraph " + std::to_string(i) + " of " + title +
        ", written at enough length that it wraps across several lines of the "
        "measure and contributes real height to the page it lands on.";
    it.blocks.push_back(std::move(b));
    it.text_bytes += it.blocks.back().text.size();
  }
  return it;
}

std::vector<Section> two_sections() {
  Section tech{"Technology", {}};
  Section world{"World", {}};
  for (int i = 0; i < 6; ++i) {
    tech.items.push_back(story("Tech story " + std::to_string(i), 10 - i, 12));
    world.items.push_back(story("World story " + std::to_string(i), 10 - i, 8));
  }
  return {tech, world};
}

}  // namespace

TEST_CASE("an edition is nothing but story text now") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  const Edition ed = compose_edition(two_sections(), *fonts, opts);

  REQUIRE(!ed.stories.empty());
  size_t sum = 0;
  for (const StoryRef& s : ed.stories) {
    CHECK(s.first_page < ed.pages.size());
    CHECK(s.page_count >= 1);
    sum += s.page_count;
  }
  CHECK(sum == ed.pages.size());  // every page belongs to exactly one story

  // And it ends.
  CHECK(ed.pages.size() < 200);
}

TEST_CASE("every story has real pages of its own") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  const Edition ed = compose_edition(two_sections(), *fonts, opts);

  REQUIRE(ed.stories.size() == 12);
  for (const StoryRef& s : ed.stories) {
    CAPTURE(s.title);
    CHECK(s.page_count > 0);
    CHECK(s.first_page + s.page_count <= ed.pages.size());
    CHECK_FALSE(s.section.empty());
    CHECK_FALSE(s.source.empty());
    CHECK(s.key != 0);
    CHECK(s.published != kNoDate);
  }
}

TEST_CASE("stories run newest first within a section") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  Section s{"Technology", {}};
  s.items.push_back(story("older", 1, 4));
  s.items.push_back(story("newest", 9, 4));
  s.items.push_back(story("middle", 5, 4));

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  const Edition ed = compose_edition({s}, *fonts, opts);

  REQUIRE(ed.stories.size() == 3);
  CHECK(ed.stories[0].title == "newest");
  CHECK(ed.stories[1].title == "middle");
  CHECK(ed.stories[2].title == "older");
}

TEST_CASE("stale stories are dropped and counted") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  Section s{"Technology", {}};
  s.items.push_back(story("fresh", 10, 4));
  Item old = story("ancient", 0, 4);
  old.published = 1;  // 1970
  s.items.push_back(old);

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3;
  const Edition ed = compose_edition({s}, *fonts, opts);

  CHECK(ed.stats.dropped_stale == 1);
  CHECK(ed.stats.items_published == 1);
  REQUIRE(ed.stories.size() == 1);
  CHECK(ed.stories[0].title == "fresh");
}

TEST_CASE("an edition with no stories has no pages") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = compose_edition({}, *fonts, ComposeOptions());

  // Nothing but story text now, and there are no stories: the home dashboard
  // is what tells the reader the paper came up empty, not a composed page.
  CHECK(ed.stories.empty());
  CHECK(ed.pages.empty());
}

TEST_CASE("every section gets a share of the budget") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  // Three sections, the first far busier than the rest. Spending the budget
  // in section order would leave the last with nothing.
  Section busy{"Technology", {}};
  for (int i = 0; i < 20; ++i) {
    busy.items.push_back(story("Tech " + std::to_string(i), 10, 3));
  }
  Section middle{"World", {}};
  for (int i = 0; i < 6; ++i) {
    middle.items.push_back(story("World " + std::to_string(i), 10, 3));
  }
  Section last{"Miscellany", {}};
  for (int i = 0; i < 6; ++i) {
    last.items.push_back(story("Misc " + std::to_string(i), 10, 3));
  }

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  opts.max_items = 12;
  opts.min_per_section = 2;

  const Edition ed = compose_edition({busy, middle, last}, *fonts, opts);

  size_t tech = 0, world = 0, misc = 0;
  for (const StoryRef& s : ed.stories) {
    if (s.section == "Technology") ++tech;
    if (s.section == "World") ++world;
    if (s.section == "Miscellany") ++misc;
  }
  CHECK(ed.stats.items_published == 12);
  CHECK(tech + world + misc == 12);
  CHECK(tech >= 2);
  CHECK(world >= 2);
  CHECK(misc >= 2);  // the whole point: the back pages still exist
  CHECK(ed.stats.dropped_over_budget == 20 + 6 + 6 - 12);
}

TEST_CASE("a small section doesn't hoard budget it can't use") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  Section big{"Technology", {}};
  for (int i = 0; i < 10; ++i) {
    big.items.push_back(story("Tech " + std::to_string(i), 10, 3));
  }
  Section tiny{"Weather", {}};
  tiny.items.push_back(story("Only story", 10, 3));

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  opts.max_items = 8;
  opts.min_per_section = 3;  // more than Weather has

  const Edition ed = compose_edition({big, tiny}, *fonts, opts);
  CHECK(ed.stats.items_published == 8);

  size_t weather = 0;
  for (const StoryRef& s : ed.stories) {
    if (s.section == "Weather") ++weather;
  }
  CHECK(weather == 1);  // it gets what it has, not what the floor allows
}

TEST_CASE("the budget keeps the newest stories, not the first in the feed") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  // A feed that lists its oldest story first. Cutting to the budget before
  // sorting would keep exactly the wrong three.
  Section s{"Technology", {}};
  for (int day = 1; day <= 6; ++day) {
    s.items.push_back(story("Day " + std::to_string(day), day, 3));
  }

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  opts.max_items = 3;
  opts.min_per_section = 0;

  const Edition ed = compose_edition({s}, *fonts, opts);
  REQUIRE(ed.stories.size() == 3);
  CHECK(ed.stories[0].title == "Day 6");
  CHECK(ed.stories[1].title == "Day 5");
  CHECK(ed.stories[2].title == "Day 4");
}

TEST_CASE("no ceiling means every story is published") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  opts.max_items = 0;  // the default: no ceiling

  std::vector<Section> sections = two_sections();
  size_t offered = 0;
  for (const Section& s : sections) offered += s.items.size();

  const Edition ed = compose_edition(std::move(sections), *fonts, opts);
  CHECK(ed.stories.size() == offered);
  CHECK(ed.stats.items_published == offered);
  CHECK(ed.stats.dropped_over_budget == 0);
}

