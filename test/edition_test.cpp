// The edition's shape: a short sequence you flip through, with full stories
// behind it. These assertions are the product thesis in executable form.
#include <string>
#include <vector>

#include "core/edition/edition.h"
#include "core/text/font_pack.h"
#include "doctest.h"

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

TEST_CASE("the edition you flip through is short; the stories sit behind it") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  const Edition ed = compose_edition(two_sections(), *fonts, opts);

  REQUIRE(ed.browse_page_count > 0);
  CHECK(ed.stats.items_published == 12);

  // The whole point: browsing the paper is a dozen page turns, not two
  // hundred, even though every story is present in full.
  CHECK(ed.browse_page_count <= 8);
  CHECK(ed.pages.size() > ed.browse_page_count);

  // And it ends.
  CHECK(ed.pages.size() < 200);
}

TEST_CASE("every story is reachable from a lede") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  const Edition ed = compose_edition(two_sections(), *fonts, opts);

  REQUIRE(ed.stories.size() == 12);
  for (const StoryRef& s : ed.stories) {
    CAPTURE(s.title);
    // The lede is on a page you can actually flip to.
    CHECK(s.lede_page < ed.browse_page_count);
    // It has somewhere to be tapped.
    CHECK_FALSE(s.lede_bounds.empty());
    CHECK(s.lede_bounds.y >= 0);
    CHECK(s.lede_bounds.y + s.lede_bounds.h <= kPageHeight);
    // And the story it opens is real, and lives outside the browse sequence.
    CHECK(s.page_count > 0);
    CHECK(s.first_page >= ed.browse_page_count);
    CHECK(s.first_page + s.page_count <= ed.pages.size());
    CHECK_FALSE(s.section.empty());
  }
}

TEST_CASE("selecting a point on a lede finds its story") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  const Edition ed = compose_edition(two_sections(), *fonts, opts);
  REQUIRE_FALSE(ed.stories.empty());

  for (const StoryRef& s : ed.stories) {
    CAPTURE(s.title);
    const int cx = s.lede_bounds.x + s.lede_bounds.w / 2;
    const int cy = s.lede_bounds.y + s.lede_bounds.h / 2;
    const StoryRef* hit = ed.story_at(s.lede_page, cx, cy);
    REQUIRE(hit != nullptr);
    CHECK(hit->title == s.title);
  }

  // A tap in the folio hits nothing.
  CHECK(ed.story_at(0, 60, kPageHeight - 10) == nullptr);
}

TEST_CASE("lede tap regions on a page do not overlap") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  const Edition ed = compose_edition(two_sections(), *fonts, opts);

  for (size_t i = 0; i < ed.stories.size(); ++i) {
    for (size_t j = i + 1; j < ed.stories.size(); ++j) {
      const StoryRef& a = ed.stories[i];
      const StoryRef& b = ed.stories[j];
      if (a.lede_page != b.lede_page) continue;
      const bool disjoint = a.lede_bounds.y + a.lede_bounds.h <= b.lede_bounds.y ||
                            b.lede_bounds.y + b.lede_bounds.h <= a.lede_bounds.y ||
                            a.lede_bounds.x + a.lede_bounds.w <= b.lede_bounds.x ||
                            b.lede_bounds.x + b.lede_bounds.w <= a.lede_bounds.x;
      CAPTURE(a.title);
      CAPTURE(b.title);
      CHECK(disjoint);
    }
  }
}

TEST_CASE("sections keep their configured order and are marked") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  const Edition ed = compose_edition(two_sections(), *fonts, opts);

  REQUIRE(ed.section_marks.size() == 2);
  CHECK(ed.section_marks[0].name == "Technology");
  CHECK(ed.section_marks[1].name == "World");
  CHECK(ed.section_marks[0].first_page < ed.section_marks[1].first_page);
  CHECK(ed.section_marks[1].first_page < ed.browse_page_count);
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

TEST_CASE("an edition with no stories is one page that explains itself") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition ed = compose_edition({}, *fonts, ComposeOptions());

  // Not zero pages: a device that woke up, found nothing, and showed a blank
  // screen is indistinguishable from a device that is broken.
  CHECK(ed.stories.empty());
  CHECK(ed.browse_page_count == 1);
  CHECK(ed.colophon_page == 0);

  std::string text;
  for (const Line& line : ed.pages[0].lines) {
    for (const PositionedRun& r : line.runs) text += r.text + " ";
  }
  CHECK(text.find("0 stories") != std::string::npos);
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

TEST_CASE("the paper ends with a colophon that says so") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  opts.feeds_configured = 4;
  const Edition ed = compose_edition(two_sections(), *fonts, opts);

  // It is the last page you can flip to, and it belongs to no section.
  REQUIRE(ed.browse_page_count > 0);
  CHECK(ed.colophon_page == ed.browse_page_count - 1);
  CHECK(ed.pages[ed.colophon_page].folio_left == ed.title);
  for (const Edition::SectionMark& m : ed.section_marks) {
    CHECK(m.first_page < ed.colophon_page);
  }

  // No story's lede lives on it — it is an ending, not an index page.
  for (const StoryRef& s : ed.stories) {
    CHECK(s.lede_page != ed.colophon_page);
  }
}

TEST_CASE("a feed that failed is named on the colophon") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  opts.feeds_configured = 4;
  opts.feed_problems.push_back(FeedProblem{"The Missing Times", "timed out"});
  opts.feed_problems.push_back(FeedProblem{"Nothing Daily", "404"});

  const Edition ed = compose_edition(two_sections(), *fonts, opts);

  // Gather the text of the colophon page.
  std::string text;
  for (const Line& line : ed.pages[ed.colophon_page].lines) {
    for (const PositionedRun& r : line.runs) text += r.text + " ";
  }
  CHECK(text.find("The Missing Times") != std::string::npos);
  CHECK(text.find("timed out") != std::string::npos);
  CHECK(text.find("Nothing Daily") != std::string::npos);
  CHECK(text.find("2 of 4 feeds") != std::string::npos);
}

TEST_CASE("a clean run does not invent problems to report") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  opts.max_items = 100;  // nothing held over
  opts.feeds_configured = 2;
  const Edition ed = compose_edition(two_sections(), *fonts, opts);

  std::string text;
  for (const Line& line : ed.pages[ed.colophon_page].lines) {
    for (const PositionedRun& r : line.runs) text += r.text + " ";
  }
  CHECK(text.find("didn't answer") == std::string::npos);
  CHECK(text.find("held over") == std::string::npos);
  CHECK(text.find("2 of 2 feeds") != std::string::npos);
}
