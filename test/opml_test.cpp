#include <string>
#include <vector>

#include "core/base/str.h"
#include "core/config/opml.h"
#include "core/io/byte_source.h"
#include "doctest.h"

using namespace rsspaper;

namespace {

FeedList import(const std::string& xml, OpmlReport* report = nullptr,
                OpmlOptions opts = OpmlOptions()) {
  MemoryByteSource src(xml);
  FeedList list;
  parse_opml(src, &list, opts, report);
  return list;
}

const FeedEntry* find(const FeedList& list, const std::string& url) {
  for (const FeedEntry& f : list.feeds) {
    if (f.url == url) return &f;
  }
  return nullptr;
}

}  // namespace

TEST_CASE("OPML: folders become sections") {
  const FeedList list = import(R"(<opml version="1.0"><body>
    <outline text="Technology">
      <outline type="rss" text="A" xmlUrl="https://a.example/feed"/>
      <outline type="rss" text="B" xmlUrl="https://b.example/feed"/>
    </outline>
    <outline text="World">
      <outline type="rss" text="C" xmlUrl="https://c.example/feed"/>
    </outline>
  </body></opml>)");

  REQUIRE(list.feeds.size() == 3);
  CHECK(find(list, "https://a.example/feed")->section == "Technology");
  CHECK(find(list, "https://b.example/feed")->section == "Technology");
  CHECK(find(list, "https://c.example/feed")->section == "World");

  const std::vector<std::string> order = list.section_order();
  REQUIRE(order.size() == 2);
  CHECK(order[0] == "Technology");
  CHECK(order[1] == "World");
}

TEST_CASE("OPML: nesting collapses to the outermost folder") {
  // Sub-folders are refinements, not sections. Taking the innermost would
  // shatter the paper into one-story sections.
  const FeedList list = import(R"(<opml><body>
    <outline text="Technology">
      <outline text="Blogs">
        <outline text="Daily">
          <outline type="rss" text="A" xmlUrl="https://a.example/feed"/>
        </outline>
      </outline>
    </outline>
  </body></opml>)");
  REQUIRE(list.feeds.size() == 1);
  CHECK(list.feeds[0].section == "Technology");
}

TEST_CASE("OPML: feeds outside any folder get the default section") {
  OpmlOptions opts;
  opts.default_section = "Miscellany";
  const FeedList list = import(R"(<opml><body>
    <outline type="rss" text="Loose" xmlUrl="https://a.example/feed"/>
  </body></opml>)", nullptr, opts);
  REQUIRE(list.feeds.size() == 1);
  CHECK(list.feeds[0].section == "Miscellany");
}

TEST_CASE("OPML: the same feed twice is imported once") {
  OpmlReport report;
  const FeedList list = import(R"(<opml><body>
    <outline text="A folder">
      <outline type="rss" text="One" xmlUrl="https://a.example/feed"/>
      <outline type="rss" text="One again" xmlUrl="https://a.example/feed"/>
    </outline>
  </body></opml>)", &report);
  CHECK(list.feeds.size() == 1);
  CHECK(report.duplicates_skipped == 1);
  CHECK(report.feeds_imported == 1);
}

TEST_CASE("OPML: outlines that are not feeds are skipped, not imported") {
  OpmlReport report;
  const FeedList list = import(R"(<opml><body>
    <outline text="An empty folder"/>
    <outline type="link" text="A bookmark" htmlUrl="https://example.com/"/>
    <outline type="rss" text="Real" xmlUrl="https://a.example/feed"/>
  </body></opml>)", &report);
  REQUIRE(list.feeds.size() == 1);
  CHECK(list.feeds[0].url == "https://a.example/feed");
  CHECK(report.entries_without_url == 2);
}

TEST_CASE("OPML: the url attribute is matched case-insensitively") {
  for (const char* attr : {"xmlUrl", "xmlurl", "XMLURL"}) {
    CAPTURE(attr);
    const std::string xml = std::string("<opml><body><outline ") + attr +
                            "=\"https://a.example/feed\"/></body></opml>";
    CHECK(import(xml).feeds.size() == 1);
  }
}

TEST_CASE("OPML: the display name comes from text, then title") {
  const FeedList list = import(R"(<opml><body>
    <outline title="From title">
      <outline type="rss" xmlUrl="https://a.example/feed"/>
    </outline>
  </body></opml>)");
  REQUIRE(list.feeds.size() == 1);
  CHECK(list.feeds[0].section == "From title");
}

TEST_CASE("OPML: a generic head title is not adopted as the masthead") {
  OpmlReport report;
  const FeedList generic = import(R"(<opml>
    <head><title>Subscriptions</title></head>
    <body><outline type="rss" xmlUrl="https://a.example/feed"/></body>
  </opml>)", &report);
  CHECK(report.title == "Subscriptions");
  CHECK(generic.edition.title == "RSSpaper");  // not "Subscriptions"

  const FeedList named = import(R"(<opml>
    <head><title>Duncan's morning paper</title></head>
    <body><outline type="rss" xmlUrl="https://a.example/feed"/></body>
  </opml>)");
  CHECK(named.edition.title == "Duncan's morning paper");
}

