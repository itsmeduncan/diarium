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

namespace diarium {

// The panel is 1024x758 of raster whichever way it is stood; orientation
// chooses how a page is laid onto it. Portrait (758x1024) is the product
// default and lands via config; landscape is the opt-in. The type scale is
// physical — sized to the ~212 PPI panel — so it does not move with the axes;
// only the measure and the front page's column count do.
enum class Orientation : uint8_t { Landscape, Portrait };

// Orientation is a compose-time property: an edition's pages bake in absolute
// positions, so it must be chosen before any layout runs and not change under
// a composed edition. Set once at startup from EditionConfig, read-only
// thereafter. A function rather than a constant because it is chosen at
// runtime; write-once-before-read leaves it as good as constant for the
// single-threaded pipeline that reads it. The core keeps no default opinion —
// it stays landscape until told — because the default that ships is expressed
// in config, where the reader can see and change it.
void set_orientation(Orientation o);
Orientation orientation();
int page_width();
int page_height();

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

}  // namespace diarium
