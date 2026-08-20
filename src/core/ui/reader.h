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

#include "core/edition/clippings.h"
#include "core/edition/seen_store.h"
#include "core/edition/edition.h"
#include "core/render/page_renderer.h"
#include "core/text/font_pack.h"
#include "core/ui/gesture.h"
#include "hal/hal.h"

namespace rsspaper {

enum class ReaderMode : uint8_t {
  Browse,     // the overview: the front page you land on
  Article,    // one article in a continuous oldest-first pass
  Story,      // the full text of a story opened from a lede
  Sections,   // the jump list
  Clippings,  // stories you folded the corner of
  Finished,   // the news ran out
};

// The last browse page is the colophon: it says the paper has ended, and why
// it is the length it is. That is a composed page rather than a special screen
// so it persists with the edition and needs no rendering of its own.

struct ReaderPolicy {
  // Partial refreshes between full ones. E-ink ghosting accumulates; this is
  // the number of page turns before the panel gets cleaned up.
  //
  // Measured on a 6FLICK: smearing becomes visible around the seventh page,
  // so six partials cleaned up exactly one turn too late. Four keeps the
  // panel ahead of it, at about 75 ms more per turn averaged over the cycle —
  // a partial costs ~0.52 s and a full ~1.83 s.
  int partial_turns_before_full = 4;
  // Opening a story or coming back always gets a full refresh: the whole page
  // changes, and partial-refreshing a wholly different page looks like dirt.
  bool full_refresh_on_context_change = true;

  // Below this, the paper says so. A guess until someone runs a cell down and
  // measures where "one more edition left" actually falls: lithium discharge
  // is flat through most of its range, which is exactly why a percentage
  // would be a lie.
  int low_battery_mv = 3500;
};

class Reader {
 public:
  Reader(const Edition& edition, const FontPack& fonts, Hal hal,
         ReaderPolicy policy = ReaderPolicy());

  // Loads saved clippings from storage. Absent or unreadable is not an error:
  // you simply have none.
  void load_clippings(const std::string& path);
  const class Clippings& clippings() const { return clippings_; }

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
  bool toggle_clippings_view();
  // Folds the corner of whatever is under the finger, or of the open story.
  // Returns true if it is now saved.
  bool toggle_clipping_at(int x, int y);

  // The continuous pass: every article you have not read, oldest first, one
  // swipe at a time until there are none left.
  bool begin_reading();
  bool next_article();
  bool previous_article();
  bool scroll_down();
  bool scroll_up();
  // Stories already read, so a second pass does not show them again.
  void load_read_state(const std::string& path);

 private:
  void mark_current_read();
  // A discreet mark in the furniture when the battery is genuinely low. No
  // percentage, no icon that animates, and nothing at all when it is fine.
  void render_battery_mark();
  void render_finished();
  size_t unread_remaining() const;

  // Position within the oldest-first order, and the page within the article
  // at that position. Scrolling an article is moving through its pages.
  bool show_article_at(size_t order_pos, bool context_change);

  void set_page(size_t page, bool context_change);
  RefreshMode choose_refresh(bool context_change);
  void render_section_overlay();
  void render_clippings();
  // Row geometry, shared by hit-testing and drawing so a tap can never
  // land on a different row than the one under the finger.
  size_t section_row_at(int y) const;
  size_t clipping_row_at(int y) const;
  void save_clippings();
  Clipping clipping_for(const StoryRef& story) const;

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

  // The oldest-first walk. `order_` is story indices; `order_pos_` is where
  // the reader has got to.
  std::vector<size_t> order_;
  size_t order_pos_ = 0;
  int article_page_ = 0;
  SeenStore read_;
  std::string read_path_;
  class Clippings clippings_;
  std::string clippings_path_;
  int partials_since_full_ = 0;
  bool needs_render_ = true;
  // Set when the next render shows a wholly different page, so it gets a full
  // refresh rather than partial-refreshing one page over another.
  bool pending_context_change_ = true;
};

}  // namespace rsspaper
