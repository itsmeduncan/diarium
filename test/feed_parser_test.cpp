#include <string>
#include <vector>

#include "core/feed/feed_parser.h"
#include "core/io/byte_source.h"
#include "doctest.h"

using namespace rsspaper;

namespace {

class Collect final : public ItemSink {
 public:
  bool on_item(Item&& item) override {
    items.push_back(std::move(item));
    return true;
  }
  std::vector<Item> items;
};

struct Parsed {
  FeedParseStats stats;
  std::vector<Item> items;
};

Parsed run(const std::string& xml,
           FeedParseOptions opts = FeedParseOptions()) {
  MemoryByteSource src(xml);
  Collect sink;
  Parsed p;
  p.stats = parse_feed(src, sink, opts);
  p.items = std::move(sink.items);
  return p;
}

std::string body_of(const Item& it) {
  std::string out;
  for (const Block& b : it.blocks) {
    if (!out.empty()) out += " ";
    out += b.text;
  }
  return out;
}

}  // namespace

TEST_CASE("RSS 2.0") {
  const Parsed p = run(R"(<?xml version="1.0"?>
<rss version="2.0">
 <channel>
  <title>Example Feed</title>
  <item>
   <title>First post</title>
   <link>http://example.com/1</link>
   <guid>tag:example,1</guid>
   <pubDate>Tue, 12 Aug 2025 09:31:00 -0400</pubDate>
   <description>&lt;p&gt;Hello &lt;b&gt;world&lt;/b&gt;.&lt;/p&gt;</description>
  </item>
 </channel>
</rss>)");

  CHECK(p.stats.format == FeedFormat::Rss);
  CHECK(p.stats.feed_title == "Example Feed");
  REQUIRE(p.items.size() == 1);
  CHECK(p.items[0].title == "First post");
  CHECK(p.items[0].link == "http://example.com/1");
  CHECK(p.items[0].guid == "tag:example,1");
  CHECK(p.items[0].published != kNoDate);
  CHECK(body_of(p.items[0]) == "Hello world.");
}

TEST_CASE("Atom, including link href and author/name") {
  const Parsed p = run(R"(<?xml version="1.0"?>
<feed xmlns="http://www.w3.org/2005/Atom">
 <title>Atom Feed</title>
 <entry>
  <title>An entry</title>
  <link rel="self" href="http://example.com/self"/>
  <link rel="alternate" type="text/html" href="http://example.com/story"/>
  <id>urn:uuid:1</id>
  <published>2025-08-12T09:31:00Z</published>
  <author><name>Ada Lovelace</name></author>
  <content type="html">&lt;p&gt;Body text.&lt;/p&gt;</content>
 </entry>
</feed>)");

  CHECK(p.stats.format == FeedFormat::Atom);
  REQUIRE(p.items.size() == 1);
  CHECK(p.items[0].title == "An entry");
  CHECK(p.items[0].link == "http://example.com/story");  // not rel="self"
  CHECK(p.items[0].author == "Ada Lovelace");
  CHECK(body_of(p.items[0]) == "Body text.");
}

TEST_CASE("content:encoded wins over description, whatever the order") {
  const char* kBefore = R"(<rss><channel><item>
   <description>short summary</description>
   <content:encoded><![CDATA[<p>the full article</p>]]></content:encoded>
  </item></channel></rss>)";
  const char* kAfter = R"(<rss><channel><item>
   <content:encoded><![CDATA[<p>the full article</p>]]></content:encoded>
   <description>short summary</description>
  </item></channel></rss>)";

  for (const char* xml : {kBefore, kAfter}) {
    const Parsed p = run(xml);
    REQUIRE(p.items.size() == 1);
    CHECK(body_of(p.items[0]) == "the full article");
    CHECK(p.items[0].content_source == ContentSource::FullContent);
    // The summary is still kept, for the front-page standfirst.
    CHECK(p.items[0].summary_text == "short summary");
  }
}

TEST_CASE("namespaced lookalikes are not article content") {
  const Parsed p = run(R"(<rss><channel><item>
   <title>T</title>
   <media:content url="http://x/v.mp4"/>
   <media:description>a video caption</media:description>
   <itunes:summary>podcast blurb</itunes:summary>
   <description>the real summary</description>
  </item></channel></rss>)");
  REQUIRE(p.items.size() == 1);
  CHECK(body_of(p.items[0]) == "the real summary");
}

TEST_CASE("dc:creator supplies a byline") {
  const Parsed p = run(R"(<rss><channel><item>
   <title>T</title><dc:creator>Grace Hopper</dc:creator>
  </item></channel></rss>)");
  REQUIRE(p.items.size() == 1);
  CHECK(p.items[0].author == "Grace Hopper");
}