TEST_CASE("OPML: malformed input yields what it can rather than failing") {
  // Unclosed outlines, a stray close, junk between attributes.
  const FeedList list = import(R"(<opml><body>
    <outline text="Tech">
      <outline type="rss" xmlUrl="https://a.example/feed">
      <outline type="rss" xmlUrl="https://b.example/feed"/>
    </outline>
    </outline>
    <outline type="rss" ??? xmlUrl="https://c.example/feed"/>
  </body></opml>)");
  CHECK(list.feeds.size() == 3);
}

TEST_CASE("OPML: an empty or non-OPML document imports nothing") {
  MemoryByteSource empty("");
  FeedList list;
  CHECK_FALSE(parse_opml(empty, &list));
  CHECK(list.feeds.empty());

  MemoryByteSource html("<html><body>not opml</body></html>");
  CHECK_FALSE(parse_opml(html, &list));
  CHECK(list.feeds.empty());
}

TEST_CASE("OPML: several files merge into one list") {
  MemoryByteSource a(
      R"(<opml><body><outline text="Tech"><outline type="rss"
         xmlUrl="https://a.example/feed"/></outline></body></opml>)");
  MemoryByteSource b(
      R"(<opml><body><outline text="World"><outline type="rss"
         xmlUrl="https://b.example/feed"/></outline></body></opml>)");
  FeedList list;
  CHECK(parse_opml(a, &list));
  CHECK(parse_opml(b, &list));
  CHECK(list.feeds.size() == 2);
  CHECK(list.section_order().size() == 2);
}

TEST_CASE("an imported list round-trips through feeds.toml") {
  const FeedList imported = import(R"(<opml><body>
    <outline text="Technology">
      <outline type="rss" xmlUrl="https://a.example/feed"/>
      <outline type="rss" xmlUrl="https://b.example/feed"/>
    </outline>
    <outline text="World">
      <outline type="rss" xmlUrl="https://c.example/feed"/>
    </outline>
  </body></opml>)");

  const std::string toml = to_feeds_toml(imported);
  FeedList reparsed;
  std::string error;
  REQUIRE_MESSAGE(parse_feeds_toml(toml, &reparsed, &error), error);

  REQUIRE(reparsed.feeds.size() == imported.feeds.size());
  for (size_t i = 0; i < imported.feeds.size(); ++i) {
    CAPTURE(i);
    CHECK(reparsed.feeds[i].url == imported.feeds[i].url);
    CHECK(reparsed.feeds[i].section == imported.feeds[i].section);
    CHECK(reparsed.feeds[i].max_items == imported.feeds[i].max_items);
  }
  CHECK(reparsed.edition.title == imported.edition.title);
  CHECK(reparsed.edition.max_items == imported.edition.max_items);
  CHECK(reparsed.section_order() == imported.section_order());
}

TEST_CASE("quotes and backslashes survive the round trip") {
  FeedList list;
  list.edition.title = "The \"Daily\" \\ Paper";
  FeedEntry f;
  f.url = "https://example.com/feed?q=\"x\"";
  f.section = "Odd \"Section\"";
  list.feeds.push_back(f);

  FeedList reparsed;
  std::string error;
  REQUIRE(parse_feeds_toml(to_feeds_toml(list), &reparsed, &error));
  REQUIRE(reparsed.feeds.size() == 1);
  CHECK(reparsed.feeds[0].section == "Odd \"Section\"");
}

TEST_CASE("the shipped OPML fixture imports the way it reads") {
  FeedList list;
  OpmlReport report;
  REQUIRE(import_opml_file("test/fixtures/opml/subscriptions.opml", &list,
                           OpmlOptions(), &report));

  CHECK(report.feeds_imported == 5);
  CHECK(report.duplicates_skipped == 1);

  const std::vector<std::string> order = list.section_order();
  REQUIRE(order.size() == 3);
  CHECK(order[0] == "Technology");
  CHECK(order[1] == "World news");
  CHECK(order[2] == "News");  // xkcd, which sits outside any folder

  // The feed nested two folders deep still lands in Technology.
  const FeedEntry* nested = find(list, "https://simonwillison.net/atom/everything/");
  REQUIRE(nested != nullptr);
  CHECK(nested->section == "Technology");
}

TEST_CASE("placeholder folders are not sections") {
  // Feedly files loose subscriptions under a literal "Uncategorized" folder.
  // A newspaper section called Uncategorized is not a section.
  for (const char* folder : {"Uncategorized", "uncategorised", "Unsorted",
                             "Imported", "(no folder)"}) {
    CAPTURE(folder);
    const std::string xml = std::string("<opml><body><outline text=\"") +
                            folder +
                            "\"><outline type=\"rss\" "
                            "xmlUrl=\"https://a.example/feed\"/></outline>"
                            "</body></opml>";
    OpmlOptions opts;
    opts.default_section = "Miscellany";
    const FeedList list = import(xml, nullptr, opts);
    REQUIRE(list.feeds.size() == 1);
    CHECK(list.feeds[0].section == "Miscellany");
  }
}

TEST_CASE("a real folder that merely sounds vague is still a section") {
  const FeedList list = import(R"(<opml><body>
    <outline text="Miscellany">
      <outline type="rss" xmlUrl="https://a.example/feed"/>
    </outline>
  </body></opml>)");
  REQUIRE(list.feeds.size() == 1);
  CHECK(list.feeds[0].section == "Miscellany");
}
