// Layout tests run against the real font pack, because line breaking without
// real metrics tests nothing. They skip rather than fail when the pack is
// absent, so `make check` works before `make fonts` has run.
#include <string>
#include <vector>

#include "core/base/str.h"
#include "core/config/feeds_config.h"
#include "core/edition/seen_store.h"
#include "core/layout/line_breaker.h"
#include "core/layout/paginator.h"
#include "core/layout/type_scale.h"
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

Block para(const std::string& text) {
  Block b;
  b.type = BlockType::Paragraph;
  b.text = text;
  return b;
}

FlowElement element_of(TextRole role, const std::string& text) {
  FlowElement e;
  e.role = role;
  e.block = para(text);
  return e;
}

std::string line_text(const BrokenLine& line) {
  std::string out;
  for (const PositionedRun& r : line.runs) {
    if (!out.empty()) out += " ";
    out += r.text;
  }
  return out;
}

}  // namespace

TEST_CASE("font pack loads and reports plausible metrics") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;  // no pack built; nothing to assert

  for (const FaceSpec& spec : kFaceSpecs) {
    CAPTURE(spec.name);
    const Face& f = fonts->face(spec.id);
    REQUIRE(f.valid());
    CHECK(f.px_size() == spec.px);
    // Ascent plus descent should be in the neighbourhood of the em, never
    // zero and never absurd.
    CHECK(f.ascent() > spec.px / 2);
    CHECK(f.ascent() + f.descent() >= spec.px);
    CHECK(f.ascent() + f.descent() < spec.px * 2);
    CHECK(f.glyph('A') != nullptr);
    CHECK(f.glyph(' ') != nullptr);
    CHECK(f.advance_of('M') > f.advance_of('i'));
    CHECK(f.advance_of(' ') > 0);
  }
}

TEST_CASE("a missing letter shows as tofu, so the gap is visible") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Face& body = fonts->face(FaceId::Body);
  // U+4E2D is a letter we have no glyph for. It must occupy space and be
  // visible: a headline in a script we can't set should look missing, not
  // silently become blank.
  CHECK(fallback_codepoint(0x4E2D) == kTofuGlyph);
  CHECK(body.glyph(0x4E2D) != nullptr);
  CHECK(body.advance_of(0x4E2D) > 0);
  CHECK(body.measure("a\xE4\xB8\xAD" "b") > body.measure("ab"));
}

TEST_CASE("a symbol with a stand-in is substituted, not boxed") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Face& body = fonts->face(FaceId::Body);

  // ★ is not in a book face. Daring Fireball puts one in front of every
  // link-post title, so it has to become something rather than a box.
  CHECK(fallback_codepoint(0x2605) == 0x2022);  // -> bullet
  const Glyph* star = body.glyph(0x2605);
  const Glyph* bullet = body.glyph(0x2022);
  REQUIRE(star != nullptr);
  REQUIRE(bullet != nullptr);
  CHECK(star->codepoint == bullet->codepoint);
  CHECK(body.advance_of(0x2605) == body.advance_of(0x2022));
}

TEST_CASE("decoration with no stand-in is dropped and costs nothing") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Face& body = fonts->face(FaceId::Body);

  const uint32_t check_mark = 0x2713;  // ✓
  const uint32_t emoji = 0x1F600;
  CHECK(fallback_codepoint(check_mark) == kDropGlyph);
  CHECK(fallback_codepoint(emoji) == kDropGlyph);
  CHECK(body.glyph(check_mark) == nullptr);
  CHECK(body.advance_of(check_mark) == 0);

  // Dropping must be consistent between measuring and drawing, or justified
  // lines drift by exactly the width the renderer didn't spend.
  CHECK(body.measure("ab\xE2\x9C\x93") == body.measure("ab"));
}

TEST_CASE("measuring is additive and kerning-consistent") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  const Face& body = fonts->face(FaceId::Body);
  CHECK(body.measure("") == 0);
  CHECK(body.measure("aa") == 2 * body.advance_of('a') + body.kern('a', 'a'));
  CHECK(body.measure("The quick brown fox") > body.measure("The quick"));
}

