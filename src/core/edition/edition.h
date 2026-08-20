// Composing an edition: the step that turns a pile of items into a newspaper
// with a front page, sections, and a last page.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/base/datetime.h"
#include "core/feed/item.h"
#include "core/layout/page.h"
#include "core/text/font_pack.h"

namespace rsspaper {

struct Section {
  std::string name;
  std::vector<Item> items;
};

// A feed that didn't make it into the edition, and why. Today's paper being
// thinner than yesterday's is something the reader should be able to notice —
// silently omitting a section is exactly the failure a calm device makes
// invisible.
struct FeedProblem {
  std::string source;  // the feed's name, or its URL if we never got a name
  std::string reason;  // "timed out", "404", "no fixture" — short, for print
};

struct ComposeOptions {
  Epoch now = kNoDate;
  std::string title = "RSSpaper";
  // Hard ceiling on the whole edition. A paper that never ends is a feed.
  size_t max_items = 40;
  // Stories every section gets before any section gets more. Without this the
  // budget is spent in section order and the last section can vanish entirely.
  size_t min_per_section = 2;
  // Stories older than this are stale news, however recently they appeared in
  // the feed.
  int max_age_days = 3;
  int front_page_columns = 2;
  Align body_alignment = Align::Left;
  bool hyphenate = true;
  // How many section headlines the front page lists per section.
  size_t front_page_per_section = 2;
  // How many feeds were configured, and which of them failed. Both appear on
  // the colophon.
  size_t feeds_configured = 0;
  std::vector<FeedProblem> feed_problems;
};

// What the edition dropped and why. Printed by the simulator and worth
// surfacing on device too, because silently omitting stories is the kind of
// thing a reader should be able to notice.
struct ComposeStats {
  size_t items_in = 0;
  size_t dropped_seen = 0;
  size_t dropped_stale = 0;
  size_t dropped_over_budget = 0;
  size_t items_published = 0;
  // Front-page teasers that did not fit on the single front page.
  size_t front_page_overflow = 0;
  size_t truncated_published = 0;
};

// One story's place in the edition.
//
// An edition has two kinds of page. The *browse* pages — the front page and
// the section pages of ledes — are what you flip through, and there are a
// dozen of them, not two hundred. A story's full text lives on its own pages
// after those, reached by selecting its lede and left by going back. The
// reader never pages into a 40-page essay by accident, and the paper still
// ends.
struct StoryRef {
  // The item's dedup key. Stable across editions, which is what lets a
  // clipping saved on Tuesday find its story again on Thursday.
  uint64_t key = 0;
  std::string title;
  std::string section;
  std::string source;
  size_t lede_page = 0;    // browse page the lede appears on
  Rect lede_bounds;        // where to tap
  size_t first_page = 0;   // where the full text starts
  size_t page_count = 0;   // how long it runs
  bool truncated = false;  // the publisher's feed stops early
  // When the publisher says it ran. Carried so the reader can walk the
  // edition oldest-first, which page order alone cannot give.
  Epoch published = kNoDate;
};

struct Edition {
  Epoch date = kNoDate;
  std::string title;
  std::vector<Page> pages;
  ComposeStats stats;

  // Pages [0, browse_page_count) are the edition you flip through. Everything
  // after is story text, reachable only by selection.
  size_t browse_page_count = 0;
  // The last browse page: it says the paper has ended and why it is the
  // length it is. Belongs to no section.
  size_t colophon_page = 0;

  std::vector<StoryRef> stories;

  // Where each section's ledes start, for the section-jump overlay.
  struct SectionMark {
    std::string name;
    size_t first_page = 0;
  };
  std::vector<SectionMark> section_marks;

  size_t page_count() const { return pages.size(); }

  // Story indices oldest-first, which is the order a reader walks them in.
  // Computed rather than stored: it is a view of the stories, not a fact
  // about the edition.
  std::vector<size_t> reading_order() const;

  // The story whose lede covers a point on a browse page, or null.
  const StoryRef* story_at(size_t page, int x, int y) const {
    for (const StoryRef& s : stories) {
      if (s.lede_page == page && s.lede_bounds.contains(x, y)) return &s;
    }
    return nullptr;
  }
};

// Filters, orders and lays out. `sections` is consumed in the order given —
// that order is the paper's running order.
Edition compose_edition(std::vector<Section> sections, const FontPack& fonts,
                        const ComposeOptions& opts);

}  // namespace rsspaper
