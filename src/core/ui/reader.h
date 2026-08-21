// The reader: the loop that makes an edition readable.
//
// Portable, and driven entirely through the HAL, so the same code runs in the
// simulator and on the panel. It owns exactly three ideas:
//
//   where you are      — the home page, one article in the continuous pass,
//                        or the end of the news
//   what a gesture     — onward, back, scroll within an article, going home
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

#include "core/edition/seen_store.h"
#include "core/edition/edition.h"
#include "core/render/page_renderer.h"
#include "core/text/font_pack.h"
#include "core/ui/gesture.h"
#include "hal/hal.h"

namespace diarium {

enum class ReaderMode : uint8_t {
  Home,       // the summary you wake to
  Article,    // one article in a continuous oldest-first pass
  Finished,   // the news ran out
};

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

  // Draws the current page and pushes it to the panel.
  void render();

  // Applies a gesture. Returns true if anything changed and a render is due.
  bool handle(const GestureEvent& event);

  // Convenience for a device main loop: poll input, recognise, handle,
  // render if needed. Returns true if the display was updated.
  bool tick();

  ReaderMode mode() const { return mode_; }
  size_t current_page() const { return page_; }
  // The story the current article page belongs to, mid-pass. Set by whatever
  // last moved the reader into an article.
  const StoryRef* open_story() const;

  // A one-line description of where the reader is, for logs and the
  // simulator. Not shown on the panel.
  std::string position() const;

  // The continuous pass: every article you have not read, oldest first, one
  // swipe at a time until there are none left.
  bool begin_reading();
  bool next_article();
  bool previous_article();
  bool scroll_down();
  bool scroll_up();
  // Clears the whole backlog. Two taps: a carry-over pile with no exit is an
  // inbox, and an exit that fires on a mis-tap is worse.
  bool mark_everything_read();
  // Back to the home page from wherever you are. A long press in the
  // bottom-left corner, which is furniture rather than a control on the page.
  bool go_home();

  // The frontlight. A tap in the top corner switches it; a long press there
  // steps the brightness round. It is a light with a switch: nothing reacts
  // to the room and nothing ramps.
  void load_frontlight(const std::string& path);
  // Stories already read, so a second pass does not show them again.
  void load_read_state(const std::string& path);

 private:
  void mark_current_read();
  // A discreet mark in the furniture when the battery is genuinely low. No
  // percentage, no icon that animates, and nothing at all when it is fine.
  void render_battery_mark();
  bool in_light_corner(int x, int y) const;
  bool in_home_corner(int x, int y) const;
  bool handle_frontlight(const GestureEvent& event);
  void save_frontlight();
  void render_finished();
  size_t unread_remaining() const;

  // Position within the oldest-first order, and the page within the article
  // at that position. Scrolling an article is moving through its pages.
  bool show_article_at(size_t order_pos, bool context_change);

  void set_page(size_t page, bool context_change);
  RefreshMode choose_refresh(bool context_change);

  const Edition& edition_;
  const FontPack& fonts_;
  PageRenderer renderer_;
  Hal hal_;
  ReaderPolicy policy_;
  GestureRecognizer gestures_;

  ReaderMode mode_ = ReaderMode::Home;
  size_t page_ = 0;
  // Which story `page_` belongs to while in Article mode, and whether one is
  // set at all. Read by `open_story()`.
  size_t story_index_ = 0;
  bool have_story_ = false;

  // The oldest-first walk. `order_` is story indices; `order_pos_` is where
  // the reader has got to.
  std::vector<size_t> order_;
  size_t order_pos_ = 0;
  int article_page_ = 0;
  // Two-tap arm/fire state for `mark_everything_read()`.
  bool confirm_mark_all_ = false;
  std::string light_path_;
  int light_level_ = 0;   // 0 is off
  int light_last_on_ = 24;  // what "on" means when it is switched back on
  SeenStore read_;
  std::string read_path_;
  int partials_since_full_ = 0;
  bool needs_render_ = true;
  // Set when the next render shows a wholly different page, so it gets a full
  // refresh rather than partial-refreshing one page over another.
  bool pending_context_change_ = true;
};

}  // namespace diarium
