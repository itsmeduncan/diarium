// Pulling the article out of a whole web page.
//
// A feed that truncates gives two paragraphs and an ellipsis; the page it
// links to has the rest, wrapped in navigation, related-story rails, cookie
// notices and a footer. This is the part that decides which of those is the
// story.
#include "core/html/readability.h"

#include <string>
#include <vector>

#include "core/html/block.h"
#include "doctest.h"

using namespace diarium;

namespace {

Block para(const std::string& text) {
  Block b;
  b.type = BlockType::Paragraph;
  b.text = text;
  return b;
}

Block heading(const std::string& text, uint8_t level) {
  Block b;
  b.type = BlockType::Heading;
  b.level = level;
  b.text = text;
  return b;
}

const char* kBody =
    "The fire service said the scale of the incidents was without recent "
    "precedent, with crews drawn from three neighbouring counties working "
    "through the night to contain what had begun as separate fires.";

std::string joined(const std::vector<Block>& blocks) {
  std::string out;
  for (const Block& b : blocks) out += b.text + "\n";
  return out;
}

}  // namespace

TEST_CASE("the longest run of real prose is the article") {
  std::vector<Block> page = {
      para("Home"), para("News"), para("Sport"), para("Sign in"),
      para(kBody),  para(kBody),  para(kBody),
      para("Related stories"), para("More from us"), para("Contact"),
      para("Copyright 2026"),
  };
  const std::vector<Block> article = extract_article(page);
  CHECK(article.size() == 3);
  CHECK(joined(article).find("fire service") != std::string::npos);
  CHECK(joined(article).find("Copyright") == std::string::npos);
  CHECK(joined(article).find("Sign in") == std::string::npos);
}

TEST_CASE("a heading inside the run is kept") {
  std::vector<Block> page = {
      para("Menu"),
      para(kBody),
      heading("What happens next", 2),
      para(kBody),
      para("Follow us"),
  };
  const std::vector<Block> article = extract_article(page);
  CHECK(joined(article).find("What happens next") != std::string::npos);
}

TEST_CASE("a page that is all navigation yields nothing") {
  std::vector<Block> page = {
      para("Home"), para("News"), para("Sport"), para("Jobs"), para("Contact"),
  };
  CHECK(extract_article(page).empty());
}

TEST_CASE("an empty page yields nothing") {
  CHECK(extract_article({}).empty());
}

TEST_CASE("a single long paragraph is an article") {
  std::vector<Block> page = {para("Nav"), para(kBody), para("Footer")};
  const std::vector<Block> article = extract_article(page);
  REQUIRE(article.size() == 1);
  CHECK(article[0].text == kBody);
}

TEST_CASE("the denser of two runs wins") {
  std::vector<Block> page = {
      para(kBody),
      para("Advertisement"),
      para("Sponsored"),
      para(kBody), para(kBody), para(kBody), para(kBody),
  };
  const std::vector<Block> article = extract_article(page);
  CHECK(article.size() == 4);
}

TEST_CASE("extraction is bounded") {
  // A hostile page must not produce an article larger than a paper can hold.
  std::vector<Block> page;
  for (int i = 0; i < 5000; ++i) page.push_back(para(kBody));
  ReadabilityLimits limits;
  limits.max_blocks = 60;
  CHECK(extract_article(page, limits).size() <= 60);
}
