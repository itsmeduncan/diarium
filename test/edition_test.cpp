// The edition's shape: nothing but story text, walked oldest-first. These
// assertions are the product thesis in executable form.
#include <string>
#include <vector>

#include "core/edition/edition.h"
#include "core/edition/edition_stream.h"
#include "core/io/byte_sink.h"
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

TEST_CASE("a page budget bounds the edition instead of letting it grow") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  ComposeOptions opts;
  const Edition full = compose_edition(two_sections(), *fonts, opts);
  REQUIRE(full.pages.size() > 10);  // the fixture is comfortably multi-page

  // The budget is a memory-safety stop, not a preference: the edition must not
  // grow past it, and the stories that could not fit are dropped and counted
  // rather than the paper growing until the device runs out of RAM.
  opts.max_pages = 8;
  const Edition capped = compose_edition(two_sections(), *fonts, opts);
  CHECK(capped.pages.size() < full.pages.size());
  // At most one story overshoots — the one being laid out when the budget was
  // reached. Fixture stories run a few pages each.
  CHECK(capped.pages.size() <= opts.max_pages + 12);
  CHECK(capped.stats.dropped_over_budget > 0);
  // The all-story-text invariant still holds: every page belongs to a story.
  size_t sum = 0;
  for (const StoryRef& s : capped.stories) sum += s.page_count;
  CHECK(sum == capped.pages.size());
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

// Stage B: the same select/paginate pipeline, but writing one story at a
// time to a sink instead of building a resident Edition.
TEST_CASE("compose_streaming writes the same stories compose_edition would, one at a time") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;

  const Edition whole = compose_edition(two_sections(), *fonts, opts);
  REQUIRE(!whole.stories.empty());

  std::string out;
  StringSink sink(&out);
  ComposeStats stream_stats;
  REQUIRE(compose_streaming(two_sections(), *fonts, opts, sink, &stream_stats));

  CHECK(stream_stats.items_published == whole.stats.items_published);
  CHECK(stream_stats.items_published == whole.stories.size());
  CHECK(stream_stats.dropped_stale == whole.stats.dropped_stale);
  CHECK(stream_stats.truncated_published == whole.stats.truncated_published);
  CHECK(stream_stats.dropped_over_budget == whole.stats.dropped_over_budget);

  StreamingEditionReader r;
  std::string error;
  REQUIRE_MESSAGE(r.open(out, &error), error);

  CHECK(r.date() == whole.date);
  CHECK(r.title() == whole.title);
  // The v5 file's own header, not just the out-param above: pins the header
  // to the real survivor count rather than the constant zero a prior bug
  // left it at.
  CHECK(r.stats().items_published == whole.stats.items_published);
  CHECK(r.stats().items_published == whole.stories.size());
  CHECK(r.stats().truncated_published == whole.stats.truncated_published);
  REQUIRE(r.index().size() == whole.stories.size());

  for (size_t i = 0; i < whole.stories.size(); ++i) {
    CAPTURE(i);
    CHECK(r.index()[i].ref.title == whole.stories[i].title);
    CHECK(r.index()[i].ref.section == whole.stories[i].section);
    CHECK(r.index()[i].ref.source == whole.stories[i].source);
    CHECK(r.index()[i].ref.page_count == whole.stories[i].page_count);
    CHECK(r.index()[i].ref.published == whole.stories[i].published);
  }

  // Same first story's page text — the point of the "one at a time" claim.
  const std::vector<Page> loaded = r.load_story_pages(0);
  const StoryRef& first = whole.stories[0];
  REQUIRE(loaded.size() == first.page_count);
  for (size_t p = 0; p < loaded.size(); ++p) {
    CAPTURE(p);
    const Page& want = whole.pages[first.first_page + p];
    REQUIRE(loaded[p].lines.size() == want.lines.size());
    for (size_t l = 0; l < want.lines.size(); ++l) {
      REQUIRE(loaded[p].lines[l].runs.size() == want.lines[l].runs.size());
      for (size_t k = 0; k < want.lines[l].runs.size(); ++k) {
        CHECK(loaded[p].lines[l].runs[k].text == want.lines[l].runs[k].text);
      }
    }
    CHECK(loaded[p].folio_left == want.folio_left);
    CHECK(loaded[p].folio_right == want.folio_right);
  }
}

