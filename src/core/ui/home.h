// The page you wake to: a summary of what is unread, not an index of it.
//
// Drawn rather than composed, because it is a view of what is left and that
// changes as you read while the edition does not. Reading is linear now — you
// swipe right from here into the oldest unread story — so this counts and
// orients rather than listing headlines to tap.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/edition/edition.h"
#include "core/render/framebuffer.h"
#include "core/text/font_pack.h"

namespace diarium {

// The unread breakdown, split out so it can be tested without a font pack.
struct HomeSummary {
  size_t unread_total = 0;
  struct Section {
    std::string name;
    size_t count = 0;
  };
  // Sections in the order they first appear in `order`, which is the paper's
  // running order.
  std::vector<Section> sections;
};

// `order` is story indices oldest-first; `unread` is the same length and says
// which are still to read.
HomeSummary summarize_home(const Edition& edition,
                           const std::vector<size_t>& order,
                           const std::vector<bool>& unread);

// Draws the home page: nameplate, the unread count and its per-section
// breakdown, the freshness strap, and the hint to swipe onward.
//
// `confirm_clear` shows the tap-to-clear prompt in place of the swipe hint,
// for when a first tap has armed clearing the whole backlog and it is
// waiting on a second tap to confirm.
void render_home(const FontPack& fonts, const Edition& edition,
                 const std::vector<size_t>& order,
                 const std::vector<bool>& unread, const std::string& strap,
                 Framebuffer* fb, bool confirm_clear = false);

}  // namespace diarium