TEST_CASE("lines fit the measure") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  const RoleStyle style = role_style(TextRole::Body);
  const Block b = para(
      "The quick brown fox jumps over the lazy dog, and then continues on "
      "for some considerable distance so that the text must break across "
      "several lines of a realistic newspaper measure.");

  for (const int measure : {200, 440, 916}) {
    CAPTURE(measure);
    const std::vector<BrokenLine> lines =
        break_block(b, style, measure, *fonts, null_hyphenator());
    REQUIRE(lines.size() > 1);
    for (const BrokenLine& line : lines) {
      CHECK(line.width <= measure * kSubpixel);
    }
    CHECK(lines.back().last);
  }
}

TEST_CASE("breaking preserves every word, in order") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  const std::string text =
      "One two three four five six seven eight nine ten eleven twelve "
      "thirteen fourteen fifteen sixteen seventeen eighteen";
  const std::vector<BrokenLine> lines = break_block(
      para(text), role_style(TextRole::Body), 300, *fonts, null_hyphenator());

  std::string rebuilt;
  for (const BrokenLine& line : lines) {
    if (!rebuilt.empty()) rebuilt += " ";
    rebuilt += line_text(line);
  }
  CHECK(rebuilt == text);
}

TEST_CASE("a hard break starts a new line") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  Block b = para("first\nsecond");
  const std::vector<BrokenLine> lines =
      break_block(b, role_style(TextRole::Body), 916, *fonts,
                  null_hyphenator());
  REQUIRE(lines.size() == 2);
  CHECK(line_text(lines[0]) == "first");
  CHECK(line_text(lines[1]) == "second");
}

TEST_CASE("a styled span does not split its word across faces mid-line") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  Block b = para("word");
  b.runs.push_back(StyleRun{0, 2, kStyleBold});  // "wo" bold, "rd" plain
  const std::vector<BrokenLine> lines = break_block(
      b, role_style(TextRole::Body), 916, *fonts, null_hyphenator());
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].runs.size() == 2);
  CHECK(lines[0].runs[0].face == FaceId::BodyBold);
  CHECK(lines[0].runs[1].face == FaceId::Body);
  // The fragments must abut, not be separated by a word space.
  const int join = lines[0].runs[0].x +
                   fonts->face(FaceId::BodyBold).measure(lines[0].runs[0].text);
  CHECK(lines[0].runs[1].x == join);
}

TEST_CASE("justified lines reach the measure, except the last") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  RoleStyle style = role_style(TextRole::Body);
  style.align = Align::Justify;
  const Block b = para(
      "Justification distributes the slack across the word spaces of every "
      "line except the last one, which is left to end wherever it ends.");

  const std::vector<BrokenLine> lines =
      break_atoms(atomize(b, style, *fonts), 440, Align::Justify, *fonts,
                  null_hyphenator());
  REQUIRE(lines.size() >= 2);
  for (size_t i = 0; i + 1 < lines.size(); ++i) {
    CHECK(lines[i].width <= 440 * kSubpixel);
  }
}

TEST_CASE("empty and whitespace-only blocks produce no lines") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  CHECK(break_block(para(""), role_style(TextRole::Body), 400, *fonts,
                    null_hyphenator())
            .empty());
  CHECK(break_block(para("   "), role_style(TextRole::Body), 400, *fonts,
                    null_hyphenator())
            .empty());
}

TEST_CASE("a zero or negative measure yields nothing rather than looping") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;
  CHECK(break_block(para("text"), role_style(TextRole::Body), 0, *fonts,
                    null_hyphenator())
            .empty());
}

