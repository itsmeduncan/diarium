// Standing in for the network until the fetcher lands (issue #3).
//
// Resolving a configured feed URL to a file in the corpus is a simulator
// concern and is confined here: it must never leak into the config format or
// the core. When the real `IHttpClient` exists, this becomes the thing you
// point at instead of a server, and nothing above it changes.
#pragma once

#include <string>
#include <vector>

#include "core/base/datetime.h"
#include "core/config/feeds_config.h"
#include "core/edition/edition.h"
#include "core/text/font_pack.h"

namespace diarium {
namespace sim {

// Creates `path` if it isn't there. POSIX mkdir rather than <filesystem>,
// since the device build has no such thing and the simulator shouldn't need
// more of the standard library than the device can offer.
bool ensure_output_dir(const std::string& path);

// Fixture filenames present under `dir`.
std::vector<std::string> list_fixtures(const std::string& dir);

// Maps a feed URL to a fixture filename, or "" if nothing matches.
std::string fixture_for(const std::string& url,
                        const std::vector<std::string>& fixtures);

struct FixtureComposeOptions {
  std::string fixtures_dir = "test/fixtures/feeds";
  // Empty disables cross-edition dedup.
  std::string seen_path;
  // Ignore and don't update the seen store. What you want while iterating.
  bool fresh = true;
  // The edition's date. Defaults to the newest story found, so a corpus
  // captured months ago doesn't compose as entirely stale.
  Epoch date_override = kNoDate;
};

struct FixtureComposeReport {
  size_t unresolved_feeds = 0;
  size_t feeds_read = 0;
  size_t dropped_seen = 0;
  Epoch date = kNoDate;
  std::vector<std::string> problems;
  // The same failures, in the shape the colophon prints.
  std::vector<FeedProblem> feed_problems;
};

Edition compose_from_fixtures(const FeedList& config, const FontPack& fonts,
                              const FixtureComposeOptions& opts,
                              FixtureComposeReport* report);

}  // namespace sim
}  // namespace diarium
