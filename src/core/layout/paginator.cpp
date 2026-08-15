#include "core/layout/paginator.h"

#include "core/base/str.h"

namespace rsspaper {
namespace {

// Compact image placeholder. A framed box would be the obvious choice, but
// feeds like NASA's carry twenty images per item and twenty empty boxes make
// the page unreadable. A marked caption line says the same thing and costs one
// line. Real image decoding replaces `render_image_slot`, nothing else.
constexpr const char* kImageMark = "\xE2\x96\xA3 ";  // ▣

int line_height(const RoleStyle& style) {
  return style.leading > 0 ? style.leading : 32;
}

}  // namespace

std::vector<Frame> frames_for(const PageTemplate& tmpl, bool first_page) {
  std::vector<Frame> frames;
  int top = tmpl.margin_top + (first_page ? tmpl.first_page_header : 0);
  const int usable_w = kPageWidth - tmpl.margin_left - tmpl.margin_right;
  const int cols = tmpl.columns < 1 ? 1 : tmpl.columns;
  const int col_w = (usable_w - tmpl.gutter * (cols - 1)) / cols;

  // The banner is a frame like any other, and being first in the list is what
  // makes the flow fill it before the columns.
  if (first_page && tmpl.banner_height > 0) {
    Frame banner;
    banner.x = tmpl.margin_left;
    banner.y = top;
    banner.w = usable_w;
    banner.h = tmpl.banner_height;
    frames.push_back(banner);
    top += tmpl.banner_height;
  }

  const int usable_h = kPageHeight - top - tmpl.margin_bottom;
  for (int i = 0; i < cols; ++i) {
    Frame f;
    f.x = tmpl.margin_left + i * (col_w + tmpl.gutter);
    f.y = top;
    f.w = col_w;
    f.h = usable_h;
    frames.push_back(f);
  }
  return frames;
}

void Paginator::start_page(Cursor* c, const PageTemplate& tmpl) const {
  c->page.clear();
  c->page.is_front_page = c->first_page;
  c->frames = frames_for(tmpl, c->first_page);
  c->frame = 0;
  c->pen_y = c->frames.empty() ? 0 : c->frames[0].y;
  c->dirty = false;

  // Rules go *between columns*. The banner is frames[0] on the front page and
  // spans the measure, so a rule after it would hang down the right margin.
  const size_t first_col =
      (c->first_page && tmpl.banner_height > 0 && !c->frames.empty()) ? 1 : 0;
  if (tmpl.column_rules && c->frames.size() > first_col + 1) {
    for (size_t i = first_col; i + 1 < c->frames.size(); ++i) {
      const Frame& f = c->frames[i];
      Rule r;
      r.x = f.x + f.w + tmpl.gutter / 2;
      r.y = f.y;
      r.w = 1;
      r.h = f.h;
      r.grey = 150;  // a hairline, not a black bar
      c->page.rules.push_back(r);
    }
  }
}

void Paginator::finish_page(Cursor* c, std::vector<Page>* out) const {
  if (!c->dirty) return;
  out->push_back(c->page);
  c->first_page = false;
}

bool Paginator::advance_frame(Cursor* c, const PageTemplate& tmpl,
                              std::vector<Page>* out) const {
  if (c->frame + 1 < c->frames.size()) {
    ++c->frame;
    c->pen_y = c->frames[c->frame].y;
    return false;
  }
  finish_page(c, out);
  start_page(c, tmpl);
  return true;
}

size_t Paginator::paginate(const std::vector<FlowElement>& flow,
                           const PageTemplate& tmpl, std::vector<Page>* out,
                           std::vector<size_t>* element_page) const {
  const size_t before = out->size();
  Cursor c;
  start_page(&c, tmpl);
  if (element_page != nullptr) element_page->assign(flow.size(), before);

  for (size_t ei = 0; ei < flow.size(); ++ei) {
    const FlowElement& el = flow[ei];
    if (el.page_break_before && c.dirty) {
      finish_page(&c, out);
      start_page(&c, tmpl);
    }
    // The in-progress page has not been appended yet, so its eventual index
    // is exactly the current size of `out`.
    if (element_page != nullptr) (*element_page)[ei] = out->size();

    const RoleStyle style = role_style(el.role);

    if (el.block.type == BlockType::Rule) {
      const Frame& f = c.frames[c.frame];
      if (c.pen_y + 16 > f.bottom()) advance_frame(&c, tmpl, out);
      const Frame& g = c.frames[c.frame];
      Rule r;
      r.x = g.x + g.w / 4;
      r.y = c.pen_y + 8;
      r.w = g.w / 2;
      r.h = 1;
      r.grey = 120;
      c.page.rules.push_back(r);
      c.pen_y += 20;
      c.dirty = true;
      continue;
    }

    Block block = el.block;
    if (block.type == BlockType::Image) {
      block.text = std::string(kImageMark) +
                   (block.text.empty() ? "Image" : block.text);
      block.runs.clear();
    }
    if (block.text.empty()) continue;

    // List markers hang in the left margin, so the text measure shrinks.
    std::string marker;
    if (el.role == TextRole::ListItem) {
      marker = block.ordered ? std::to_string(block.list_index) + "."
                             : "\xE2\x80\xA2";  // •
    }

    const Frame* frame = &c.frames[c.frame];
    const int measure = frame->w - style.left_margin;
    if (measure <= 0) continue;

    std::vector<BrokenLine> lines =
        break_block(block, style, measure, fonts_, hyphenator_);
    if (lines.empty()) continue;

    const int lh = line_height(style);
    const Face& face = fonts_.face(style.face);
    const int ascent = face.ascent();

    size_t placed = 0;
    bool first_chunk = true;
    while (placed < lines.size()) {
      frame = &c.frames[c.frame];
      int top = c.pen_y;
      if (first_chunk) top += style.space_before;

      int available = frame->bottom() - top;
      int fits = available > 0 ? available / lh : 0;
      const size_t remaining = lines.size() - placed;

      // Orphan control: don't strand the opening line or two of a paragraph
      // at the foot of a column — move the whole thing on.
      if (first_chunk && style.min_orphan > 0 &&
          remaining > style.min_orphan &&
          fits < static_cast<int>(style.min_orphan) && c.dirty) {
        advance_frame(&c, tmpl, out);
        continue;
      }
      if (fits <= 0) {
        if (!c.dirty && c.frame == 0) {
          // A single element taller than an empty page: place what we can
          // rather than looping forever.
          fits = 1;
        } else {
          advance_frame(&c, tmpl, out);
          continue;
        }
      }

      size_t take = static_cast<size_t>(fits) < remaining
                        ? static_cast<size_t>(fits)
                        : remaining;
      // Widow control: never carry a single line over on its own.
      if (style.min_widow > 1 && remaining - take == 1 && take >= 2) {
        --take;
      }

      frame = &c.frames[c.frame];
      int y = top;
      for (size_t k = 0; k < take; ++k) {
        const BrokenLine& bl = lines[placed + k];
        Line out_line;
        out_line.baseline = y + ascent;

        const bool indent_this =
            (placed + k == 0) && !el.opens_story && style.indent > 0;
        const int x0 = frame->x + style.left_margin +
                       (indent_this ? style.indent : 0);

        if (!marker.empty() && placed + k == 0) {
          PositionedRun m;
          m.face = style.face;
          m.x = (frame->x + style.left_margin - 30) * kSubpixel;
          m.text = marker;
          out_line.runs.push_back(std::move(m));
        }
        for (const PositionedRun& r : bl.runs) {
          PositionedRun abs = r;
          abs.x = x0 * kSubpixel + r.x;
          out_line.runs.push_back(std::move(abs));
        }
        c.page.lines.push_back(std::move(out_line));
        y += lh;
      }

      c.pen_y = y;
      c.dirty = true;
      placed += take;
      first_chunk = false;

      if (placed < lines.size()) advance_frame(&c, tmpl, out);
    }

    c.pen_y += style.space_after;
  }

  finish_page(&c, out);
  return out->size() - before;
}

}  // namespace rsspaper