TEST_CASE("frames: columns tile the measure and the banner spans it") {
  PageTemplate tmpl;
  tmpl.columns = 2;
  tmpl.banner_height = 200;
  tmpl.first_page_header = 100;

  const std::vector<Frame> front = frames_for(tmpl, true);
  REQUIRE(front.size() == 3);  // banner + 2 columns
  CHECK(front[0].w == kPageWidth - tmpl.margin_left - tmpl.margin_right);
  CHECK(front[0].y == tmpl.margin_top + tmpl.first_page_header);
  CHECK(front[1].y == front[0].y + tmpl.banner_height);
  CHECK(front[1].y == front[2].y);
  CHECK(front[2].x > front[1].x + front[1].w);  // gutter between them
  CHECK(front[2].x + front[2].w <= kPageWidth - tmpl.margin_right);

  const std::vector<Frame> inner = frames_for(tmpl, false);
  REQUIRE(inner.size() == 2);  // no banner, no header reserve
  CHECK(inner[0].y == tmpl.margin_top);
}

TEST_CASE("pagination places every line inside its frame") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  std::vector<FlowElement> flow;
  for (int i = 0; i < 40; ++i) {
    FlowElement e;
    e.role = TextRole::Body;
    e.block = para(
        "Paragraph " + std::to_string(i) +
        ": text long enough to wrap several times over a full-width measure "
        "so that pagination has real work to do and pages actually fill.");
    flow.push_back(std::move(e));
  }

  PageTemplate tmpl;
  std::vector<Page> pages;
  const Paginator p(*fonts, null_hyphenator());
  const size_t n = p.paginate(flow, tmpl, &pages);

  CHECK(n > 1);
  CHECK(pages.size() == n);
  for (const Page& page : pages) {
    CHECK_FALSE(page.lines.empty());
    for (const Line& line : page.lines) {
      CHECK(line.baseline > 0);
      CHECK(line.baseline < kPageHeight);
      for (const PositionedRun& r : line.runs) {
        CHECK(to_px(r.x) >= 0);
        CHECK(to_px(r.x) < kPageWidth);
      }
    }
  }
}

TEST_CASE("a page break starts a new page") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  std::vector<FlowElement> flow;
  for (int i = 0; i < 3; ++i) {
    FlowElement e;
    e.role = TextRole::Body;
    e.block = para("Story " + std::to_string(i));
    e.page_break_before = true;
    flow.push_back(std::move(e));
  }
  std::vector<Page> pages;
  std::vector<Placement> where;
  Paginator(*fonts, null_hyphenator())
      .paginate(flow, PageTemplate(), &pages, &where);

  CHECK(pages.size() == 3);
  REQUIRE(where.size() == 3);
  CHECK(where[0].page == 0);
  CHECK(where[1].page == 1);
  CHECK(where[2].page == 2);
  // Every element reports a real area, which is what makes a lede tappable.
  for (const Placement& p : where) {
    CHECK_FALSE(p.bounds.empty());
    CHECK(p.bounds.y >= 0);
    CHECK(p.bounds.y + p.bounds.h <= kPageHeight);
  }
}

TEST_CASE("feeds.toml parses the config we ship") {
  FeedList list;
  std::string error;
  REQUIRE_MESSAGE(load_feeds_toml("config/feeds.toml", &list, &error), error);
  CHECK(list.feeds.size() >= 10);
  CHECK(list.edition.max_items > 0);
  for (const FeedEntry& f : list.feeds) {
    CHECK(starts_with(f.url, "http"));
    CHECK_FALSE(f.section.empty());
  }
  const std::vector<std::string> order = list.section_order();
  CHECK(order.size() >= 3);
  CHECK(order[0] == "Technology");  // section order is the running order
}

TEST_CASE("feeds.toml: values, comments and unknown keys") {
  FeedList list;
  std::string error;
  const char* toml = R"(
# a comment
[edition]
title = "The Daily"
max_items = 12          # trailing comment
body_alignment = "justified"
unknown_key = "ignored"

[unknown_table]
whatever = 1

[[feed]]
url = "https://example.com/feed"
section = "News"
max_items = 4
)";
  REQUIRE(parse_feeds_toml(toml, &list, &error));
  CHECK(list.edition.title == "The Daily");
  CHECK(list.edition.max_items == 12);
  CHECK(list.edition.body_alignment == Align::Justify);
  REQUIRE(list.feeds.size() == 1);
  CHECK(list.feeds[0].url == "https://example.com/feed");
  CHECK(list.feeds[0].max_items == 4);
}

