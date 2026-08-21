#include "core/ui/reader.h"

#include "core/ui/home.h"

#include <cstdlib>

#include "core/base/str.h"
#include "core/layout/type_scale.h"

namespace diarium {

Reader::Reader(const Edition& edition, const FontPack& fonts, Hal hal,
               ReaderPolicy policy)
    : edition_(&edition),
      fonts_(fonts),
      renderer_(fonts),
      hal_(hal),
      policy_(policy) {}

Reader::Reader(StreamingEditionReader& stream, const FontPack& fonts, Hal hal,
               ReaderPolicy policy)
    : edition_(&owned_shell_),
      stream_(&stream),
      fonts_(fonts),
      renderer_(fonts),
      hal_(hal),
      policy_(policy) {
  // The index only — title, date, and every StoryRef — never a pages list.
  // reading_order(), the home dashboard and every metadata lookup below
  // work on this exactly as they would on a whole resident Edition; only
  // the article render path (current_render_page) and story navigation
  // (show_article_at) know pages come from stream_ instead.
  owned_shell_.date = stream.date();
  owned_shell_.title = stream.title();
  owned_shell_.stats = stream.stats();
  owned_shell_.stories.reserve(stream.index().size());
  for (const StreamIndexEntry& e : stream.index()) {
    owned_shell_.stories.push_back(e.ref);
  }
}

const StoryRef* Reader::open_story() const {
  if (!have_story_ || story_index_ >= edition_->stories.size()) return nullptr;
  return &edition_->stories[story_index_];
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
  // Streaming: current_pages_ already holds whatever story show_article_at
  // just loaded (or is empty before any story has been opened), so it is
  // the bound in that mode — edition_->pages is deliberately never
  // populated when stream_ is set.
  const size_t bound =
      stream_ != nullptr ? current_pages_.size() : edition_->pages.size();
  if (page >= bound) return;
  page_ = page;
  needs_render_ = true;
  pending_context_change_ = pending_context_change_ || context_change;
}

const Page* Reader::current_render_page() const {
  const std::vector<Page>& pages = stream_ != nullptr ? current_pages_ : edition_->pages;
  if (page_ >= pages.size()) return nullptr;
  return &pages[page_];
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
  // news, and down keeps reading further into the article while up steps
  // back a page.
  if (mode_ == ReaderMode::Article) {
    switch (event.kind) {
      case Gesture::SwipeRight:
        return next_article();
      case Gesture::SwipeLeft:
        return previous_article();
      case Gesture::SwipeDown:
        return scroll_down();
      case Gesture::SwipeUp:
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

  // Home: rightwards is into the news. A tap arms clearing the whole
  // backlog and a second tap fires it — see `mark_everything_read()`. Any
  // swipe cancels a pending arm first, so turning away from it and coming
  // back does not leave a mis-tap away from clearing everything.
  if (event.kind == Gesture::Tap) return mark_everything_read();
  const bool was_armed = confirm_mark_all_;
  confirm_mark_all_ = false;
  if (event.kind == Gesture::SwipeRight) return begin_reading();
  if (was_armed) needs_render_ = true;
  return was_armed;
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

void Reader::render() {
  if (hal_.display == nullptr) return;
  needs_render_ = false;

  if (mode_ == ReaderMode::Finished) {
    render_finished();
  } else if (mode_ == ReaderMode::Home) {
    // Home is drawn rather than composed: it is a view of what is left to
    // read, and that changes as you read while the edition does not.
    std::vector<bool> unread;
    unread.reserve(order_.size());
    for (size_t i = 0; i < order_.size(); ++i) {
      unread.push_back(!read_.has(edition_->stories[order_[i]].key));
    }
    render_home(fonts_, *edition_, order_, unread, "composed on device",
                &hal_.display->framebuffer(), confirm_mark_all_);
  } else {
    const Page* p = current_render_page();
    if (p == nullptr) return;
    renderer_.render(*p, &hal_.display->framebuffer());
  }

  render_battery_mark();

  const bool context_change = pending_context_change_;
  pending_context_change_ = false;
  hal_.display->flush(choose_refresh(context_change));
}

std::string Reader::position() const {
  if (mode_ == ReaderMode::Article && order_pos_ < order_.size()) {
    const StoryRef& s = edition_->stories[order_[order_pos_]];
    return "article " + std::to_string(order_pos_ + 1) + "/" +
           std::to_string(order_.size()) + " page " +
           std::to_string(article_page_ + 1) + "/" +
           std::to_string(s.page_count) + " — " + s.title;
  }
  if (mode_ == ReaderMode::Finished) return "no more news";
  return "home";
}


// ---------------------------------------------------------------------------
// The continuous pass: every unread article, oldest first, one swipe at a
// time. This is the reading model — the ledes are an overview you land on,
// not a place you keep coming back to.
// ---------------------------------------------------------------------------

void Reader::load_read_state(const std::string& path) {
  read_path_ = path;
  order_ = edition_->reading_order();
  if (hal_.storage == nullptr) return;
  std::string blob;
  if (!hal_.storage->read(path, &blob)) return;  // nothing read yet
  read_.deserialize(blob, edition_->date);
}

size_t Reader::unread_remaining() const {
  size_t n = 0;
  for (size_t i = 0; i < order_.size(); ++i) {
    if (!read_.has(edition_->stories[order_[i]].key)) ++n;
  }
  return n;
}

void Reader::mark_current_read() {
  if (order_pos_ >= order_.size()) return;
  const StoryRef& s = edition_->stories[order_[order_pos_]];
  if (!read_.mark(s.key, edition_->date)) return;  // already read
  if (hal_.storage != nullptr && !read_path_.empty()) {
    hal_.storage->write(read_path_, serialize_seen_store(read_));
  }
}

bool Reader::show_article_at(size_t order_pos, bool context_change) {
  // A loop, not a single attempt: a corrupt or failed slice on a real card
  // (load_story_pages returning empty for a story whose metadata says it
  // has pages) must not strand the reader on a blank, unflushed panel, and
  // must not mark a story the reader never actually showed as read — so a
  // failed load is treated as if that position had nothing to show, and the
  // search moves on to the next one exactly the way running out of order_
  // already did.
  while (order_pos < order_.size()) {
    const StoryRef& s = edition_->stories[order_[order_pos]];
    if (s.page_count == 0) return false;

    std::vector<Page> loaded;
    if (stream_ != nullptr) {
      loaded = stream_->load_story_pages(order_[order_pos]);
      if (loaded.empty()) {
        ++order_pos;
        continue;
      }
    }

    order_pos_ = order_pos;
    article_page_ = 0;
    mode_ = ReaderMode::Article;
    story_index_ = order_[order_pos];
    have_story_ = true;
    // Streaming: the pages just loaded above, replacing whatever the
    // previous story left in current_pages_ — never more than one story
    // resident. Whole-Edition mode has nothing to load; the pages are
    // already there.
    if (stream_ != nullptr) current_pages_ = std::move(loaded);
    set_page(s.first_page, context_change);
    mark_current_read();
    return true;
  }

  mode_ = ReaderMode::Finished;
  needs_render_ = true;
  pending_context_change_ = true;
  return true;
}

// Enters the pass at the oldest thing not yet read.
bool Reader::begin_reading() {
  for (size_t i = 0; i < order_.size(); ++i) {
    if (!read_.has(edition_->stories[order_[i]].key)) {
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
    if (!read_.has(edition_->stories[order_[i]].key)) {
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
  const StoryRef& s = edition_->stories[order_[order_pos_]];
  if (static_cast<size_t>(article_page_) + 1 >= s.page_count) return false;
  ++article_page_;
  set_page(s.first_page + static_cast<size_t>(article_page_), false);
  return true;
}

bool Reader::scroll_up() {
  if (order_pos_ >= order_.size() || article_page_ == 0) return false;
  --article_page_;
  const StoryRef& s = edition_->stories[order_[order_pos_]];
  set_page(s.first_page + static_cast<size_t>(article_page_), false);
  return true;
}

// Two taps, not one: the first arms it and says so, the second does it.
// Driven from home, which is where the count being cleared is visible —
// this does not move the reader on its own; it stays wherever it was
// called from, and home simply recomputes to zero unread on the next
// render.
bool Reader::mark_everything_read() {
  if (!confirm_mark_all_) {
    confirm_mark_all_ = true;
    needs_render_ = true;
    return true;
  }
  confirm_mark_all_ = false;

  for (const StoryRef& s : edition_->stories) read_.mark(s.key, edition_->date);
  if (hal_.storage != nullptr && !read_path_.empty()) {
    hal_.storage->write(read_path_, serialize_seen_store(read_));
  }

  needs_render_ = true;
  pending_context_change_ = true;
  return true;
}

// Home is a view of what is left to read. This is not a "back" — reading is
// a line and there is no stack to pop — so it does not restore where you
// were, it puts the paper down. Nothing becomes unread by coming here, so
// swiping onward resumes at the oldest story still outstanding, which is the
// one after whatever was on screen.
bool Reader::go_home() {
  if (mode_ == ReaderMode::Home) return false;
  mode_ = ReaderMode::Home;
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
