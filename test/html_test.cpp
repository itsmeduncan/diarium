#include <string>
#include <vector>

#include "core/html/html_to_blocks.h"
#include "doctest.h"

using namespace rsspaper;

namespace {

// "p:text|h2:text|li:text" — enough structure to assert on without being
// sensitive to fields the test doesn't care about.
std::string shape(const std::vector<Block>& blocks) {
  std::string out;
  for (const Block& b : blocks) {
    if (!out.empty()) out += "|";
    switch (b.type) {
      case BlockType::Paragraph: out += "p"; break;
      case BlockType::Heading: out += "h" + std::to_string(b.level); break;
      case BlockType::Blockquote: out += "q" + std::to_string(b.level); break;
      case BlockType::ListItem: out += "li"; break;
      case BlockType::Code: out += "pre"; break;
      case BlockType::Rule: out += "hr"; break;
      case BlockType::Image: out += "img"; break;
      case BlockType::Caption: out += "cap"; break;
    }
    out += ":" + b.text;
  }
  return out;
}

std::string styled_text(const Block& b, uint8_t flag) {
  std::string out;
  for (const StyleRun& r : b.runs) {
    if ((r.flags & flag) != 0) out += b.text.substr(r.start, r.length);
  }
  return out;
}

}  // namespace

TEST_CASE("paragraphs and headings") {
  CHECK(shape(html_to_blocks("<h2>Title</h2><p>One.</p><p>Two.</p>")) ==
        "h2:Title|p:One.|p:Two.");
}

TEST_CASE("whitespace collapses and blocks are trimmed") {
  CHECK(shape(html_to_blocks("<p>  a\n\n   b  </p>")) == "p:a b");
  CHECK(shape(html_to_blocks("<p>   </p><p>x</p>")) == "p:x");
}

TEST_CASE("text outside any element still becomes a paragraph") {
  CHECK(shape(html_to_blocks("bare text")) == "p:bare text");
}

TEST_CASE("inline styling produces non-overlapping runs") {
  const std::vector<Block> b =
      html_to_blocks("<p>plain <strong>bold</strong> and <em>ital</em>.</p>");
  REQUIRE(b.size() == 1);
  CHECK(b[0].text == "plain bold and ital.");
  CHECK(styled_text(b[0], kStyleBold) == "bold");
  CHECK(styled_text(b[0], kStyleItalic) == "ital");
}

TEST_CASE("style runs never overhang the text they annotate") {
  // A styled span ending in whitespace at the end of a block: the trailing
  // space is trimmed, and the run must be trimmed with it.
  for (const char* html : {"<p>a <b>bold </b></p>", "<p><em>all </em></p>",
                           "<p>x <a href='u'>link </a>  </p>"}) {
    CAPTURE(html);
    for (const Block& b : html_to_blocks(html)) {
      for (const StyleRun& r : b.runs) {
        CHECK(static_cast<size_t>(r.start) + r.length <= b.text.size());
      }
    }
  }
}

TEST_CASE("nested inline tags combine flags") {
  const std::vector<Block> b =
      html_to_blocks("<p><b>bo<i>th</i></b></p>");
  REQUIRE(b.size() == 1);
  CHECK(b[0].text == "both");
  CHECK(styled_text(b[0], kStyleBold) == "both");
  CHECK(styled_text(b[0], kStyleItalic) == "th");
}

TEST_CASE("unclosed inline tags don't leak into the next block") {
  const std::vector<Block> b = html_to_blocks("<p><em>a</p><p>b</p>");
  REQUIRE(b.size() == 2);
  CHECK(b[1].text == "b");
}

TEST_CASE("links are styled but their URLs are not rendered in v1") {
  const std::vector<Block> b =
      html_to_blocks("<p>see <a href=\"http://x\">this</a></p>");
  REQUIRE(b.size() == 1);
  CHECK(b[0].text == "see this");
  CHECK(styled_text(b[0], kStyleLink) == "this");
}

TEST_CASE("lists carry depth, ordering and index") {
  const std::vector<Block> b =
      html_to_blocks("<ol><li>one</li><li>two</li></ol>");
  REQUIRE(b.size() == 2);
  CHECK(b[0].ordered);
  CHECK(b[0].list_index == 1);
  CHECK(b[1].list_index == 2);
  CHECK(b[0].level == 1);
}

