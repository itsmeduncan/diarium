// OPML import.
//
// Nobody hand-writes a feed list they already have somewhere else. This reads
// the file every reader exports and produces a `FeedList` — the same type
// `feeds.toml` parses into, so nothing downstream knows or cares where the
// feeds came from. That seam was the point of `FeedList` existing.
//
// OPML's folders become sections, which is the natural reading: a folder in a
// feed reader is already the thing a section is.
#pragma once

#include <string>

#include "core/config/feeds_config.h"
#include "core/io/byte_source.h"

namespace diarium {

struct OpmlOptions {
  // Where feeds that sit outside any folder go.
  std::string default_section = "News";
  // Items to take per imported feed.
  size_t max_items = 6;
  // OPML nests arbitrarily; sections do not. Below this depth, folders are
  // ignored and their feeds join the nearest named ancestor.
  size_t max_folder_depth = 3;
};

struct OpmlReport {
  std::string title;        // the OPML's own <head><title>
  size_t feeds_imported = 0;
  size_t duplicates_skipped = 0;
  size_t entries_without_url = 0;  // folders, and malformed outlines
};

// Appends to `out->feeds`, so several OPML files can be merged. Returns false
// only when the document yields no feeds at all.
bool parse_opml(ByteSource& src, FeedList* out, OpmlOptions opts = OpmlOptions(),
                OpmlReport* report = nullptr);

bool import_opml_file(const std::string& path, FeedList* out,
                      OpmlOptions opts = OpmlOptions(),
                      OpmlReport* report = nullptr);

// Renders a FeedList back out as feeds.toml, so an import can be written to
// the device's config rather than held in memory. Round-trips through
// `parse_feeds_toml`.
std::string to_feeds_toml(const FeedList& list);

}  // namespace diarium
