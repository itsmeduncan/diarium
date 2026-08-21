// The corpus test: every fixture is a real feed, and the invariants asserted
// here are the ones that must hold for *any* feed, not just these twelve. If
// a new feed breaks the parser, it belongs in test/fixtures/feeds/ and its
// breakage belongs in the table in that directory's README.
#include <string>
#include <vector>

#include "core/feed/feed_parser.h"
#include "core/io/file_byte_source.h"
#include "doctest.h"

using namespace diarium;

namespace {

struct Fixture {
  const char* file;
  FeedFormat format;
  const char* feed_title;
};

const Fixture kFixtures[] = {
    {"arstechnica.rss.xml", FeedFormat::Rss, "Ars Technica - All content"},
    {"astralcodexten.rss.xml", FeedFormat::Rss, "Astral Codex Ten"},
    {"bbc-news.rss.xml", FeedFormat::Rss, "BBC News"},
    {"craigmod.rss.xml", FeedFormat::Rss, nullptr},
    {"daringfireball.atom.xml", FeedFormat::Atom, "Daring Fireball"},
    {"guardian-world.rss.xml", FeedFormat::Rss, "World news | The Guardian"},
    {"hackernews.rss.xml", FeedFormat::Rss, "Hacker News"},
    {"kottke.rss.xml", FeedFormat::Atom, "kottke.org"},
    {"nasa.rss.xml", FeedFormat::Rss, "NASA"},
    {"simonwillison.atom.xml", FeedFormat::Atom, "Simon Willison's Weblog"},
    {"theverge.atom.xml", FeedFormat::Atom, "The Verge"},
    {"xkcd.rss.xml", FeedFormat::Rss, "xkcd.com"},
};

class Collect final : public ItemSink {
 public:
  bool on_item(Item&& item) override {
    items.push_back(std::move(item));
    return true;
  }
  std::vector<Item> items;
};

std::string path_for(const char* file) {
  return std::string("test/fixtures/feeds/") + file;
}

}  // namespace

TEST_CASE("every fixture parses into usable stories") {
  for (const Fixture& f : kFixtures) {
    CAPTURE(f.file);
    FileByteSource src(path_for(f.file));
    REQUIRE_MESSAGE(src.ok(),
                    "fixture missing — run tests from the repository root");

    Collect sink;
    FeedParseOptions opts;
    opts.max_items = 6;
    const FeedParseStats stats = parse_feed(src, sink, opts);

    CHECK(stats.format == f.format);
    if (f.feed_title != nullptr) CHECK(stats.feed_title == f.feed_title);
    CHECK(sink.items.size() >= 1);

    for (const Item& it : sink.items) {
      CAPTURE(it.title);
      // A story without a title is not renderable as a headline.
      CHECK_FALSE(it.title.empty());
      // Titles are single-line: newlines break the headline setter.
      CHECK(it.title.find('\n') == std::string::npos);
      // No unresolved entity references survived into display text.
      CHECK(it.title.find("&#") == std::string::npos);
      CHECK(it.title.find("&amp;") == std::string::npos);
      // No markup leaked past the block converter.
      CHECK(it.title.find('<') == std::string::npos);
      for (const Block& b : it.blocks) {
        CHECK(b.text.find("<p>") == std::string::npos);
        CHECK(b.text.find("&amp;") == std::string::npos);
      }
    }
  }
}

TEST_CASE("style runs are in range and non-overlapping for every fixture") {
  for (const Fixture& f : kFixtures) {
    CAPTURE(f.file);
    FileByteSource src(path_for(f.file));
    REQUIRE(src.ok());
    Collect sink;
    FeedParseOptions opts;
    opts.max_items = 6;
    parse_feed(src, sink, opts);

    for (const Item& it : sink.items) {
      for (const Block& b : it.blocks) {
        uint32_t prev_end = 0;
        for (const StyleRun& r : b.runs) {
          CHECK(r.start >= prev_end);
          CHECK(static_cast<size_t>(r.start) + r.length <= b.text.size());
          prev_end = r.start + r.length;
        }
      }
    }
  }
}

TEST_CASE("dates parse for the overwhelming majority of real items") {
  size_t total = 0, dated = 0;
  for (const Fixture& f : kFixtures) {
    FileByteSource src(path_for(f.file));
    REQUIRE(src.ok());
    Collect sink;
    FeedParseOptions opts;
    opts.max_items = 6;
    parse_feed(src, sink, opts);
    for (const Item& it : sink.items) {
      ++total;
      if (it.published != kNoDate) ++dated;
    }
  }
  CHECK(total > 50);
  CHECK(dated == total);
}

TEST_CASE("a big feed is parsed without buffering it") {
  // 772 KB of Substack. Taking three items must not read the whole file.
  FileByteSource src(path_for("astralcodexten.rss.xml"));
  REQUIRE(src.ok());
  Collect sink;
  FeedParseOptions opts;
  opts.max_items = 3;
  const FeedParseStats stats = parse_feed(src, sink, opts);

  CHECK(sink.items.size() == 3);
  CHECK(stats.stopped_early);
  CHECK(stats.bytes_consumed < 400 * 1024);
  // And the articles really are long — this is the streaming case, not a
  // feed that happens to be small.
  CHECK(sink.items[0].text_bytes > 10000);
}

TEST_CASE("per-item content limits hold on the largest articles") {
  HtmlLimits limits;
  limits.max_total_bytes = 20000;
  limits.max_blocks = 40;

  FileByteSource src(path_for("astralcodexten.rss.xml"));
  REQUIRE(src.ok());
  Collect sink;
  FeedParseOptions opts;
  opts.max_items = 2;
  opts.html_limits = limits;
  parse_feed(src, sink, opts);

  for (const Item& it : sink.items) {
    CHECK(it.blocks.size() <= 40);
    CHECK(it.text_bytes <= 20000 + 8192);  // total cap plus one block's slack
  }
}