TEST_CASE("feeds.toml errors name the problem") {
  FeedList list;
  std::string error;
  CHECK_FALSE(parse_feeds_toml("[[feed]]\nsection = \"x\"\n", &list, &error));
  CHECK(error.find("url") != std::string::npos);

  CHECK_FALSE(parse_feeds_toml("[edition]\ntitle = \"x\"\n", &list, &error));
  CHECK(error.find("no [[feed]]") != std::string::npos);

  CHECK_FALSE(parse_feeds_toml(
      "[[feed]]\nurl = \"u\"\nmax_items = many\n", &list, &error));
  CHECK(error.find("whole number") != std::string::npos);
}

TEST_CASE("the seen store keeps stories from running twice") {
  SeenStore seen;
  const Epoch now = 1767225600;
  CHECK(seen.mark(1234, now));
  CHECK_FALSE(seen.mark(1234, now));  // second time is a duplicate
  CHECK(seen.has(1234));
  CHECK_FALSE(seen.has(5678));
  CHECK(seen.size() == 1);
}

TEST_CASE("the seen store round-trips and expires old entries") {
  const Epoch now = 1767225600;
  const std::string path = "build/test-seen.txt";
  {
    SeenStore seen(30);
    seen.mark(0xAAAA, now);
    seen.mark(0xBBBB, now - 40 * 86400);  // older than the retention window
    REQUIRE(seen.save(path));
  }
  SeenStore reloaded(30);
  REQUIRE(reloaded.load(path, now));
  CHECK(reloaded.has(0xAAAA));
  CHECK_FALSE(reloaded.has(0xBBBB));
}

TEST_CASE("a missing seen store is not an error") {
  SeenStore seen;
  CHECK_FALSE(seen.load("build/definitely-not-here.txt", 0));
  CHECK(seen.size() == 0);
}

TEST_CASE("keep_with_next moves a group rather than splitting it") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  // Fill most of a page, then a three-element group that cannot fit in what
  // remains. All three must land together on the next page.
  std::vector<FlowElement> flow;
  for (int i = 0; i < 14; ++i) {
    FlowElement e;
    e.role = TextRole::Body;
    e.block = para("Filler paragraph " + std::to_string(i) +
                   " with enough text to occupy a full line of the measure.");
    flow.push_back(std::move(e));
  }
  FlowElement kicker = element_of(TextRole::LedeKicker, "The Source");
  kicker.keep_with_next = true;
  FlowElement head = element_of(TextRole::LedeHead,
                                "A headline that runs to about two lines of "
                                "the available measure on this page");
  head.keep_with_next = true;
  const size_t group_start = flow.size();
  flow.push_back(std::move(kicker));
  flow.push_back(std::move(head));
  flow.push_back(element_of(TextRole::LedeText,
                            "A summary of the story, long enough to take a "
                            "couple of lines under the headline."));

  std::vector<Page> pages;
  std::vector<Placement> where;
  Paginator(*fonts, null_hyphenator())
      .paginate(flow, PageTemplate(), &pages, &where);

  REQUIRE(where.size() == flow.size());
  const size_t page = where[group_start].page;
  CHECK(where[group_start + 1].page == page);
  CHECK(where[group_start + 2].page == page);
}

TEST_CASE("a placement's bounds never span a page break") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  std::vector<FlowElement> flow;
  for (int i = 0; i < 60; ++i) {
    FlowElement e;
    e.role = TextRole::Body;
    e.block = para(
        "Paragraph " + std::to_string(i) +
        " long enough to wrap over several lines and eventually to straddle "
        "a page boundary, which is the case that matters here.");
    flow.push_back(std::move(e));
  }

  std::vector<Page> pages;
  std::vector<Placement> where;
  Paginator(*fonts, null_hyphenator())
      .paginate(flow, PageTemplate(), &pages, &where);

  for (const Placement& p : where) {
    CHECK(p.bounds.y >= 0);
    CHECK(p.bounds.y + p.bounds.h <= kPageHeight);
    CHECK(p.page < pages.size());
  }
}
