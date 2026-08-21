#include "core/edition/edition.h"

#include <algorithm>

#include "core/base/str.h"
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

}  // namespace

Edition compose_edition(std::vector<Section> sections, const FontPack& fonts,
                        const ComposeOptions& opts) {
  Edition ed;
  ed.date = opts.now;
  ed.title = opts.title;
  set_body_alignment(opts.body_alignment);

  // --- select ---------------------------------------------------------------
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
      ++ed.stats.items_in;
      if (cutoff != kNoDate && it.published != kNoDate &&
          it.published < cutoff) {
        ++ed.stats.dropped_stale;
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
        ed.stats.dropped_over_budget += sections[i].items.size() - take[i];
        sections[i].items.resize(take[i]);
      }
    }
  }

  // --- story pages: the full text, and each story's place in the paper -----
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
  for (const Section& s : sections) {
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
      const Item& it = s.items[ii];
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
        story.push_back(element(TextRole::Caption,
                                "The publisher's feed ends here."));
      }

      StoryRef ref;
      ref.key = it.dedup_key();
      ref.title = it.title;
      ref.section = s.name;
      ref.source = it.source_name;
      ref.truncated = it.looks_truncated();
      ref.published = it.published;
      ref.first_page = ed.pages.size();
      ref.page_count = paginator.paginate(story, story_tmpl, &ed.pages);
      ed.stories.push_back(std::move(ref));

      ++ed.stats.items_published;
      if (it.looks_truncated()) ++ed.stats.truncated_published;
    }
  }

  // --- furniture ------------------------------------------------------------
  for (size_t i = 0; i < ed.pages.size(); ++i) {
    Page& p = ed.pages[i];
    p.is_front_page = false;  // there is no front-of-paper any more

    // The folio says where you are in *that* story, not in the edition: the
    // edition's page count is not what you're reading.
    for (const StoryRef& st : ed.stories) {
      if (st.page_count > 0 && i >= st.first_page &&
          i < st.first_page + st.page_count) {
        p.folio_left = st.section;
        p.folio_right = std::to_string(i - st.first_page + 1) + " / " +
                        std::to_string(st.page_count);
        break;
      }
    }
  }

  return ed;
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
