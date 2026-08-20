// Building an edition from live feeds.
//
// The mirror of src/sim/fixtures.cpp, which does the same from files on disk.
// It lives in src/device/ for the same reason that one lives in src/sim/:
// composing needs a source of bytes, and only the reader may hold the HAL.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/config/feeds_config.h"
#include "core/edition/edition.h"
#include "core/edition/seen_store.h"
#include "core/net/feed_cache.h"
#include "core/text/font_pack.h"
#include "hal/hal.h"

namespace rsspaper {
namespace device {

struct ComposeReport {
  size_t feeds_fetched = 0;    // returned a body
  size_t feeds_unchanged = 0;  // answered 304, and were read from the card
  size_t feeds_failed = 0;
  size_t bytes_downloaded = 0;
  uint32_t elapsed_ms = 0;
  std::vector<FeedProblem> problems;  // carried into the colophon
};

// Two phases, deliberately separate, because the radio and the composed
// edition must not be in memory at once (DECISIONS 27).
//
// Phase one: fetch every configured feed onto the card and nothing else. Each
// body is kept, so a 304 costs a handshake and the stories still appear —
// without that, an unchanged feed would silently drop out of the paper, which
// is the opposite of what conditional GET is for.
//
// A feed that fails costs that feed and lands in the colophon, never the paper.
void fetch_all(const FeedList& config, IHttpClient* http, IStorage* storage,
               FeedCache* cache, ComposeReport* report);

// Phase two: parse what is on the card and lay out the paper. Call it with
// the radio already off.
Edition compose_from_card(const FeedList& config, const FontPack& fonts,
                          IStorage* storage, SeenStore* seen, Epoch now,
                          const ComposeReport& fetched);

}  // namespace device
}  // namespace rsspaper
