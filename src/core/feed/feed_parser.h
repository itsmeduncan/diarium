// RSS 2.0 / RSS 1.0 (RDF) / Atom → Items, in one streaming pass.
//
// The three formats disagree about almost everything except that stories are
// elements with titles, so the parser keys on element names rather than
// branching on a detected format. Where they genuinely differ — a link is
// element text in RSS and an attribute in Atom — both spellings are accepted.
#pragma once

#include <cstddef>
#include <string>

#include "core/feed/item.h"
#include "core/html/html_to_blocks.h"
#include "core/io/byte_source.h"

namespace diarium {

struct FeedParseOptions {
  // Stop after this many items. Feeds routinely carry 100+; an edition wants a
  // handful, and parsing stops early rather than building blocks we discard.
  size_t max_items = 12;
  // Per-item content limits, handed to the block converter.
  HtmlLimits html_limits;
  // Cap on the plain-text standfirst kept for the front page.
  size_t summary_bytes = 480;
  // Below this many characters of body text, a linked story is assumed to be
  // truncated by the publisher.
  size_t short_body_threshold = 500;
};

class ItemSink {
 public:
  virtual ~ItemSink() = default;
  // Return false to stop parsing early.
  virtual bool on_item(Item&& item) = 0;
  virtual void on_feed_title(const std::string& title) { (void)title; }
};

enum class FeedFormat : uint8_t { Unknown, Rss, Rdf, Atom };

struct FeedParseStats {
  FeedFormat format = FeedFormat::Unknown;
  std::string feed_title;
  size_t items_seen = 0;
  size_t items_emitted = 0;
  size_t bytes_consumed = 0;
  bool recovered_errors = false;  // parser had to apply a tolerance rule
  bool stopped_early = false;     // hit max_items or the sink said stop
};

FeedParseStats parse_feed(ByteSource& src, ItemSink& sink,
                          const FeedParseOptions& opts = FeedParseOptions());

const char* feed_format_name(FeedFormat f);

}  // namespace diarium
