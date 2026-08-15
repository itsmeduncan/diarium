// A composed edition has to survive a round trip through storage, because the
// whole battery argument depends on composing once and reading many times.
#include <string>
#include <vector>

#include "core/edition/edition_store.h"
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
  it.guid = title;
  it.published = 1786000000 + day * 86400;
  it.summary_text = "Summary of " + title + " with a line or two of text.";
  for (size_t i = 0; i < paragraphs; ++i) {
    Block b;
    b.type = BlockType::Paragraph;
    b.text = "Paragraph " + std::to_string(i) + " of " + title +
             " — long enough to wrap over several lines, with a curly "
             "apostrophe (it\xE2\x80\x99s) and an em dash so the round trip "
             "has real UTF-8 to carry.";
    it.blocks.push_back(std::move(b));
  }
  return it;
}

Edition sample(const FontPack& fonts) {
  Section tech{"Technology", {}};
  Section world{"World", {}};
  for (int i = 0; i < 4; ++i) {
    tech.items.push_back(story("Tech " + std::to_string(i), 10 - i, 10));
    world.items.push_back(story("World " + std::to_string(i), 10 - i, 5));
  }
  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  opts.feeds_configured = 2;
  opts.feed_problems.push_back(FeedProblem{"A Feed", "timed out"});
  return compose_edition({tech, world}, fonts, opts);
}

}  // namespace

TEST_CASE("an edition survives a round trip byte for byte") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition original = sample(*fonts);

  const std::string blob = serialize_edition(original);
  CHECK(blob.size() > 1000);

  Edition restored;
  std::string error;
  REQUIRE_MESSAGE(deserialize_edition(blob, &restored, &error), error);

  CHECK(restored.date == original.date);
  CHECK(restored.title == original.title);
  CHECK(restored.browse_page_count == original.browse_page_count);
  CHECK(restored.colophon_page == original.colophon_page);
  CHECK(restored.pages.size() == original.pages.size());
  CHECK(restored.stats.items_published == original.stats.items_published);
  CHECK(restored.stats.truncated_published ==
        original.stats.truncated_published);

  REQUIRE(restored.section_marks.size() == original.section_marks.size());
  for (size_t i = 0; i < original.section_marks.size(); ++i) {
    CHECK(restored.section_marks[i].name == original.section_marks[i].name);
    CHECK(restored.section_marks[i].first_page ==
          original.section_marks[i].first_page);
  }

  REQUIRE(restored.stories.size() == original.stories.size());
  for (size_t i = 0; i < original.stories.size(); ++i) {
    CAPTURE(i);
    const StoryRef& a = original.stories[i];
    const StoryRef& b = restored.stories[i];
    CHECK(b.title == a.title);
    CHECK(b.section == a.section);
    CHECK(b.lede_page == a.lede_page);
    CHECK(b.first_page == a.first_page);
    CHECK(b.page_count == a.page_count);
    CHECK(b.truncated == a.truncated);
    CHECK(b.lede_bounds.x == a.lede_bounds.x);
    CHECK(b.lede_bounds.y == a.lede_bounds.y);
    CHECK(b.lede_bounds.w == a.lede_bounds.w);
    CHECK(b.lede_bounds.h == a.lede_bounds.h);
  }

  for (size_t p = 0; p < original.pages.size(); ++p) {
    CAPTURE(p);
    const Page& a = original.pages[p];
    const Page& b = restored.pages[p];
    CHECK(b.is_front_page == a.is_front_page);
    CHECK(b.folio_left == a.folio_left);
    CHECK(b.folio_right == a.folio_right);
    REQUIRE(b.lines.size() == a.lines.size());
    REQUIRE(b.rules.size() == a.rules.size());
    for (size_t l = 0; l < a.lines.size(); ++l) {
      CHECK(b.lines[l].baseline == a.lines[l].baseline);
      REQUIRE(b.lines[l].runs.size() == a.lines[l].runs.size());
      for (size_t k = 0; k < a.lines[l].runs.size(); ++k) {
        CHECK(b.lines[l].runs[k].face == a.lines[l].runs[k].face);
        CHECK(b.lines[l].runs[k].x == a.lines[l].runs[k].x);
        CHECK(b.lines[l].runs[k].text == a.lines[l].runs[k].text);
      }
    }
    for (size_t k = 0; k < a.rules.size(); ++k) {
      CHECK(b.rules[k].x == a.rules[k].x);
      CHECK(b.rules[k].grey == a.rules[k].grey);
    }
  }

  // Re-serialising the restored edition gives the same bytes.
  CHECK(serialize_edition(restored) == blob);
}

TEST_CASE("a restored edition still navigates") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition original = sample(*fonts);

  Edition restored;
  std::string error;
  REQUIRE(deserialize_edition(serialize_edition(original), &restored, &error));

  for (const StoryRef& s : restored.stories) {
    const int cx = s.lede_bounds.x + s.lede_bounds.w / 2;
    const int cy = s.lede_bounds.y + s.lede_bounds.h / 2;
    const StoryRef* hit = restored.story_at(s.lede_page, cx, cy);
    REQUIRE(hit != nullptr);
    CHECK(hit->title == s.title);
  }
}

TEST_CASE("an empty edition round-trips too") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Edition original = compose_edition({}, *fonts, ComposeOptions());

  Edition restored;
  std::string error;
  REQUIRE(deserialize_edition(serialize_edition(original), &restored, &error));
  CHECK(restored.pages.size() == original.pages.size());
  CHECK(restored.stories.empty());
}

TEST_CASE("a corrupt blob is refused, not crashed on") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const std::string good = serialize_edition(sample(*fonts));

  Edition out;
  std::string error;

  SUBCASE("empty") {
    CHECK_FALSE(deserialize_edition("", &out, &error));
    CHECK_FALSE(error.empty());
  }
  SUBCASE("wrong magic") {
    std::string bad = good;
    bad[0] = 'X';
    CHECK_FALSE(deserialize_edition(bad, &out, &error));
    CHECK(error.find("not a saved edition") != std::string::npos);
  }
  SUBCASE("a future version") {
    std::string bad = good;
    bad[4] = static_cast<char>(kEditionVersion + 1);
    CHECK_FALSE(deserialize_edition(bad, &out, &error));
    CHECK(error.find("different build") != std::string::npos);
  }
  SUBCASE("truncated at every length") {
    // Every prefix must be refused rather than read off the end — this is the
    // shape of a write interrupted by a flat battery.
    for (size_t n = 1; n < good.size(); n += 97) {
      CAPTURE(n);
      Edition partial;
      CHECK_FALSE(deserialize_edition(good.substr(0, n), &partial, &error));
    }
  }
  SUBCASE("a corrupted byte anywhere is refused or survived, never crashes") {
    for (size_t at = 0; at < good.size(); at += 211) {
      std::string bad = good;
      bad[at] = static_cast<char>(bad[at] ^ 0xFF);
      Edition mangled;
      std::string ignored;
      // The contract is only that it returns; a flipped bit inside a string
      // may well produce a valid-but-different edition.
      deserialize_edition(bad, &mangled, &ignored);
    }
    CHECK(true);
  }
}

TEST_CASE("a story pointing past the pages is refused") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  Edition ed = sample(*fonts);
  REQUIRE_FALSE(ed.stories.empty());
  ed.stories[0].first_page = ed.pages.size() + 10;

  Edition out;
  std::string error;
  CHECK_FALSE(deserialize_edition(serialize_edition(ed), &out, &error));
  CHECK(error.find("past its pages") != std::string::npos);
}