TEST_CASE("nested lists increase depth") {
  const std::vector<Block> b =
      html_to_blocks("<ul><li>a<ul><li>b</li></ul></li></ul>");
  REQUIRE(b.size() == 2);
  CHECK(b[0].level == 1);
  CHECK(b[1].level == 2);
  CHECK_FALSE(b[1].ordered);
}

TEST_CASE("blockquote depth") {
  const std::vector<Block> b = html_to_blocks(
      "<blockquote><p>a</p><blockquote><p>b</p></blockquote></blockquote>");
  REQUIRE(b.size() == 2);
  CHECK(b[0].level == 1);
  CHECK(b[1].level == 2);
}

TEST_CASE("script and style contents are discarded entirely") {
  CHECK(shape(html_to_blocks(
            "<p>a</p><script>var x = '<p>ghost</p>';</script><p>b</p>")) ==
        "p:a|p:b");
  CHECK(shape(html_to_blocks("<style>p { color: red }</style><p>b</p>")) ==
        "p:b");
}

TEST_CASE("br is a hard break inside a block, not a new block") {
  const std::vector<Block> b = html_to_blocks("<p>one<br>two</p>");
  REQUIRE(b.size() == 1);
  CHECK(b[0].text == "one\ntwo");
}

TEST_CASE("images become placeholders carrying alt and src") {
  const std::vector<Block> b = html_to_blocks(
      "<p>before</p><img src=\"http://x/y.png\" alt=\"A cat\"><p>after</p>");
  REQUIRE(b.size() == 3);
  CHECK(b[1].type == BlockType::Image);
  CHECK(b[1].text == "A cat");
  CHECK(b[1].src == "http://x/y.png");
}

TEST_CASE("pre preserves whitespace") {
  const std::vector<Block> b = html_to_blocks("<pre>a  b\n  c</pre>");
  REQUIRE(b.size() == 1);
  CHECK(b[0].type == BlockType::Code);
  CHECK(b[0].text == "a  b\n  c");
}

TEST_CASE("entities decode, including numeric and unknown") {
  CHECK(shape(html_to_blocks("<p>a&amp;b &#8212; c &bogus; d</p>")) ==
        "p:a&b — c &bogus; d");
}

TEST_CASE("an entity split across chunk boundaries still decodes") {
  BlockCollector out;
  HtmlToBlocks conv(out);
  conv.feed("<p>a&am");
  conv.feed("p;b</p>");
  conv.finish();
  REQUIRE(out.blocks.size() == 1);
  CHECK(out.blocks[0].text == "a&b");
}

TEST_CASE("a tag split across chunk boundaries still parses") {
  BlockCollector out;
  HtmlToBlocks conv(out);
  conv.feed("<p>one</p><p");
  conv.feed(" class=\"x\">two</p>");
  conv.finish();
  CHECK(shape(out.blocks) == "p:one|p:two");
}

TEST_CASE("malformed markup degrades instead of failing") {
  CHECK(shape(html_to_blocks("<p>a<p>b")) == "p:a|p:b");
  CHECK(shape(html_to_blocks("<<>><p>x</p>")).find("p:x") !=
        std::string::npos);
  CHECK(shape(html_to_blocks("<p>3 < 4</p>")) == "p:3 < 4");
}

TEST_CASE("limits cap block count and block size") {
  HtmlLimits limits;
  limits.max_blocks = 3;
  limits.max_block_bytes = 10;

  std::string html;
  for (int i = 0; i < 50; ++i) html += "<p>" + std::string(100, 'x') + "</p>";

  BlockCollector out;
  HtmlToBlocks conv(out, limits);
  conv.feed(html);
  conv.finish();

  CHECK(out.blocks.size() == 3);
  for (const Block& b : out.blocks) CHECK(b.text.size() <= 10);
  CHECK(conv.hit_limit());
}

TEST_CASE("reset discards everything, for when a better content element shows up") {
  BlockCollector out;
  HtmlToBlocks conv(out);
  conv.feed("<p>summary</p>");
  conv.reset();
  out.blocks.clear();
  conv.feed("<p>the real article</p>");
  conv.finish();
  CHECK(shape(out.blocks) == "p:the real article");
}

TEST_CASE("plain text splits on blank lines") {
  CHECK(shape(text_to_blocks("one\n\ntwo\nstill two")) ==
        "p:one|p:two still two");
}
