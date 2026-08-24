#include "core/edition/edition.h"

#include <algorithm>

#include "core/base/str.h"
#include "core/edition/edition_stream.h"
#include "core/html/html_to_blocks.h"
#include "core/html/readability.h"
#include "core/layout/hyphenator.h"
#include "core/layout/paginator.h"
#include "core/layout/type_scale.h"

namespace diarium {
namespace {

Block text_block(const std::string& text) {
  Block b;
  b.type = BlockType::Paragraph;
  b.text = text;
  return b;
}

FlowElement element(TextRole role, const std::string& text) {
  FlowElement e;
  e.role = role;
  e.block = text_block(text);
  return e;
}

std::string byline_for(const Item& item) {
  std::string out;
  if (!item.author.empty()) out = "By " + item.author;
  if (!item.source_name.empty()) {
    out += out.empty() ? item.source_name : "  ·  " + item.source_name;
  }
  if (item.published != kNoDate) {
    out += out.empty() ? "" : "  ·  ";
    out += format_short_date(item.published);
  }
  return out;
}

bool newer(const Item& a, const Item& b) {
  // Undated items sort last rather than to 1970.
  if (a.published == kNoDate) return false;
  if (b.published == kNoDate) return true;
  return a.published > b.published;
}

// Drops stale stories, sorts each section newest-first, then applies the
// max_items budget across item metadata alone — none of it needs a story's
// pages laid out. Shared by compose_edition and compose_streaming, so there
// is exactly one selection path regardless of how the result gets written.
std::vector<Section> select_sections(std::vector<Section> sections,
                                     const ComposeOptions& opts,
                                     ComposeStats* stats) {
  const Epoch cutoff =
      opts.now == kNoDate
          ? kNoDate
          : opts.now - static_cast<Epoch>(opts.max_age_days) * 86400;

  // Drop stale stories, then sort each section newest-first — *before* any
  // budget is applied. Cutting to the budget first would decide which stories
  // survive by their order in the feed rather than by their date, which is
  // only accidentally the same thing.
  for (Section& s : sections) {
    std::vector<Item> fresh;
    for (Item& it : s.items) {
      ++stats->items_in;
      if (cutoff != kNoDate && it.published != kNoDate &&
          it.published < cutoff) {
        ++stats->dropped_stale;
        continue;
      }
      fresh.push_back(std::move(it));
    }
    std::stable_sort(fresh.begin(), fresh.end(), newer);
    s.items = std::move(fresh);
  }

  // Allocate the budget, if there is one. A ceiling of zero is no ceiling:
  // everything that got this far is published.
  //
  // When there is one: a floor for every section first, then the remainder
  // round-robin. Spending it in section order instead means a busy Technology
  // section eats the whole paper and the back pages simply vanish — a
  // newspaper doesn't drop its last section because the front was busy.
  if (opts.max_items > 0) {
    std::vector<size_t> take(sections.size(), 0);
    size_t remaining = opts.max_items;

    for (size_t i = 0; i < sections.size() && remaining > 0; ++i) {
      size_t floor = opts.min_per_section;
      if (floor > sections[i].items.size()) floor = sections[i].items.size();
      if (floor > remaining) floor = remaining;
      take[i] = floor;
      remaining -= floor;
    }

    bool progress = true;
    while (remaining > 0 && progress) {
      progress = false;
      for (size_t i = 0; i < sections.size() && remaining > 0; ++i) {
        if (take[i] >= sections[i].items.size()) continue;
        ++take[i];
        --remaining;
        progress = true;
      }
    }

    for (size_t i = 0; i < sections.size(); ++i) {
      if (sections[i].items.size() > take[i]) {
        stats->dropped_over_budget += sections[i].items.size() - take[i];
        sections[i].items.resize(take[i]);
      }
    }
  }

  return sections;
}

// Builds one story's flow and paginates it into `out_pages`, appending
// rather than replacing — the caller decides whether that is the whole
// edition's page list (compose_edition) or a scratch vector scoped to one
// story (compose_streaming). `page_offset` becomes StoryRef::first_page,
// which is only meaningful in the former case; compose_streaming passes 0
// since a streamed story's pages are never addressed by a global index.
//
// Also sets each new page's furniture (is_front_page, folio) from the local
// position within *this* story, so neither caller needs a second pass over
// pages it may not still have all of at once.
StoryRef paginate_story(const Item& it, const std::string& section_name,
                        const Paginator& paginator, const PageTemplate& tmpl,
                        size_t page_offset, std::vector<Page>* out_pages) {
  std::vector<FlowElement> story;
  story.push_back(element(TextRole::ArticleHead, it.title));
  const std::string by = byline_for(it);
  if (!by.empty()) story.push_back(element(TextRole::Byline, by));

  bool opened = false;
  for (const Block& b : it.blocks) {
    FlowElement e;
    e.role = role_for_block(b);
    e.block = b;
    if (!opened && e.role == TextRole::Body) {
      e.opens_story = true;
      opened = true;
    }
    story.push_back(std::move(e));
  }
  if (it.looks_truncated()) {
    story.push_back(
        element(TextRole::Caption, "The publisher's feed ends here."));
  }

  StoryRef ref;
  ref.key = it.dedup_key();
  ref.title = it.title;
  ref.section = section_name;
  ref.source = it.source_name;
  ref.truncated = it.looks_truncated();
  ref.published = it.published;
  ref.first_page = page_offset;

  const size_t added_start = out_pages->size();
  ref.page_count = paginator.paginate(story, tmpl, out_pages);

  // The folio says where you are in *this* story, not in whatever else
  // out_pages holds — there is no front-of-paper any more.
  for (size_t k = 0; k < ref.page_count; ++k) {
    Page& p = (*out_pages)[added_start + k];
    p.is_front_page = false;
    p.folio_left = section_name;
    p.folio_right =
        std::to_string(k + 1) + " / " + std::to_string(ref.page_count);
  }

  return ref;
}

}  // namespace

Edition compose_edition(std::vector<Section> sections, const FontPack& fonts,
                        const ComposeOptions& opts) {
  Edition ed;
  ed.date = opts.now;
  ed.title = opts.title;
  set_body_alignment(opts.body_alignment);

  const std::vector<Section> selected =
      select_sections(std::move(sections), opts, &ed.stats);

  const Hyphenator& hyphenator =
      opts.hyphenate ? english_hyphenator() : null_hyphenator();
  const Paginator paginator(fonts, hyphenator);
  PageTemplate story_tmpl;
  story_tmpl.columns = 1;

  // A memory-safety stop, not a preference. The whole edition is held in RAM
  // to serialize and to read, and the device's internal RAM runs out past a
  // few hundred pages — the compose aborts rather than degrading. So when the
  // page budget is reached, the remaining stories are dropped (and counted)
  // instead. The desktop leaves max_pages at zero and is never bounded here.
  bool budget_reached = false;
  for (const Section& s : selected) {
    if (budget_reached) {
      ed.stats.dropped_over_budget += s.items.size();
      continue;
    }
    for (size_t ii = 0; ii < s.items.size(); ++ii) {
      if (opts.max_pages > 0 && ed.pages.size() >= opts.max_pages) {
        ed.stats.dropped_over_budget += s.items.size() - ii;
        budget_reached = true;
        break;
      }
      StoryRef ref = paginate_story(s.items[ii], s.name, paginator,
                                    story_tmpl, ed.pages.size(), &ed.pages);
      ++ed.stats.items_published;
      if (s.items[ii].looks_truncated()) ++ed.stats.truncated_published;
      ed.stories.push_back(std::move(ref));
    }
  }

  return ed;
}

bool compose_streaming(std::vector<Section> sections, const FontPack& fonts,
                       const ComposeOptions& opts, ByteSink& sink,
                       ComposeStats* stats, const ArticleSource* articles) {
  set_body_alignment(opts.body_alignment);

  ComposeStats selection_stats;
  const std::vector<Section> selected =
      select_sections(std::move(sections), opts, &selection_stats);

  // The header, committed by the writer's constructor below, before any
  // story — or its article — has been fetched or laid out for real: the
  // cheapest honest number available this early is every selected item,
  // not the smaller count that survives the max_pages backstop below. See
  // the `stats` doc on the declaration for why this can't be exact without
  // giving up the lazy article fetch this function exists for.
  ComposeStats header_stats = selection_stats;
  for (const Section& s : selected) {
    header_stats.items_published += s.items.size();
    for (const Item& it : s.items) {
      if (it.looks_truncated()) ++header_stats.truncated_published;
    }
  }

  StreamingEditionWriter writer(sink, opts.now, opts.title, header_stats);

  const Hyphenator& hyphenator =
      opts.hyphenate ? english_hyphenator() : null_hyphenator();
  const Paginator paginator(fonts, hyphenator);
  PageTemplate story_tmpl;
  story_tmpl.columns = 1;

  ComposeStats actual = selection_stats;
  actual.items_published = 0;
  actual.truncated_published = 0;

  // A memory-safety stop, not a preference — see compose_edition's twin of
  // this loop. Dropped stories here never reach the article fetch below
  // either: the budget is checked first.
  size_t total_pages = 0;
  bool budget_reached = false;
  for (const Section& s : selected) {
    if (budget_reached) {
      actual.dropped_over_budget += s.items.size();
      continue;
    }
    for (size_t ii = 0; ii < s.items.size(); ++ii) {
      if (opts.max_pages > 0 && total_pages >= opts.max_pages) {
        actual.dropped_over_budget += s.items.size() - ii;
        budget_reached = true;
        break;
      }

      const Item& original = s.items[ii];
      Item replaced;
      const Item* to_paginate = &original;
      // The article, fetched and extracted for this one story only, right
      // before it is laid out — never more than one story's article
      // resident, and never fetched at all for a story the budget above
      // already dropped.
      if (articles != nullptr && original.looks_truncated()) {
        const std::string html = articles->html_for(original);
        if (!html.empty()) {
          BlockCollector page_blocks;
          HtmlToBlocks conv(page_blocks);
          conv.feed(html);
          conv.finish();
          std::vector<Block> article = extract_article(page_blocks.blocks);
          // Empty means the page read as all furniture; the feed's own
          // excerpt is better than printing a navigation bar.
          if (!article.empty()) {
            replaced = original;
            replaced.blocks = std::move(article);
            replaced.truncation = TruncationReason::None;
            to_paginate = &replaced;
          }
        }
      }

      std::vector<Page> pages;
      // page_offset 0: a streamed story's pages are addressed only within
      // its own byte range (see StreamingEditionReader::load_story_pages),
      // never through a global index — there is no global page list here.
      StoryRef ref = paginate_story(*to_paginate, s.name, paginator,
                                    story_tmpl, 0, &pages);
      total_pages += pages.size();
      writer.add_story(ref, pages);
      // `pages`, and `replaced`'s article blocks if any were fetched, free
      // here, at the end of the loop body, before the next story is laid
      // out — nothing bigger than one story's article and pages is ever
      // resident.

      ++actual.items_published;
      if (to_paginate->looks_truncated()) ++actual.truncated_published;
    }
  }

  if (stats != nullptr) *stats = actual;
  return writer.finish();
}

std::vector<size_t> Edition::reading_order() const {
  std::vector<size_t> order;
  order.reserve(stories.size());
  for (size_t i = 0; i < stories.size(); ++i) order.push_back(i);

  // Oldest first. Undated stories keep their composed position relative to
  // each other rather than all piling up at one end.
  std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) {
    const Epoch pa = stories[a].published;
    const Epoch pb = stories[b].published;
    if (pa == kNoDate || pb == kNoDate) return false;
    return pa < pb;
  });
  return order;
}

}  // namespace diarium
