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
//
// RSSPAPER_PORTRAIT swaps the axes so the orientation question can be looked at
// rather than argued about. It is a scaffold for that experiment, not a
// supported mode: the panel raster is landscape-native, so a real portrait
// build would also need setRotation behind the HAL, and the front page's
// column count and the type scale would both want a fresh review at 758 wide.
#if RSSPAPER_PORTRAIT
constexpr int kPageWidth = 758;
constexpr int kPageHeight = 1024;
#else
constexpr int kPageWidth = 1024;
constexpr int kPageHeight = 758;
#endif

// One margin, used by both the paginator's frames and the renderer's
// furniture. They must agree or rules won't line up with columns.
constexpr int kSideMargin = 44;

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

// A region on a page. Used for the tap target of a lede: the reader needs to
// know where a story's summary sits in order to let you open it.
struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool empty() const { return w <= 0 || h <= 0; }
  bool contains(int px, int py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
  void extend(const Rect& other) {
    if (other.empty()) return;
    if (empty()) {
      *this = other;
      return;
    }
    const int x1 = x < other.x ? x : other.x;
    const int y1 = y < other.y ? y : other.y;
    const int x2 = (x + w) > (other.x + other.w) ? x + w : other.x + other.w;
    const int y2 = (y + h) > (other.y + other.h) ? y + h : other.y + other.h;
    x = x1;
    y = y1;
    w = x2 - x1;
    h = y2 - y1;
  }
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
