// The v5 streaming edition format: a writer that appends one story's pages
// at a time, and (once the reader exists) an index-then-lazy-load reader.
// Nothing here holds a whole edition resident — that's the point.
#include <functional>
#include <string>
#include <vector>

#include "core/edition/edition_stream.h"
#include "core/io/byte_sink.h"
#include "doctest.h"

using namespace diarium;

namespace {

StoryRef story_meta(const std::string& title, size_t page_count) {
  StoryRef s;
  s.key = std::hash<std::string>{}(title);
  s.title = title;
  s.section = "Technology";
  s.source = "The Source";
  s.page_count = page_count;
  s.truncated = false;
  s.published = 1786000000;
  return s;
}

Page make_page(const std::string& text) {
  Page p;
  p.folio_left = "Technology";
  p.folio_right = "1";
  Line line;
  line.baseline = 40;
  PositionedRun run;
  run.face = FaceId::Body;
  run.x = 44;
  run.text = text;
  line.runs.push_back(run);
  p.lines.push_back(line);
  return p;
}

std::vector<Page> two_pages() {
  return {make_page("page one"), make_page("page two")};
}

std::vector<Page> one_page() { return {make_page("only page")}; }

}  // namespace

TEST_CASE("the streaming writer frames a v5 file") {
  std::string out;
  StringSink sink(&out);
  ComposeStats stats;
  StreamingEditionWriter w(sink, 1786864000, "Diarium", stats);
  CHECK(sink.position() > 0);  // header written up front

  w.add_story(story_meta("A", 2), two_pages());
  const size_t after_first = sink.position();
  w.add_story(story_meta("B", 1), one_page());
  CHECK(sink.position() > after_first);

  REQUIRE(w.finish());

  // v5 magic + version at the front
  CHECK(static_cast<uint8_t>(out[0]) == 0x52);  // 'R' little-endian of 'RSPE'
  CHECK(out.size() > 8);
}

namespace {

std::string write_sample(const std::vector<Page>& pages_a,
                          const std::vector<Page>& pages_b) {
  std::string out;
  StringSink sink(&out);
  ComposeStats stats;
  stats.items_published = 2;
  stats.truncated_published = 1;
  StreamingEditionWriter w(sink, 1786864000, "Diarium", stats);
  w.add_story(story_meta("A", pages_a.size()), pages_a);
  w.add_story(story_meta("B", pages_b.size()), pages_b);
  w.finish();
  return out;
}

}  // namespace

TEST_CASE("the streaming reader loads one story's pages at a time") {
  const std::vector<Page> pages_a = two_pages();
  const std::vector<Page> pages_b = one_page();
  const std::string file = write_sample(pages_a, pages_b);

  StreamingEditionReader r;
  std::string error;
  REQUIRE_MESSAGE(r.open(file, &error), error);

  CHECK(r.date() == 1786864000);
  CHECK(r.title() == "Diarium");
  CHECK(r.stats().items_published == 2);
  CHECK(r.stats().truncated_published == 1);

  REQUIRE(r.index().size() == 2);
  CHECK(r.index()[0].ref.title == "A");
  CHECK(r.index()[0].ref.section == "Technology");
  CHECK(r.index()[0].ref.source == "The Source");
  CHECK(r.index()[0].ref.page_count == pages_a.size());
  CHECK(r.index()[1].ref.title == "B");
  CHECK(r.index()[1].ref.page_count == pages_b.size());

  size_t total_pages = 0;
  for (const StreamIndexEntry& e : r.index()) total_pages += e.ref.page_count;
  CHECK(total_pages == pages_a.size() + pages_b.size());

  const std::vector<Page> loaded_a = r.load_story_pages(0);
  REQUIRE(loaded_a.size() == pages_a.size());
  for (size_t i = 0; i < pages_a.size(); ++i) {
    CAPTURE(i);
    REQUIRE(loaded_a[i].lines.size() == pages_a[i].lines.size());
    CHECK(loaded_a[i].lines[0].runs[0].text == pages_a[i].lines[0].runs[0].text);
    CHECK(loaded_a[i].folio_left == pages_a[i].folio_left);
  }

  const std::vector<Page> loaded_b = r.load_story_pages(1);
  REQUIRE(loaded_b.size() == pages_b.size());
  CHECK(loaded_b[0].lines[0].runs[0].text == pages_b[0].lines[0].runs[0].text);
}

TEST_CASE("a truncated v5 file is refused, not crashed on") {
  const std::string file = write_sample(two_pages(), one_page());

  for (size_t n = 0; n < file.size(); n += 13) {
    CAPTURE(n);
    StreamingEditionReader r;
    std::string error;
    CHECK_FALSE(r.open(file.substr(0, n), &error));
  }
}

TEST_CASE("a corrupt v5 file is refused, not crashed on") {
  const std::string good = write_sample(two_pages(), one_page());

  SUBCASE("wrong footer magic") {
    std::string bad = good;
    bad[bad.size() - 1] = 'X';
    StreamingEditionReader r;
    std::string error;
    CHECK_FALSE(r.open(bad, &error));
  }
  SUBCASE("empty file") {
    StreamingEditionReader r;
    std::string error;
    CHECK_FALSE(r.open("", &error));
    CHECK_FALSE(error.empty());
  }
}
