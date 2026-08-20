#include "core/ui/reader.h"

#include "core/base/str.h"
#include "core/edition/clippings.h"
#include "core/layout/type_scale.h"

namespace rsspaper {

Reader::Reader(const Edition& edition, const FontPack& fonts, Hal hal,
               ReaderPolicy policy)
    : edition_(edition),
      fonts_(fonts),
      renderer_(fonts),
      hal_(hal),
      policy_(policy) {}

const StoryRef* Reader::open_story() const {
  if (!have_story_ || story_index_ >= edition_.stories.size()) return nullptr;
  return &edition_.stories[story_index_];
}

RefreshMode Reader::choose_refresh(bool context_change) {
  if (context_change && policy_.full_refresh_on_context_change) {
    partials_since_full_ = 0;
    return RefreshMode::Full;
  }
  if (partials_since_full_ >= policy_.partial_turns_before_full) {
    partials_since_full_ = 0;
    return RefreshMode::Full;
  }
  ++partials_since_full_;
  return RefreshMode::Partial;
}

void Reader::set_page(size_t page, bool context_change) {
  if (page >= edition_.pages.size()) return;
  page_ = page;
  needs_render_ = true;
  pending_context_change_ = pending_context_change_ || context_change;
}

bool Reader::next_page() {
  if (mode_ == ReaderMode::Sections || mode_ == ReaderMode::Clippings) {
    return false;
  }

  if (mode_ == ReaderMode::Story) {
    const StoryRef* s = open_story();
    if (s == nullptr) return back();
    const size_t last = s->first_page + s->page_count - 1;
    if (page_ >= last) {
      // The end of a story returns you to where you chose it. A story is not
      // a place you fall out of into the next story's text.
      return back();
    }
    set_page(page_ + 1, false);
    return true;
  }

  // The last browse page is the colophon, which already says the paper has
  // ended. There is nothing after it.
  if (page_ + 1 >= edition_.browse_page_count) return false;
  set_page(page_ + 1, false);
  return true;
}

bool Reader::previous_page() {
  if (mode_ == ReaderMode::Sections || mode_ == ReaderMode::Clippings) {
    return false;
  }

  if (mode_ == ReaderMode::Story) {
    const StoryRef* s = open_story();
    if (s == nullptr) return back();
    if (page_ <= s->first_page) return back();
    set_page(page_ - 1, false);
    return true;
  }

  if (page_ == 0) return false;
  set_page(page_ - 1, false);
  return true;
}

bool Reader::open_story_at(int x, int y) {
  if (mode_ != ReaderMode::Browse) return false;
  const StoryRef* hit = edition_.story_at(page_, x, y);
  if (hit == nullptr || hit->page_count == 0) return false;

  for (size_t i = 0; i < edition_.stories.size(); ++i) {
    if (&edition_.stories[i] == hit) {
      story_index_ = i;
      break;
    }
  }
  have_story_ = true;
  return_page_ = page_;
  mode_ = ReaderMode::Story;
  set_page(hit->first_page, true);
  return true;
}

bool Reader::back() {
  if (mode_ == ReaderMode::Clippings) {
    mode_ = ReaderMode::Browse;
    needs_render_ = true;
    pending_context_change_ = true;
    return true;
  }
  if (mode_ == ReaderMode::Sections) {
    mode_ = have_story_ ? ReaderMode::Story : ReaderMode::Browse;
    needs_render_ = true;
    pending_context_change_ = true;
    return true;
  }
  if (mode_ != ReaderMode::Story) return false;

  mode_ = ReaderMode::Browse;
  have_story_ = false;
  set_page(return_page_, true);
  return true;
}

void Reader::load_clippings(const std::string& path) {
  clippings_path_ = path;
  if (hal_.storage == nullptr) return;
  std::string blob;
  if (!hal_.storage->read(path, &blob)) return;  // none saved yet
  std::string error;
  Clippings loaded;
  if (deserialize_clippings(blob, &loaded, &error)) clippings_ = loaded;
}

void Reader::save_clippings() {
  if (hal_.storage == nullptr || clippings_path_.empty()) return;
  hal_.storage->write(clippings_path_, serialize_clippings(clippings_));
}

Clipping Reader::clipping_for(const StoryRef& story) const {
  Clipping c;
  c.key = story.key;
  c.title = story.title;
  c.section = story.section;
  c.source = story.source;
  c.saved = hal_.clock != nullptr ? hal_.clock->now() : edition_.date;
  c.published = edition_.date;
  return c;
}

bool Reader::toggle_clipping_at(int x, int y) {
  const StoryRef* target = nullptr;
  if (mode_ == ReaderMode::Story) {
    target = open_story();
  } else if (mode_ == ReaderMode::Browse) {
    target = edition_.story_at(page_, x, y);
  } else if (mode_ == ReaderMode::Clippings) {
    // In the list, a long press takes it back out again.
    const size_t row = clipping_row_at(y);
    if (row < clippings_.all().size()) {
      clippings_.remove(clippings_.all()[row].key);
      save_clippings();
      needs_render_ = true;
      pending_context_change_ = true;
    }
    return false;
  }
  if (target == nullptr) return false;

  const bool saved = clippings_.toggle(clipping_for(*target));
  save_clippings();
  needs_render_ = true;
  pending_context_change_ = true;
  return saved;
}

bool Reader::toggle_clippings_view() {
  if (mode_ == ReaderMode::Clippings) {
    mode_ = ReaderMode::Browse;
    needs_render_ = true;
    pending_context_change_ = true;
    return true;
  }
  mode_ = ReaderMode::Clippings;
  needs_render_ = true;
  pending_context_change_ = true;
  return true;
}

bool Reader::toggle_sections() {
  if (mode_ == ReaderMode::Sections) return back();
  if (edition_.section_marks.empty()) return false;
  mode_ = ReaderMode::Sections;
  needs_render_ = true;
  pending_context_change_ = true;
  return true;
}

bool Reader::jump_to_section(size_t index) {
  if (index >= edition_.section_marks.size()) return false;
  mode_ = ReaderMode::Browse;
  have_story_ = false;
  set_page(edition_.section_marks[index].first_page, true);
  return true;
}

bool Reader::handle(const GestureEvent& event) {
  // The continuous pass has its own gestures: right goes onward through the
  // news, and up and down move within the article you are on.
  if (mode_ == ReaderMode::Article) {
    switch (event.kind) {
      case Gesture::SwipeRight:
        return next_article();
      case Gesture::SwipeLeft:
        return previous_article();
      case Gesture::SwipeUp:
        return scroll_down();
      case Gesture::SwipeDown:
        return scroll_up();
      case Gesture::LongPress:
        toggle_clipping_at(event.x, event.y);
        return true;
      default:
        return false;
    }
  }

  if (mode_ == ReaderMode::Finished) {
    // Backwards out of the end, in case it arrived sooner than expected.
    if (event.kind == Gesture::SwipeLeft) return previous_article();
    return false;
  }

  switch (event.kind) {
    case Gesture::SwipeLeft:
      return next_page();
    case Gesture::SwipeRight:
      // From the overview, rightwards is into the news rather than backwards
      // through pages you have already passed.
      return mode_ == ReaderMode::Browse ? begin_reading() : previous_page();
    case Gesture::SwipeDown:
      return toggle_sections();
    case Gesture::SwipeUp:
      return (mode_ == ReaderMode::Sections || mode_ == ReaderMode::Clippings)
                 ? back()
                 : false;
    case Gesture::Tap:
      if (mode_ == ReaderMode::Sections) {
        const size_t rows = edition_.section_marks.size();
        const size_t index = section_row_at(event.y);
        if (index == rows) return toggle_clippings_view();  // the last row
        if (index < rows) return jump_to_section(index);
        return back();
      }
      if (mode_ == ReaderMode::Clippings) {
        const size_t row = clipping_row_at(event.y);
        if (row >= clippings_.all().size()) return back();
        // Open it if this edition still carries the story; a clipping from
        // last Tuesday has a headline but nothing to turn to.
        const uint64_t key = clippings_.all()[row].key;
        for (size_t i = 0; i < edition_.stories.size(); ++i) {
          if (edition_.stories[i].key != key) continue;
          story_index_ = i;
          have_story_ = true;
          return_page_ = edition_.stories[i].lede_page;
          mode_ = ReaderMode::Story;
          set_page(edition_.stories[i].first_page, true);
          return true;
        }
        return false;
      }
      if (mode_ == ReaderMode::Story) return false;
      return open_story_at(event.x, event.y);
    case Gesture::LongPress:
      toggle_clipping_at(event.x, event.y);
      return true;
    case Gesture::None:
      return false;
  }
  return false;
}

bool Reader::tick() {
  if (hal_.input != nullptr) {
    TouchPoint points[2];
    const size_t n = hal_.input->poll(points, 2);
    const GestureEvent event = gestures_.update(
        n > 0, n > 0 ? points[0].x : 0, n > 0 ? points[0].y : 0,
        hal_.input->millis());
    if (event.kind != Gesture::None) handle(event);
  }
  if (!needs_render_) return false;
  render();
  return true;
}

namespace {
constexpr int kOverlayFirstY = 180;
constexpr int kOverlayRowHeight = 64;
// A clipping is two lines — headline and provenance — so its rows are taller
// than a section's, and the hit test has to use the same number the drawing
// does or a tap lands on the neighbour.
constexpr int kClippingRowHeight = 86;
}  // namespace

size_t Reader::section_row_at(int y) const {
  if (y < kOverlayFirstY) return static_cast<size_t>(-1);
  return static_cast<size_t>((y - kOverlayFirstY) / kOverlayRowHeight);
}

size_t Reader::clipping_row_at(int y) const {
  if (y < kOverlayFirstY) return static_cast<size_t>(-1);
  return static_cast<size_t>((y - kOverlayFirstY) / kClippingRowHeight);
}

void Reader::render_clippings() {
  Framebuffer& fb = hal_.display->framebuffer();
  fb.fill(kPaper);

  const Face& head = fonts_.face(FaceId::Head);
  const Face& body = fonts_.face(FaceId::BodyBold);
  const Face& meta = fonts_.face(FaceId::Meta);

  int y = 70;
  if (head.valid()) {
    fb.draw_text(head, "Clippings", kSideMargin * kSubpixel, y, kInk);
    y += head.descent() + 24;
    fb.fill_rect(kSideMargin, y, kPageWidth - 2 * kSideMargin, 2, kInk);
  }

  if (clippings_.empty() && meta.valid()) {
    fb.draw_text(meta,
                 "Nothing saved yet. Hold a story to fold its corner over.",
                 kSideMargin * kSubpixel, kOverlayFirstY + meta.ascent(), 110);
  }

  y = kOverlayFirstY;
  for (const Clipping& c : clippings_.all()) {
    if (y + kClippingRowHeight > kPageHeight - 60) break;
    if (body.valid()) {
      std::string title = c.title;
      // One line: the list is a list, not a page of headlines.
      while (!title.empty() &&
             body.measure(title) >
                 (kPageWidth - 2 * kSideMargin - 40) * kSubpixel) {
        title.resize(title.size() - 1);
      }
      if (title != c.title && title.size() > 1) {
        title.resize(title.size() - 1);
        title += "\xE2\x80\xA6";
      }
      fb.draw_text(body, title, kSideMargin * kSubpixel, y + body.ascent(),
                   kInk);
    }
    if (meta.valid()) {
      std::string sub = c.source;
      if (!c.section.empty()) sub += sub.empty() ? c.section : "  ·  " + c.section;
      if (c.saved != kNoDate) sub += "  ·  saved " + format_short_date(c.saved);
      fb.draw_text(meta, sub, kSideMargin * kSubpixel,
                   y + body.ascent() + meta.ascent() + 6, 100);
    }
    y += kClippingRowHeight;
    fb.fill_rect(kSideMargin, y - 22, kPageWidth - 2 * kSideMargin, 1, 205);
  }

  if (meta.valid()) {
    fb.draw_text(meta, "Hold a clipping to remove it  ·  swipe up to go back",
                 kSideMargin * kSubpixel, kPageHeight - 40, 120);
  }
}

void Reader::render_section_overlay() {
  Framebuffer& fb = hal_.display->framebuffer();
  fb.fill(kPaper);

  const Face& head = fonts_.face(FaceId::Head);
  const Face& body = fonts_.face(FaceId::BodyBold);
  const Face& meta = fonts_.face(FaceId::Meta);

  int y = 70;
  if (head.valid()) {
    fb.draw_text(head, "Sections", kSideMargin * kSubpixel, y, kInk);
    y += head.descent() + 24;
    fb.fill_rect(kSideMargin, y, kPageWidth - 2 * kSideMargin, 2, kInk);
  }

  y = kOverlayFirstY;
  const int row_height = kOverlayRowHeight;
  for (size_t i = 0; i < edition_.section_marks.size(); ++i) {
    const Edition::SectionMark& m = edition_.section_marks[i];
    if (y + row_height > kPageHeight - 60) break;
    if (body.valid()) {
      fb.draw_text(body, m.name, kSideMargin * kSubpixel, y + body.ascent(),
                   kInk);
    }
    if (meta.valid()) {
      const std::string page = "page " + std::to_string(m.first_page + 1);
      const int w = meta.measure(page);
      fb.draw_text(meta, page, (kPageWidth - kSideMargin) * kSubpixel - w,
                   y + body.ascent(), 90);
    }
    y += row_height;
    fb.fill_rect(kSideMargin, y - 18, kPageWidth - 2 * kSideMargin, 1, 190);
  }

  // Clippings sit at the end of the list: the only other place to go.
  if (body.valid() && y + row_height <= kPageHeight - 60) {
    const std::string label =
        "Clippings (" + std::to_string(clippings_.size()) + ")";
    fb.draw_text(body, label, kSideMargin * kSubpixel, y + body.ascent(), kInk);
  }

  if (meta.valid()) {
    const std::string hint = "Swipe up or tap the heading to go back";
    fb.draw_text(meta, hint, kSideMargin * kSubpixel, kPageHeight - 40, 120);
  }
}

void Reader::render() {
  if (hal_.display == nullptr) return;
  needs_render_ = false;

  if (mode_ == ReaderMode::Sections) {
    render_section_overlay();
  } else if (mode_ == ReaderMode::Clippings) {
    render_clippings();
  } else if (mode_ == ReaderMode::Finished) {
    render_finished();
  } else {
    if (page_ >= edition_.pages.size()) return;
    renderer_.render(edition_.pages[page_], &hal_.display->framebuffer());
    if (edition_.pages[page_].is_front_page) {
      MastheadInfo info;
      info.title = edition_.title;
      info.date_line = format_masthead_date(edition_.date);
      info.strap = std::to_string(edition_.stats.items_published) +
                   " stories · composed on device";
      renderer_.render_masthead(info, &hal_.display->framebuffer());
    }
  }

  const bool context_change = pending_context_change_;
  pending_context_change_ = false;
  hal_.display->flush(choose_refresh(context_change));
}

std::string Reader::position() const {
  if (mode_ == ReaderMode::Article && order_pos_ < order_.size()) {
    const StoryRef& s = edition_.stories[order_[order_pos_]];
    return "article " + std::to_string(order_pos_ + 1) + "/" +
           std::to_string(order_.size()) + " page " +
           std::to_string(article_page_ + 1) + "/" +
           std::to_string(s.page_count) + " — " + s.title;
  }
  if (mode_ == ReaderMode::Finished) return "no more news";
  switch (mode_) {
    case ReaderMode::Sections:
      return "sections";
    case ReaderMode::Clippings:
      return "clippings (" + std::to_string(clippings_.size()) + ")";
    case ReaderMode::Story: {
      const StoryRef* s = open_story();
      if (s == nullptr) return "story";
      return "story \"" + s->title + "\" page " +
             std::to_string(page_ - s->first_page + 1) + "/" +
             std::to_string(s->page_count);
    }
    case ReaderMode::Browse:
      break;
  }
  return "browsing page " + std::to_string(page_ + 1) + "/" +
         std::to_string(edition_.browse_page_count);
}


// ---------------------------------------------------------------------------
// The continuous pass: every unread article, oldest first, one swipe at a
// time. This is the reading model — the ledes are an overview you land on,
// not a place you keep coming back to.
// ---------------------------------------------------------------------------

void Reader::load_read_state(const std::string& path) {
  read_path_ = path;
  order_ = edition_.reading_order();
  if (hal_.storage == nullptr) return;
  std::string blob;
  if (!hal_.storage->read(path, &blob)) return;  // nothing read yet
  read_.deserialize(blob, edition_.date);
}

size_t Reader::unread_remaining() const {
  size_t n = 0;
  for (size_t i = 0; i < order_.size(); ++i) {
    if (!read_.has(edition_.stories[order_[i]].key)) ++n;
  }
  return n;
}

void Reader::mark_current_read() {
  if (order_pos_ >= order_.size()) return;
  const StoryRef& s = edition_.stories[order_[order_pos_]];
  if (!read_.mark(s.key, edition_.date)) return;  // already read
  if (hal_.storage != nullptr && !read_path_.empty()) {
    hal_.storage->write(read_path_, serialize_seen_store(read_));
  }
}

bool Reader::show_article_at(size_t order_pos, bool context_change) {
  if (order_pos >= order_.size()) {
    mode_ = ReaderMode::Finished;
    needs_render_ = true;
    pending_context_change_ = true;
    return true;
  }
  const StoryRef& s = edition_.stories[order_[order_pos]];
  if (s.page_count == 0) return false;

  order_pos_ = order_pos;
  article_page_ = 0;
  mode_ = ReaderMode::Article;
  story_index_ = order_[order_pos];
  have_story_ = true;
  set_page(s.first_page, context_change);
  mark_current_read();
  return true;
}

// Enters the pass at the oldest thing not yet read.
bool Reader::begin_reading() {
  for (size_t i = 0; i < order_.size(); ++i) {
    if (!read_.has(edition_.stories[order_[i]].key)) {
      return show_article_at(i, true);
    }
  }
  mode_ = ReaderMode::Finished;
  needs_render_ = true;
  pending_context_change_ = true;
  return true;
}

bool Reader::next_article() {
  for (size_t i = order_pos_ + 1; i < order_.size(); ++i) {
    if (!read_.has(edition_.stories[order_[i]].key)) {
      return show_article_at(i, true);
    }
  }
  // Nothing unread ahead: the news has run out.
  mode_ = ReaderMode::Finished;
  needs_render_ = true;
  pending_context_change_ = true;
  return true;
}

// Backwards goes to the previous article whether or not it has been read:
// having just read it is the usual reason to want it again.
bool Reader::previous_article() {
  if (order_pos_ == 0) return false;
  return show_article_at(order_pos_ - 1, true);
}

bool Reader::scroll_down() {
  if (order_pos_ >= order_.size()) return false;
  const StoryRef& s = edition_.stories[order_[order_pos_]];
  if (static_cast<size_t>(article_page_) + 1 >= s.page_count) return false;
  ++article_page_;
  set_page(s.first_page + static_cast<size_t>(article_page_), false);
  return true;
}

bool Reader::scroll_up() {
  if (order_pos_ >= order_.size() || article_page_ == 0) return false;
  --article_page_;
  const StoryRef& s = edition_.stories[order_[order_pos_]];
  set_page(s.first_page + static_cast<size_t>(article_page_), false);
  return true;
}

void Reader::render_finished() {
  Framebuffer& fb = hal_.display->framebuffer();
  fb.fill(kPaper);

  const Face& head = fonts_.face(FaceId::Head);
  const Face& body = fonts_.face(FaceId::Body);
  const Face& meta = fonts_.face(FaceId::Meta);

  int y = 260;
  if (head.valid()) {
    fb.draw_text(head, "That is all the news", kSideMargin * kSubpixel, y, kInk);
    y += head.descent() + 28;
  }
  fb.fill_rect(kSideMargin, y, kPageWidth - 2 * kSideMargin, 2, kInk);
  y += 56;

  if (body.valid()) {
    fb.draw_text(body, "You have read everything in this edition.",
                 kSideMargin * kSubpixel, y, kInk);
    y += 46;
  }
  if (meta.valid()) {
    fb.draw_text(meta, "The next paper arrives in the morning.",
                 kSideMargin * kSubpixel, y, 110);
  }
}

}  // namespace rsspaper
