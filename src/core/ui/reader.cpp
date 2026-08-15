#include "core/ui/reader.h"

#include "core/base/str.h"
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

  if (page_ + 1 >= edition_.browse_page_count) {
    // The last page of the paper. Say so rather than silently doing nothing —
    // "you are finished" is the whole product.
    if (mode_ != ReaderMode::End) {
      mode_ = ReaderMode::End;
      needs_render_ = true;
      pending_context_change_ = true;
      return true;
    }
    return false;
  }
  set_page(page_ + 1, false);
  return true;
}

bool Reader::previous_page() {
  if (mode_ == ReaderMode::Sections) return false;

  if (mode_ == ReaderMode::End) {
    mode_ = ReaderMode::Browse;
    needs_render_ = true;
    pending_context_change_ = true;
    return true;
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
  if (mode_ == ReaderMode::Sections) {
    mode_ = have_story_ ? ReaderMode::Story : ReaderMode::Browse;
    needs_render_ = true;
    pending_context_change_ = true;
    return true;
  }
  if (mode_ == ReaderMode::End) {
    mode_ = ReaderMode::Browse;
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

bool Reader::handle(const GestureEvent& event) {
  switch (event.kind) {
    case Gesture::SwipeLeft:
      return next_page();
    case Gesture::SwipeRight:
      return previous_page();
    case Gesture::SwipeDown:
      return toggle_sections();
    case Gesture::SwipeUp:
      return mode_ == ReaderMode::Sections ? back() : false;
    case Gesture::Tap:
      if (mode_ == ReaderMode::Sections) {
        // The overlay lists sections down the page; pick by row.
        const int row_height = 64;
        const int first_y = 180;
        if (event.y >= first_y) {
          const size_t index =
              static_cast<size_t>((event.y - first_y) / row_height);
          return jump_to_section(index);
        }
        return back();
      }
      if (mode_ == ReaderMode::Story) return false;
      return open_story_at(event.x, event.y);
    case Gesture::LongPress:
      // Clippings land here (issue #10). Deliberately inert for now rather
      // than doing something surprising.
      return false;
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

  y = 180;
  const int row_height = 64;
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
  } else if (mode_ == ReaderMode::End) {
    Framebuffer& fb = hal_.display->framebuffer();
    fb.fill(kPaper);
    const Face& head = fonts_.face(FaceId::Head);
    const Face& meta = fonts_.face(FaceId::Meta);
    if (head.valid()) {
      const std::string done = "That's the paper.";
      const int w = head.measure(done);
      fb.draw_text(head, done, (kPageWidth * kSubpixel - w) / 2,
                   kPageHeight / 2, kInk);
    }
    if (meta.valid()) {
      const std::string sub =
          std::to_string(edition_.stats.items_published) + " stories · " +
          format_masthead_date(edition_.date);
      const int w = meta.measure(sub);
      fb.draw_text(meta, sub, (kPageWidth * kSubpixel - w) / 2,
                   kPageHeight / 2 + 60, 110);
    }
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
  switch (mode_) {
    case ReaderMode::Sections:
      return "sections";
    case ReaderMode::End:
      return "end of the paper";
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

}  // namespace rsspaper
