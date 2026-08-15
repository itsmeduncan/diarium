// The reader: the loop that makes an edition readable.
//
// Portable, and driven entirely through the HAL, so the same code runs in the
// simulator and on the panel. It owns exactly three ideas:
//
//   where you are      — browsing the ledes, inside a story, or looking at the
//                        section list
//   what a gesture     — a page turn, opening a story, going back
//   means here
//   when to refresh    — partial for a page turn, full when ghosting has had
//                        time to build up or the context changed
//
// It does not own the edition, the fonts, or the display buffer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/edition/edition.h"
#include "core/render/page_renderer.h"
#include "core/text/font_pack.h"
#include "core/ui/gesture.h"
#include "hal/hal.h"

namespace rsspaper {

enum class ReaderMode : uint8_t {
  Browse,    // the ledes: front page and section pages
  Story,     // the full text of a story you opened
  Sections,  // the jump list
  End,       // you have reached the end of the paper
};

struct ReaderPolicy {
  // Partial refreshes between full ones. E-ink ghosting accumulates; this is
  // the number of page turns before the panel gets cleaned up. Tune against
  // real hardware, not a simulator.
  int partial_turns_before_full = 6;
  // Opening a story or coming back always gets a full refresh: the whole page
  // changes, and partial-refreshing a wholly different page looks like dirt.
  bool full_refresh_on_context_change = true;
};

class Reader {
 public:
  Reader(const Edition& edition, const FontPack& fonts, Hal hal,
         ReaderPolicy policy = ReaderPolicy());

  // Draws the current page and pushes it to the panel.
  void render();

  // Applies a gesture. Returns true if anything changed and a render is due.
  bool handle(const GestureEvent& event);

  // Convenience for a device main loop: poll input, recognise, handle,
  // render if needed. Returns true if the display was updated.
  bool tick();

  ReaderMode mode() const { return mode_; }
  size_t current_page() const { return page_; }
  const StoryRef* open_story() const;

  // A one-line description of where the reader is, for logs and the
  // simulator. Not shown on the panel.
  std::string position() const;

  // Navigation, exposed so the simulator can drive it from the keyboard and
  // tests can drive it directly.
  bool next_page();
  bool previous_page();
  bool open_story_at(int x, int y);
  bool back();
  bool toggle_sections();
  bool jump_to_section(size_t index);

 private:
  void set_page(size_t page, bool context_change);
  RefreshMode choose_refresh(bool context_change);
  void render_section_overlay();

  const Edition& edition_;
  const FontPack& fonts_;
  PageRenderer renderer_;
  Hal hal_;
  ReaderPolicy policy_;
  GestureRecognizer gestures_;

  ReaderMode mode_ = ReaderMode::Browse;
  size_t page_ = 0;
  // Where to return to when a story is closed.
  size_t return_page_ = 0;
  size_t story_index_ = 0;
  bool have_story_ = false;
  int partials_since_full_ = 0;
  bool needs_render_ = true;
  // Set when the next render shows a wholly different page, so it gets a full
  // refresh rather than partial-refreshing one page over another.
  bool pending_context_change_ = true;
};

}  // namespace rsspaper
