// The output of layout: absolutely positioned text, ready to blit.
//
// A Page owns its text rather than pointing into the Item it came from. That
// costs about a kilobyte per page and buys the ability to persist a composed
// edition and render it later without keeping the parsed feed alive — which is
// the whole reason reading is cheap on battery.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/text/faces.h"

namespace rsspaper {

// The panel. Fixed here because layout decisions (measure, leading, column
// count) were made for this geometry; a different panel wants its own review,
// not a scaled page.
constexpr int kPageWidth = 1024;
constexpr int kPageHeight = 758;

enum class Align : uint8_t { Left, Center, Right, Justify };

// A fragment of a line sharing one face: usually a word, or a run of words
// that share styling. `x` is measured from the page's left edge, in 1/16 px.
struct PositionedRun {
  FaceId face = FaceId::Body;
  int x = 0;
  std::string text;
};

struct Line {
  int baseline = 0;  // px from the top of the page
  std::vector<PositionedRun> runs;
};

// Horizontal or vertical hairline: section rules, column dividers, the
// nameplate underline.
struct Rule {
  int x = 0, y = 0, w = 0, h = 0;
  uint8_t grey = 0;  // 0 = black
};

// Where an image would go. v1 draws the frame and the caption; decoding the
// image itself is the one thing this reserves space for.
struct ImageSlot {
  int x = 0, y = 0, w = 0, h = 0;
  std::string alt;
};

struct Page {
  std::vector<Line> lines;
  std::vector<Rule> rules;
  std::vector<ImageSlot> images;

  // Running heads. Empty strings are simply not drawn.
  std::string folio_left;   // section name, or the masthead on page 1
  std::string folio_right;  // page number
  bool is_front_page = false;

  void clear() {
    lines.clear();
    rules.clear();
    images.clear();
    folio_left.clear();
    folio_right.clear();
    is_front_page = false;
  }
};

// A rectangle text flows into. The paginator fills columns left to right.
struct Frame {
  int x = 0, y = 0, w = 0, h = 0;
  int bottom() const { return y + h; }
};

}  // namespace rsspaper
