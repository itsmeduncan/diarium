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

struct ComposeOptions {
  Epoch now = kNoDate;
  std::string title = "RSSpaper";
  // Hard ceiling on the whole edition. A paper that never ends is a feed.
  size_t max_items = 40;
  // Stories older than this are stale news, however recently they appeared in
  // the feed.
  int max_age_days = 3;
  int front_page_columns = 2;
  Align body_alignment = Align::Left;
  // How many section headlines the front page lists per section.
  size_t front_page_per_section = 2;
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

struct Edition {
  Epoch date = kNoDate;
  std::string title;
  std::vector<Page> pages;
  ComposeStats stats;

  // Where each section starts, for the section-jump overlay.
  struct SectionMark {
    std::string name;
    size_t first_page = 0;
  };
  std::vector<SectionMark> section_marks;

  size_t page_count() const { return pages.size(); }
};

// Filters, orders and lays out. `sections` is consumed in the order given —
// that order is the paper's running order.
Edition compose_edition(std::vector<Section> sections, const FontPack& fonts,
                        const ComposeOptions& opts);

}  // namespace rsspaper
