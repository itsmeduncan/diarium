// Reading feeds.toml.
//
// A hand-written parser for the subset of TOML this file uses — top-level
// tables, arrays of tables, string and integer values. A full TOML library
// would be a third dependency for a hundred-line config format, and this is
// the only configuration the product has.
//
// The OPML seam is `FeedList`: an importer only has to produce one of these,
// and nothing downstream knows or cares where the feeds came from.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/layout/page.h"

namespace diarium {

struct FeedEntry {
  std::string url;
  std::string section = "News";
  size_t max_items = 6;
};

struct EditionConfig {
  std::string title = "Diarium";
  std::string wake_at = "05:30";  // local time, 24-hour
  // Minutes east of UTC. The device has no network to ask and no keyboard to
  // be asked, so the offset travels with the config on the card.
  int utc_offset_minutes = 0;
  // Zero is no ceiling on the edition as a whole; per-feed caps still apply.
  size_t max_items = 0;
  int max_age_days = 3;
  Align body_alignment = Align::Left;
  // Portrait out of the box: the panel reads better stood on its short edge.
  // Landscape is a one-line opt-in here. This is a compose-time choice — change
  // it and the next composed edition takes it; a cached one keeps its shape.
  Orientation orientation = Orientation::Portrait;
  // Liang hyphenation. Worth having ragged too — it evens the rag — and
  // essential for justified text in a narrow column.
  bool hyphenate = true;
};

// Credentials live on the card and never in the repo. The device has no
// keyboard, so this is how a network gets configured — and it is why the
// card, not a captive portal, is the answer to getting config onto a device.
struct WifiConfig {
  std::string ssid;
  std::string password;
  bool configured() const { return !ssid.empty(); }
};

struct FeedList {
  EditionConfig edition;
  WifiConfig wifi;
  std::vector<FeedEntry> feeds;

  // Sections in the order they first appear, which is the running order of
  // the paper.
  std::vector<std::string> section_order() const;
};

// Parses `text`. On failure, `error` gets a message naming the line, because
// a config error on a device with no screen for diagnostics has to be
// legible when it finally is displayed.
bool parse_feeds_toml(const std::string& text, FeedList* out,
                      std::string* error);

bool load_feeds_toml(const std::string& path, FeedList* out,
                     std::string* error);

}  // namespace diarium
