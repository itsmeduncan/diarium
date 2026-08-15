// Hyphenation. The expected breaks are TeX's, because these are TeX's
// patterns — if we disagree with TeX about "hy-phen-ation" the bug is ours.
#include <string>
#include <vector>

#include "core/layout/hyphenator.h"
#include "core/layout/line_breaker.h"
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

// "hyphenation" -> "hy-phen-ation"
std::string hyphenated(const std::string& word,
                       const Hyphenator& h = english_hyphenator()) {
  std::vector<size_t> points;
  h.break_points(word, &points);
  std::string out;
  size_t k = 0;
  for (size_t i = 0; i < word.size(); ++i) {
    if (k < points.size() && points[k] == i) {
      out += "-";
      ++k;
    }
    out += word[i];
  }
  return out;
}

}  // namespace

TEST_CASE("hyphenation agrees with TeX") {
  CHECK(hyphenated("hyphenation") == "hy-phen-ation");
  CHECK(hyphenated("typesetting") == "type-set-ting");
  CHECK(hyphenated("newspaper") == "news-pa-per");
  CHECK(hyphenated("beautiful") == "beau-ti-ful");
  CHECK(hyphenated("democracy") == "democ-racy");
  CHECK(hyphenated("algorithm") == "al-go-rithm");
  CHECK(hyphenated("computer") == "com-puter");
}

TEST_CASE("the exception list overrides the patterns") {
  // These are the words the pattern file itself lists as wrong.
  CHECK(hyphenated("associate") == "as-so-ciate");
  CHECK(hyphenated("declination") == "dec-li-na-tion");
  CHECK(hyphenated("philanthropic") == "phil-an-thropic");
  // "present" is in the list with no breaks at all, meaning never break it.
  CHECK(hyphenated("present") == "present");
}

TEST_CASE("short words are left alone") {
  for (const char* w : {"the", "and", "a", "in", "of", "cat", "four"}) {
    CAPTURE(w);
    CHECK(hyphenated(w) == w);
  }
}

TEST_CASE("breaks respect the minimums the patterns were designed for") {
  // Two characters before the first break, three after the last.
  for (const char* w : {"hyphenation", "typesetting", "everything",
                        "understanding", "administrator"}) {
    CAPTURE(w);
    const std::string word = w;
    std::vector<size_t> points;
    english_hyphenator().break_points(word, &points);
    for (size_t p : points) {
      CHECK(p >= 2);
      CHECK(word.size() - p >= 3);
    }
    // And they come out ascending, without duplicates.
    for (size_t i = 1; i < points.size(); ++i) CHECK(points[i] > points[i - 1]);
  }
}

TEST_CASE("words the patterns cannot speak to are left whole") {
  // Non-ASCII, digits, apostrophes and hyphens are all outside the pattern
  // set; guessing at them would be worse than not breaking.
  for (const char* w : {"na\xc3\xafve", "don't", "F1-2000", "ISO8601",
                        "\xe4\xb8\xad\xe6\x96\x87"}) {
    CAPTURE(w);
    std::vector<size_t> points;
    english_hyphenator().break_points(w, &points);
    CHECK(points.empty());
  }
}

TEST_CASE("the null hyphenator never breaks anything") {
  CHECK(hyphenated("hyphenation", null_hyphenator()) == "hyphenation");
}

TEST_CASE("a hyphenated line still fits its measure") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  RoleStyle style = role_style(TextRole::Body);
  style.align = Align::Justify;
  Block b;
  b.type = BlockType::Paragraph;
  b.text =
      "The administrators of the establishment were recommending an "
      "extraordinarily complicated reorganisation, notwithstanding the "
      "considerable inconvenience it represented.";

  for (const int measure : {240, 380, 520}) {
    CAPTURE(measure);
    const std::vector<BrokenLine> lines =
        break_block(b, style, measure, *fonts, english_hyphenator());
    REQUIRE(lines.size() > 1);
    for (const BrokenLine& line : lines) {
      CHECK(line.width <= measure * kSubpixel);
    }
  }
}

TEST_CASE("hyphenating loses no text and adds only hyphens") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  RoleStyle style = role_style(TextRole::Body);
  style.align = Align::Justify;
  Block b;
  b.type = BlockType::Paragraph;
  b.text =
      "Extraordinarily complicated reorganisation notwithstanding the "
      "considerable inconvenience representing administrative difficulty.";

  const std::vector<BrokenLine> lines =
      break_block(b, style, 260, *fonts, english_hyphenator());

  std::string rebuilt;
  for (const BrokenLine& line : lines) {
    for (const PositionedRun& r : line.runs) {
      if (!rebuilt.empty() && rebuilt.back() != '-') rebuilt += " ";
      // A trailing hyphen is the break mark; drop it to reassemble the word.
      if (r.text.size() > 1 && r.text.back() == '-') {
        rebuilt += r.text.substr(0, r.text.size() - 1);
      } else {
        rebuilt += r.text;
      }
    }
  }
  // Rejoining across the hyphens must give back exactly the original.
  std::string expected;
  for (char c : b.text) {
    if (c != ' ') expected += c;
  }
  std::string got;
  for (char c : rebuilt) {
    if (c != ' ') got += c;
  }
  CHECK(got == expected);
}

TEST_CASE("hyphenation makes narrow justified columns tighter") {
  const FontPack* fonts = pack();
  if (fonts == nullptr) return;

  RoleStyle style = role_style(TextRole::Body);
  style.align = Align::Justify;
  Block b;
  b.type = BlockType::Paragraph;
  b.text =
      "The administrators were recommending an extraordinarily complicated "
      "reorganisation, notwithstanding the considerable inconvenience that "
      "such an undertaking would inevitably represent for everybody "
      "concerned.";

  const int measure = 300;
  const std::vector<BrokenLine> without =
      break_block(b, style, measure, *fonts, null_hyphenator());
  const std::vector<BrokenLine> with =
      break_block(b, style, measure, *fonts, english_hyphenator());

  // The measure of success is how much slack has to be absorbed by word
  // spaces: less slack is a tighter, less rivery column.
  auto total_slack = [&](const std::vector<BrokenLine>& lines) {
    int slack = 0;
    for (size_t i = 0; i + 1 < lines.size(); ++i) {
      slack += measure * kSubpixel - lines[i].width;
    }
    return slack;
  };
  CHECK(total_slack(with) < total_slack(without));
}
