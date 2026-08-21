// Flowing blocks into pages.
//
// This is where an edition stops being a list of stories and becomes a finite
// object with a last page. Widow and orphan control live here because they are
// the difference between a page that looks typeset and one that looks dumped.
#pragma once

#include <vector>

#include "core/html/block.h"
#include "core/layout/line_breaker.h"
#include "core/layout/page.h"
#include "core/layout/type_scale.h"
#include "core/text/font_pack.h"

namespace diarium {

struct PageTemplate {
  int margin_left = kSideMargin;
  int margin_right = kSideMargin;
  int margin_top = 30;
  int margin_bottom = 40;  // the folio sits in here
  int columns = 1;
  int gutter = 34;
  // Extra space reserved at the top of the first page for the nameplate.
  int first_page_header = 0;
  // A full-width band across the top of the first page, below the nameplate,
  // filled before the columns are. This is how a lead headline spans the page
  // and its story then continues down column one — the defining move of a
  // newspaper front page, and not expressible with uniform columns.
  int banner_height = 0;
  // Hairline between columns, the way a newspaper does it.
  bool column_rules = true;
};

std::vector<Frame> frames_for(const PageTemplate& tmpl, bool first_page);

// Where a flow element ended up: which page, and the area it covers there.
struct Placement {
  size_t page = 0;
  Rect bounds;
};

// A block plus the role it is set in. The composer decides roles; the
// paginator only measures and places.
struct FlowElement {
  TextRole role = TextRole::Body;
  Block block;
  // Forces a new page before this element — used between articles.
  bool page_break_before = false;
  // Suppresses the first-line indent, for the paragraph that opens a story.
  bool opens_story = false;
  // Keeps this element on the same page as the one after it. A lede is a
  // kicker, a headline and a summary; splitting it across a page break makes
  // it unreadable and makes its tap target meaningless.
  bool keep_with_next = false;
};

class Paginator {
 public:
  Paginator(const FontPack& fonts, const Hyphenator& hyphenator)
      : fonts_(fonts), hyphenator_(hyphenator) {}

  // Flows every element, appending to `out`. Returns the number of pages
  // added. Pages are complete and independent once returned.
  //
  // `placements`, when given, receives one entry per flow element: which page
  // it started on and the area it occupies there. That is how the composer
  // locates section starts and, more importantly, how a lede becomes
  // something the reader can tap — without laying the edition out twice.
  size_t paginate(const std::vector<FlowElement>& flow,
                  const PageTemplate& tmpl, std::vector<Page>* out,
                  std::vector<Placement>* placements = nullptr) const;

 private:
  struct Cursor {
    std::vector<Frame> frames;
    size_t frame = 0;
    int pen_y = 0;   // top of the next line box, px
    Page page;
    bool first_page = true;
    bool dirty = false;  // anything placed on this page yet
  };

  void start_page(Cursor* c, const PageTemplate& tmpl) const;
  bool advance_frame(Cursor* c, const PageTemplate& tmpl,
                     std::vector<Page>* out) const;
  void finish_page(Cursor* c, std::vector<Page>* out) const;

  const FontPack& fonts_;
  const Hyphenator& hyphenator_;
};

}  // namespace diarium
