// Composing an edition: the step that turns a pile of items into story-text
// pages, walked oldest-first. There is no front-of-paper — no front page, no
// section ledes, no colophon — just the stories themselves.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/base/datetime.h"
#include "core/feed/item.h"
#include "core/io/byte_sink.h"
#include "core/layout/page.h"
#include "core/text/font_pack.h"

namespace diarium {

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
  std::string title = "Diarium";
  // Optional ceiling on the whole edition. Zero means no ceiling: every story
  // that survives the per-feed caps and the age window is published, which is
  // what you want when you are a week behind the news. Set it if you would
  // rather have a paper of a predictable length.
  size_t max_items = 0;
  // A hard ceiling on the composed edition's page count. Zero means no ceiling.
  // Unlike max_items, this is not a preference — it is a memory-safety limit
  // the device sets, because the whole edition is held in RAM to serialize and
  // to read, and past a few hundred KB the device's scarce internal RAM runs
  // out and the compose aborts. When the budget is reached the remaining
  // stories are dropped (counted in dropped_over_budget) rather than the paper
  // growing until it crashes. The desktop leaves this at zero.
  size_t max_pages = 0;
  // When there is a ceiling, the stories every section gets before any section
  // gets more — otherwise the budget is spent in section order and the last
  // section vanishes because the front was busy.
  size_t min_per_section = 2;
  // Stories older than this are stale news, however recently they appeared in
  // the feed.
  int max_age_days = 3;
  Align body_alignment = Align::Left;
  bool hyphenate = true;
  // How many feeds were configured, and which of them failed.
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
  // Vestigial: the front page that used to overflow is gone, so nothing
  // increments this any more and it stays 0. Retained rather than pulled
  // out of the struct, per an earlier scope call on this task.
  size_t front_page_overflow = 0;
  size_t truncated_published = 0;
};

// One story's place in the edition.
struct StoryRef {
  // The item's dedup key. Stable across editions, which is what lets a
  // story read on Tuesday be recognised in Thursday's paper and left out.
  uint64_t key = 0;
  std::string title;
  std::string section;
  std::string source;
  size_t first_page = 0;   // where the full text starts
  size_t page_count = 0;   // how long it runs
  bool truncated = false;  // the publisher's feed stops early
  // When the publisher says it ran. Carried so the reader can walk the
  // edition oldest-first, which page order alone cannot give.
  Epoch published = kNoDate;
};

// An edition is story text, walked oldest-first — there is no front-of-paper.
// Every page belongs to exactly one story; the reader lands on a home summary
// (not a composed page) and swipes onward through `reading_order()`.
struct Edition {
  Epoch date = kNoDate;
  std::string title;
  std::vector<Page> pages;
  ComposeStats stats;

  std::vector<StoryRef> stories;

  size_t page_count() const { return pages.size(); }

  // Story indices oldest-first, which is the order a reader walks them in.
  // Computed rather than stored: it is a view of the stories, not a fact
  // about the edition.
  std::vector<size_t> reading_order() const;
};

// Filters, orders and lays out. `sections` is consumed in the order given —
// that order is the paper's running order.
Edition compose_edition(std::vector<Section> sections, const FontPack& fonts,
                        const ComposeOptions& opts);

// The same select/paginate pipeline as compose_edition, but writing one
// story at a time to `sink` (via a StreamingEditionWriter — see
// core/edition/edition_stream.h) instead of building a resident Edition.
// Nothing bigger than one story's pages is ever held at once, which is what
// lets an edition of hundreds of stories compose on a device with 8 MB of
// PSRAM. Returns false if the sink failed.
//
// `stats`, if given, is filled with the actual published/dropped counts once
// every selected story has been written, and matches what the v5 file's own
// header says: the writer's constructor commits that header right after
// selection, before any story is laid out for real, so when there is a
// max_pages ceiling this runs a write-nothing dry layout first to learn
// exactly which stories the backstop will drop — the same layout the real
// pass repeats while actually writing — so the header is not left reporting
// the pre-budget selection count.
bool compose_streaming(std::vector<Section> sections, const FontPack& fonts,
                       const ComposeOptions& opts, ByteSink& sink,
                       ComposeStats* stats = nullptr);

}  // namespace diarium
