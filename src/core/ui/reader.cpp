#include "core/ui/reader.h"

#include "core/ui/home.h"

#include <cstdlib>

#include "core/base/str.h"
#include "core/layout/type_scale.h"

namespace diarium {

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
  if (mode_ == ReaderMode::Sections) return false;

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

  if (page_ + 1 >= edition_.browse_page_count) return false;
  set_page(page_ + 1, false);
  return true;
}

bool Reader::previous_page() {
  if (mode_ == ReaderMode::Sections) return false;

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

// The top-right corner, big enough to find in the dark without looking.
bool Reader::in_light_corner(int x, int y) const {
  return x >= page_width() - 140 && y <= 140;
}

// The opposite corner, the same size. Two corners, two things you can always
// reach: the light, and the way out.
bool Reader::in_home_corner(int x, int y) const {
  return x <= 140 && y >= page_height() - 140;
}

void Reader::load_frontlight(const std::string& path) {
  light_path_ = path;
  std::string blob;
  if (hal_.storage != nullptr && hal_.storage->read(path, &blob)) {
    const int v = std::atoi(blob.c_str());
    if (v >= 0 && v <= 63) light_level_ = v;
    if (light_level_ > 0) light_last_on_ = light_level_;
  }
  if (hal_.display != nullptr) hal_.display->set_frontlight(light_level_);
}

void Reader::save_frontlight() {
  // Storage rather than RTC memory: a level that survives sleep but not a
  // battery pull is a setting that vanishes for no reason the reader can see.
  if (hal_.storage == nullptr || light_path_.empty()) return;
  hal_.storage->write(light_path_, std::to_string(light_level_));
}

// Returns whether the gesture was the light's. Changing the light needs no
// redraw — the light is its own feedback — so this never asks for one.
bool Reader::handle_frontlight(const GestureEvent& event) {
  if (hal_.display == nullptr) return false;
  if (!in_light_corner(event.x, event.y)) return false;

  if (event.kind == Gesture::Tap) {
    light_level_ = light_level_ > 0 ? 0 : light_last_on_;
  } else if (event.kind == Gesture::LongPress) {
    // Steps round rather than stopping at the top, so one gesture reaches
    // every level without needing a second one to go back down.
    const int step = hal_.display->max_frontlight() / 8;
    light_level_ += step > 0 ? step : 1;
    if (light_level_ > hal_.display->max_frontlight()) light_level_ = 0;
  } else {
    return false;
  }

  if (light_level_ > 0) light_last_on_ = light_level_;
  hal_.display->set_frontlight(light_level_);
  save_frontlight();
  return true;
}

bool Reader::handle(const GestureEvent& event) {
  // The light is reachable from wherever you are, including mid-article.
  if (handle_frontlight(event)) return false;

  // So is the way out. A long press rather than a tap, because this is the
  // one gesture that abandons where you are, and the corner is furniture
  // rather than anything the page laid out.
  if (event.kind == Gesture::LongPress && in_home_corner(event.x, event.y)) {
    return go_home();
  }

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
      return mode_ == ReaderMode::Sections ? back() : false;
    case Gesture::Tap:
      if (mode_ == ReaderMode::Sections) {
        const size_t rows = edition_.section_marks.size();
        const size_t index = section_row_at(event.y);
        if (index < rows) {
          confirm_mark_all_ = false;
          return jump_to_section(index);
        }
        if (index == rows) return mark_everything_read();
        confirm_mark_all_ = false;
        return back();
      }
      if (mode_ == ReaderMode::Story) return false;
      return open_story_at(event.x, event.y);
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

size_t Reader::section_row_at(int y) const {
  if (y < kOverlayFirstY) return static_cast<size_t>(-1);
  return static_cast<size_t>((y - kOverlayFirstY) / kOverlayRowHeight);
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
    fb.fill_rect(kSideMargin, y, page_width() - 2 * kSideMargin, 2, kInk);
  }

  y = kOverlayFirstY;
  const int row_height = kOverlayRowHeight;
  for (size_t i = 0; i < edition_.section_marks.size(); ++i) {
    const Edition::SectionMark& m = edition_.section_marks[i];
    if (y + row_height > page_height() - 60) break;
    if (body.valid()) {
      fb.draw_text(body, m.name, kSideMargin * kSubpixel, y + body.ascent(),
                   kInk);
    }
    if (meta.valid()) {
      const std::string page = "page " + std::to_string(m.first_page + 1);
      const int w = meta.measure(page);
      fb.draw_text(meta, page, (page_width() - kSideMargin) * kSubpixel - w,
                   y + body.ascent(), 90);
    }
    y += row_height;
    fb.fill_rect(kSideMargin, y - 18, page_width() - 2 * kSideMargin, 1, 190);
  }

  // And below the sections, the way out of a backlog. A second tap confirms,
  // because a mis-tap that silently discards a week of unread news would be
  // worse than any modal.
  if (body.valid() && y + row_height <= page_height() - 60) {
    const size_t left = unread_remaining();
    const std::string label =
        confirm_mark_all_ ? "Tap again to mark everything read"
                          : "Mark everything read";
    fb.draw_text(body, label, kSideMargin * kSubpixel, y + body.ascent(),
                 left == 0 ? 150 : kInk);
  }

  if (meta.valid()) {
    const std::string hint = "Swipe up or tap the heading to go back";
    fb.draw_text(meta, hint, kSideMargin * kSubpixel, page_height() - 40, 120);
  }
}

void Reader::render() {
  if (hal_.display == nullptr) return;
  needs_render_ = false;

  if (mode_ == ReaderMode::Sections) {
    render_section_overlay();
  } else if (mode_ == ReaderMode::Finished) {
    render_finished();
  } else if (mode_ == ReaderMode::Browse && page_ == 0) {
    // Page one is drawn rather than composed: it is a view of what is left to
    // read, and that changes as you read while the edition does not. The
    // pages behind it are the composed ledes, still there to be flipped
    // through.
    std::vector<bool> unread;
    unread.reserve(order_.size());
    for (size_t i = 0; i < order_.size(); ++i) {
      unread.push_back(!read_.has(edition_.stories[order_[i]].key));
    }
    render_home(fonts_, edition_, order_, unread, "composed on device",
               &hal_.display->framebuffer());
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

  render_battery_mark();

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

// Two taps, not one: the first arms it and says so, the second does it.
bool Reader::mark_everything_read() {
  if (!confirm_mark_all_) {
    confirm_mark_all_ = true;
    needs_render_ = true;
    return true;
  }
  confirm_mark_all_ = false;

  for (const StoryRef& s : edition_.stories) read_.mark(s.key, edition_.date);
  if (hal_.storage != nullptr && !read_path_.empty()) {
    hal_.storage->write(read_path_, serialize_seen_store(read_));
  }

  mode_ = ReaderMode::Finished;
  needs_render_ = true;
  pending_context_change_ = true;
  return true;
}

// The contents page is home: a view of what is left to read. This is not a
// "back" — reading is a line and there is no stack to pop — so it does not
// restore where you were, it puts the paper down. Nothing becomes unread by
// coming here, so swiping onward resumes at the oldest story still
// outstanding, which is the one after whatever was on screen.
bool Reader::go_home() {
  if (mode_ == ReaderMode::Browse && page_ == 0) return false;
  confirm_mark_all_ = false;
  mode_ = ReaderMode::Browse;
  have_story_ = false;
  set_page(0, true);
  return true;
}

void Reader::render_battery_mark() {
  if (hal_.power == nullptr || hal_.display == nullptr) return;
  const int mv = hal_.power->battery_millivolts();
  // A reading of zero means no measurement rather than an empty cell.
  if (mv <= 0 || mv >= policy_.low_battery_mv) return;

  Framebuffer& fb = hal_.display->framebuffer();

  // Centred in the bottom margin, on the folio's line. The margins themselves
  // are taken: folio_left sits against the left margin and folio_right is
  // right-aligned against the other, so the middle is the only space that is
  // reliably empty on every page.
  const int w = 26;
  const int h = 13;
  const int x = (page_width() - w) / 2;
  const int y = page_height() - 30;

  fb.frame_rect(x, y, w, h, kInk);
  fb.fill_rect(x + w, y + 4, 3, h - 8, kInk);  // the terminal
  fb.fill_rect(x + 3, y + 3, 5, h - 6, kInk);  // what is left of the charge
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
  fb.fill_rect(kSideMargin, y, page_width() - 2 * kSideMargin, 2, kInk);
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

}  // namespace diarium