TEST_CASE("compose_streaming's page budget still bounds the streamed count") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  ComposeOptions opts;
  opts.max_pages = 8;

  std::string out;
  StringSink sink(&out);
  ComposeStats stats;
  REQUIRE(compose_streaming(two_sections(), *fonts, opts, sink, &stats));
  CHECK(stats.dropped_over_budget > 0);

  StreamingEditionReader r;
  std::string error;
  REQUIRE_MESSAGE(r.open(out, &error), error);
  // The out-param is the authoritative, post-budget count: it must match
  // what actually landed in the index exactly.
  CHECK(r.index().size() == stats.items_published);
  // The file's own header is written before the budget is applied — before
  // any story (or its article) is fetched or laid out at all — so it can
  // only overstate, by at most the number of stories the backstop trims.
  // Nothing downstream reads it for an exact count; `*stats` is what a
  // caller uses for that.
  CHECK(r.stats().items_published >= stats.items_published);

  size_t total_pages = 0;
  for (const StreamIndexEntry& e : r.index()) total_pages += e.ref.page_count;
  CHECK(total_pages <= opts.max_pages + 12);
}

namespace {

// Stands in for device/compose.cpp's CardArticleSource: answers html_for
// for exactly one key (the truncated item under test), and counts calls so
// a test can check it was asked for the right story and no more than once.
class CountingArticleSource final : public ArticleSource {
 public:
  CountingArticleSource(uint64_t key, std::string html)
      : key_(key), html_(std::move(html)) {}

  std::string html_for(const Item& item) const override {
    ++calls_;
    return item.dedup_key() == key_ ? html_ : "";
  }

  size_t calls() const { return calls_; }

 private:
  uint64_t key_;
  std::string html_;
  mutable size_t calls_ = 0;
};

// A space after every run: PositionedRun::text doesn't carry the
// inter-word space the layout placed between runs, so joining without one
// would glue "fire" and "service" into one unfindable word.
std::string joined_text(const std::vector<Page>& pages) {
  std::string out;
  for (const Page& p : pages) {
    for (const Line& l : p.lines) {
      for (const PositionedRun& run : l.runs) out += run.text + " ";
    }
  }
  return out;
}

}  // namespace

TEST_CASE("compose_streaming fetches a truncated story's article lazily") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  Item truncated;
  truncated.title = "A story cut short";
  truncated.author = "A Reporter";
  truncated.source_name = "The Source";
  truncated.guid = "truncated-1";
  truncated.published = 1786864000 - 86400;
  truncated.truncation = TruncationReason::EllipsisTail;
  Block excerpt;
  excerpt.type = BlockType::Paragraph;
  excerpt.text = "Fires broke out across the county overnight, officials said...";
  truncated.blocks = {excerpt};

  Item whole_story;
  whole_story.title = "A story told in full";
  whole_story.author = "A Reporter";
  whole_story.source_name = "The Source";
  whole_story.guid = "whole-1";
  whole_story.published = 1786864000 - 172800;
  Block para;
  para.type = BlockType::Paragraph;
  para.text =
      "Body paragraph of a story that was never truncated, written at "
      "length so it occupies real space on whatever page it lands on.";
  whole_story.blocks = {para};

  const std::string article_html =
      "<html><body>"
      "<nav><a href=\"/\">Home</a> <a href=\"/news\">News</a></nav>"
      "<article>"
      "<p>The fire service said the scale of the incidents was without "
      "recent precedent, with crews drawn from three neighbouring counties "
      "working through the night to contain what had begun as separate "
      "fires.</p>"
      "<p>Investigators said the pattern of ignition, spread across several "
      "miles within a narrow window, pointed toward a common cause rather "
      "than coincidence, though no determination had been made by the time "
      "crews stood down at dawn.</p>"
      "</article>"
      "<footer>Copyright 2026</footer>"
      "</body></html>";
  CountingArticleSource articles(truncated.dedup_key(), article_html);

  ComposeOptions opts;
  opts.now = 1786864000;
  opts.max_age_days = 3650;
  // Hyphenation can split a word across a line break with a hyphen and no
  // space, which would make a plain substring search unreliable for
  // reasons that have nothing to do with what this test checks.
  opts.hyphenate = false;

  std::string out;
  StringSink sink(&out);
  ComposeStats stats;
  Section s{"Technology", {truncated, whole_story}};
  REQUIRE(compose_streaming({s}, *fonts, opts, sink, &stats, &articles));

  // Asked for exactly the one truncated, selected story — never the
  // untruncated one, and never more than once for it.
  CHECK(articles.calls() == 1);

  StreamingEditionReader r;
  std::string error;
  REQUIRE_MESSAGE(r.open(out, &error), error);
  REQUIRE(r.index().size() == 2);

  const size_t idx = r.index()[0].ref.key == truncated.dedup_key() ? 0 : 1;
  // No longer marked truncated: the fetched article replaced the excerpt,
  // exactly as compose_from_card's whole-Edition equivalent already did.
  CHECK_FALSE(r.index()[idx].ref.truncated);

  const std::string text = joined_text(r.load_story_pages(idx));
  CHECK(text.find("fire service") != std::string::npos);
  CHECK(text.find("neighbouring counties") != std::string::npos);
  CHECK(text.find("Fires broke out across the county overnight") ==
       std::string::npos);
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