TEST_CASE("an RSS author address yields the human name, or nothing") {
  const Parsed a = run(R"(<rss><channel><item><title>T</title>
   <author>ed@example.com (Ed Bloggs)</author></item></channel></rss>)");
  CHECK(a.items[0].author == "Ed Bloggs");

  const Parsed b = run(R"(<rss><channel><item><title>T</title>
   <author>noreply@example.com</author></item></channel></rss>)");
  CHECK(b.items[0].author.empty());
}

TEST_CASE("a title that is html inside CDATA is decoded once more") {
  const Parsed p = run(R"(<feed><entry>
   <title type="html"><![CDATA[Don&#8217;t <em>miss</em> this]]></title>
  </entry></feed>)");
  REQUIRE(p.items.size() == 1);
  CHECK(p.items[0].title == "Don’t miss this");
}

TEST_CASE("max_items stops the parse early") {
  std::string xml = "<rss><channel>";
  for (int i = 0; i < 100; ++i) {
    xml += "<item><title>t" + std::to_string(i) +
           "</title><description>body</description></item>";
  }
  xml += "</channel></rss>";

  FeedParseOptions opts;
  opts.max_items = 3;
  const Parsed p = run(xml, opts);
  CHECK(p.items.size() == 3);
  CHECK(p.stats.stopped_early);
  // And it stopped reading, rather than parsing all 100 and discarding.
  CHECK(p.stats.bytes_consumed < xml.size() / 2);
}

TEST_CASE("truncation heuristics") {
  SUBCASE("summary-only") {
    const Parsed p = run(R"(<rss><channel><item><title>T</title>
     <link>http://x/1</link><description>a one line teaser</description>
    </item></channel></rss>)");
    CHECK(p.items[0].truncation == TruncationReason::SummaryOnly);
  }
  SUBCASE("a long multi-paragraph description is the whole article") {
    std::string desc;
    for (int i = 0; i < 6; ++i) {
      desc += "&lt;p&gt;" + std::string(400, 'a') + "&lt;/p&gt;";
    }
    const Parsed p = run("<rss><channel><item><title>T</title><link>http://x/1"
                         "</link><description>" + desc +
                         "</description></item></channel></rss>");
    CHECK(p.items[0].truncation == TruncationReason::None);
  }
  SUBCASE("an ellipsis tail") {
    const Parsed p = run(
        "<rss><channel><item><title>T</title><link>http://x/1</link>"
        "<content:encoded>&lt;p&gt;" + std::string(600, 'a') +
        "\xE2\x80\xA6&lt;/p&gt;</content:encoded></item></channel></rss>");
    CHECK(p.items[0].truncation == TruncationReason::EllipsisTail);
  }
  SUBCASE("a read-more link") {
    const Parsed p = run(
        "<rss><channel><item><title>T</title><link>http://x/1</link>"
        "<content:encoded>&lt;p&gt;" + std::string(600, 'a') +
        " Continue reading&lt;/p&gt;</content:encoded></item></channel></rss>");
    CHECK(p.items[0].truncation == TruncationReason::ReadMoreLink);
  }
  SUBCASE("a full article is not flagged") {
    const Parsed p = run(
        "<rss><channel><item><title>T</title><link>http://x/1</link>"
        "<content:encoded>&lt;p&gt;" + std::string(2000, 'a') +
        ".&lt;/p&gt;</content:encoded></item></channel></rss>");
    CHECK(p.items[0].truncation == TruncationReason::None);
  }
}

TEST_CASE("dedup keys prefer guid, then link, then title") {
  Item a, b;
  a.guid = "g1";
  a.link = "http://x/1";
  b.guid = "g1";
  b.link = "http://x/2-different";
  CHECK(a.dedup_key() == b.dedup_key());

  Item c, d;
  c.link = "http://x/1";
  d.link = "http://x/1";
  CHECK(c.dedup_key() == d.dedup_key());
  CHECK(c.dedup_key() != a.dedup_key());
}

TEST_CASE("a malformed feed still yields its stories") {
  const Parsed p = run(R"(<rss><channel>
   <item><title>One</title><description>a & b</description>
   <item><title>Two</title><description>c</description></item>
  </channel></rss>)");
  CHECK(p.items.size() >= 1);
  CHECK(p.items[0].title == "One");
  CHECK(p.stats.recovered_errors);
}

TEST_CASE("an item that closes only at EOF is still emitted") {
  const Parsed p = run(
      "<rss><channel><item><title>Last</title><description>x</description>");
  REQUIRE(p.items.size() == 1);
  CHECK(p.items[0].title == "Last");
}

TEST_CASE("an empty document produces no items and does not hang") {
  CHECK(run("").items.empty());
  CHECK(run("<rss></rss>").items.empty());
  CHECK(run("not xml at all").items.empty());
}
