// One story, after parsing and before layout.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/base/datetime.h"
#include "core/html/block.h"

namespace diarium {

enum class ContentSource : uint8_t {
  None,         // the feed gave us nothing renderable
  Summary,      // <description> / <summary> only
  FullContent,  // <content:encoded> / Atom <content>
};

// Why we think the publisher held something back. v1 renders what the feed
// publishes either way — this is measurement, so we can tell later whether an
// on-device readability extractor is worth the flash it would cost.
enum class TruncationReason : uint8_t {
  None,
  SummaryOnly,    // no full-content element at all
  EllipsisTail,   // body trails off in "…" or "..."
  ReadMoreLink,   // ends in "Read more" / "Continue reading" and friends
  VeryShort,      // implausibly short body for a linked article
};

const char* truncation_reason_name(TruncationReason r);

struct Item {
  std::string guid;         // publisher's id, when they gave one
  std::string title;
  std::string author;
  std::string link;
  Epoch published = kNoDate;

  std::string section;      // assigned from feeds.toml, not from the feed
  std::string source_name;  // the feed's own title, for the byline

  std::vector<Block> blocks;
  // Plain-text opening, capped. Feeds the front-page standfirst without
  // re-flowing the whole article.
  std::string summary_text;

  ContentSource content_source = ContentSource::None;
  TruncationReason truncation = TruncationReason::None;
  size_t text_bytes = 0;

  // Stable across runs: what the seen-store remembers so a story doesn't
  // reappear tomorrow.
  uint64_t dedup_key() const;

  bool looks_truncated() const { return truncation != TruncationReason::None; }
};

}  // namespace diarium
