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
