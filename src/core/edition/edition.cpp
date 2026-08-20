#include "core/edition/edition.h"

#include <algorithm>

#include "core/base/str.h"
#include "core/layout/hyphenator.h"
#include "core/layout/paginator.h"
#include "core/layout/type_scale.h"
#include "core/render/page_renderer.h"

namespace rsspaper {
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

// A short excerpt for the front page, cut at a sentence if one is close by so
// the page doesn't read as a wall of half-sentences.
std::string standfirst(const Item& item, size_t max_bytes) {
  std::string s = item.summary_text;
  if (s.empty()) {
    for (const Block& b : item.blocks) {
      if (b.type == BlockType::Paragraph && !b.text.empty()) {
        s = b.text;
        break;
      }
    }
  }
  for (char& c : s) {
    if (c == '\n') c = ' ';
  }
  s = collapse_ws(s);
  if (s.size() <= max_bytes) return s;

  size_t cut = max_bytes;
  while (cut > 0 && (static_cast<uint8_t>(s[cut]) & 0xC0) == 0x80) --cut;
  const size_t stop = s.find_last_of('.', cut);
  if (stop != std::string::npos && stop > max_bytes / 2) {
    return s.substr(0, stop + 1);
  }
  const size_t space = s.rfind(' ', cut);
  if (space != std::string::npos && space > max_bytes / 2) cut = space;
  std::string out = s.substr(0, cut);
  trim_inplace(out);
  return out + "\xE2\x80\xA6";
}

// Total height of the first `count` elements at `measure` px wide. Used to
// size the front-page banner to its content instead of a guessed constant.
int measure_flow_height(const std::vector<FlowElement>& flow, size_t count,
                        int measure, const FontPack& fonts,
                        const Hyphenator& hyphenator) {
  int total = 0;
  for (size_t i = 0; i < count && i < flow.size(); ++i) {
    const RoleStyle style = role_style(flow[i].role);
    const std::vector<BrokenLine> lines = break_block(
        flow[i].block, style, measure - style.left_margin, fonts, hyphenator);
    total += style.space_before + style.space_after +
             static_cast<int>(lines.size()) * style.leading;
  }
  return total;
}

// Picks the largest display size the headline can wear without swamping the
// page. Newspapers have always sized a lead headline to fit its measure rather
// than setting every lead at one size and hoping; three lines at 66 px is the
// point where the headline stops being a headline and starts being the page.
TextRole fit_lead_headline(const std::string& title, int measure,
                           const FontPack& fonts,
                           const Hyphenator& hyphenator) {
  for (const TextRole candidate : {TextRole::LeadHead, TextRole::ArticleHead}) {
    const RoleStyle style = role_style(candidate);
    Block b;
    b.text = title;
    const size_t lines =
        break_block(b, style, measure, fonts, hyphenator).size();
    if (lines <= (candidate == TextRole::LeadHead ? 2u : 4u)) return candidate;
  }
  return TextRole::ArticleHead;
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

  // Allocate the budget: a floor for every section first, then the remainder
  // round-robin. Spending it in section order instead means a busy Technology
  // section eats the whole paper and the back pages simply vanish — a
  // newspaper doesn't drop its last section because the front was busy.
  {
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

  // The lead is simply the most recent story in the paper. No scoring, no
  // ranking — that is the product thesis, not a placeholder.
  const Item* lead = nullptr;
  size_t lead_section = 0, lead_index = 0;
  for (size_t si = 0; si < sections.size(); ++si) {
    for (size_t ii = 0; ii < sections[si].items.size(); ++ii) {
      const Item& it = sections[si].items[ii];
      if (lead == nullptr || newer(it, *lead)) {
        lead = &it;
        lead_section = si;
        lead_index = ii;
      }
    }
  }

  // --- front page -----------------------------------------------------------
  const Hyphenator& hyphenator =
      opts.hyphenate ? english_hyphenator() : null_hyphenator();
  const PageRenderer renderer(fonts);
  std::vector<FlowElement> front;
  size_t lead_element_count = 0;

  if (lead != nullptr) {
    const int lead_measure = kPageWidth - 2 * PageTemplate().margin_left;
    front.push_back(element(TextRole::Kicker, sections[lead_section].name));
    front.push_back(element(
        fit_lead_headline(lead->title, lead_measure, fonts, hyphenator),
        lead->title));
    const std::string deck = standfirst(*lead, 320);
    if (!deck.empty()) front.push_back(element(TextRole::Deck, deck));
    const std::string by = byline_for(*lead);
    if (!by.empty()) front.push_back(element(TextRole::Byline, by));
    lead_element_count = front.size();
  }

  for (size_t si = 0; si < sections.size(); ++si) {
    const Section& s = sections[si];
    if (s.items.empty()) continue;

    // A section label with no story under it is worse than no label.
    FlowElement section_label = element(TextRole::Kicker, s.name);
    section_label.keep_with_next = true;
    front.push_back(std::move(section_label));
    size_t listed = 0;
    for (size_t ii = 0; ii < s.items.size(); ++ii) {
      if (si == lead_section && ii == lead_index) continue;  // already the lead
      if (listed >= opts.front_page_per_section) break;
      // Same shape as a section lede, so the front page and the section
      // pages teach the reader one thing rather than two — and so a teaser
      // that doesn't fit is pushed off whole instead of being cut mid-line.
      const Item& it = s.items[ii];
      FlowElement head = element(TextRole::LedeHead, it.title);
      head.keep_with_next = true;
      front.push_back(std::move(head));
      const std::string ex = standfirst(it, 130);
      if (!ex.empty()) front.push_back(element(TextRole::LedeText, ex));
      ++listed;
    }
  }

  PageTemplate front_tmpl;
  front_tmpl.columns = opts.front_page_columns;
  front_tmpl.first_page_header = renderer.masthead_height();
  front_tmpl.column_rules = true;
  // The banner is exactly as tall as the lead's kicker, headline and deck
  // need, so the columns start immediately beneath rather than against a
  // guessed constant.
  front_tmpl.banner_height =
      measure_flow_height(front, lead_element_count,
                          kPageWidth - front_tmpl.margin_left -
                              front_tmpl.margin_right,
                          fonts, hyphenator);
  // The banner may take most of the page below the nameplate, but not all of
  // it: something has to be left for the section teasers that make a front
  // page a front page.
  const int banner_cap = (kPageHeight - front_tmpl.first_page_header -
                          front_tmpl.margin_top - front_tmpl.margin_bottom) *
                         5 / 8;
  if (front_tmpl.banner_height > banner_cap) {
    front_tmpl.banner_height = banner_cap;
  }

  const Paginator paginator(fonts, hyphenator);
  std::vector<Placement> front_placements;
  paginator.paginate(front, front_tmpl, &ed.pages, &front_placements);

  // A front page is one page. Anything that didn't fit is dropped rather than
  // spilling into a second and third "front" page — but it is counted, because
  // silently losing stories is exactly the kind of thing a reader should be
  // able to notice.
  const size_t front_pages = ed.pages.empty() ? 0 : 1;
  if (ed.pages.size() > 1) {
    for (size_t i = 0; i < front_placements.size(); ++i) {
      if (front_placements[i].page >= 1 && front[i].role == TextRole::LedeHead) {
        ++ed.stats.front_page_overflow;
      }
    }
    ed.pages.resize(1);
  }

  // --- section pages: ledes you browse ------------------------------------
  //
  // This is the edition proper. Each section page carries four or so ledes;
  // selecting one opens the story, which lives on pages of its own after the
  // browse sequence.
  std::vector<FlowElement> ledes;
  std::vector<std::pair<std::string, size_t>> section_starts;  // name, flow idx
  // Parallel to `ledes`: which flow elements make up each story's tap target,
  // and which story they belong to.
  struct LedeSpan {
    size_t first_element;
    size_t element_count;
    size_t story;
  };
  std::vector<LedeSpan> lede_spans;

  for (const Section& s : sections) {
    if (s.items.empty()) continue;
    section_starts.emplace_back(s.name, ledes.size());

    FlowElement head = element(TextRole::SectionHead, s.name);
    head.page_break_before = true;
    ledes.push_back(std::move(head));

    for (const Item& it : s.items) {
      const size_t first = ledes.size();

      std::string source = it.source_name;
      if (it.published != kNoDate) {
        source += source.empty() ? "" : "  ·  ";
        source += format_short_date(it.published);
      }
      if (!source.empty()) {
        FlowElement kicker = element(TextRole::LedeKicker, source);
        kicker.keep_with_next = true;
        ledes.push_back(std::move(kicker));
      }
      FlowElement lede_head = element(TextRole::LedeHead, it.title);
      lede_head.keep_with_next = true;
      ledes.push_back(std::move(lede_head));

      std::string summary = standfirst(it, 190);
      if (it.looks_truncated() && !summary.empty()) {
        summary += "  (feed excerpt)";
      }
      if (!summary.empty()) {
        ledes.push_back(element(TextRole::LedeText, summary));
      }

      StoryRef ref;
      ref.published = it.published;
      ref.key = it.dedup_key();
      ref.title = it.title;
      ref.section = s.name;
      ref.source = it.source_name;
      ref.truncated = it.looks_truncated();
      ed.stories.push_back(std::move(ref));

      lede_spans.push_back(
          LedeSpan{first, ledes.size() - first, ed.stories.size() - 1});
      ++ed.stats.items_published;
      if (it.looks_truncated()) ++ed.stats.truncated_published;
    }
  }

  // --- the colophon: the last page, which says the paper has ended --------
  const size_t colophon_element = ledes.size();
  {
    FlowElement end = element(TextRole::SectionHead, "That's the paper.");
    end.page_break_before = true;
    ledes.push_back(std::move(end));

    std::string line = format_masthead_date(opts.now);
    if (!line.empty()) line += "  ·  ";
    line += std::to_string(ed.stats.items_published) + " stories";
    if (opts.feeds_configured > 0) {
      const size_t ok = opts.feeds_configured >= opts.feed_problems.size()
                            ? opts.feeds_configured - opts.feed_problems.size()
                            : 0;
      line += "  ·  " + std::to_string(ok) + " of " +
              std::to_string(opts.feeds_configured) + " feeds";
    }
    ledes.push_back(element(TextRole::Byline, line));

    if (!opts.feed_problems.empty()) {
      ledes.push_back(element(
          TextRole::LedeKicker,
          opts.feed_problems.size() == 1
              ? "One feed didn't answer"
              : std::to_string(opts.feed_problems.size()) +
                    " feeds didn't answer"));
      for (const FeedProblem& problem : opts.feed_problems) {
        std::string text = problem.source;
        if (!problem.reason.empty()) text += " — " + problem.reason;
        ledes.push_back(element(TextRole::LedeText, text));
      }
    }

    // Held-over stories, so a thin paper explains itself.
    const size_t held = ed.stats.dropped_over_budget;
    if (held > 0) {
      ledes.push_back(element(
          TextRole::LedeText,
          std::to_string(held) +
              (held == 1 ? " story was held over for space."
                         : " stories were held over for space.")));
    }
    if (ed.stats.truncated_published > 0) {
      ledes.push_back(element(
          TextRole::LedeText,
          std::to_string(ed.stats.truncated_published) + " of " +
              std::to_string(ed.stats.items_published) +
              " stories are excerpts, because that is all their publisher's "
              "feed carries."));
    }
  }

  PageTemplate lede_tmpl;
  lede_tmpl.columns = 1;
  std::vector<Placement> lede_placements;
  paginator.paginate(ledes, lede_tmpl, &ed.pages, &lede_placements);
  ed.browse_page_count = ed.pages.size();
  ed.colophon_page = colophon_element < lede_placements.size()
                         ? lede_placements[colophon_element].page
                         : (ed.pages.empty() ? 0 : ed.pages.size() - 1);

  for (const std::pair<std::string, size_t>& start : section_starts) {
    const size_t page = start.second < lede_placements.size()
                            ? lede_placements[start.second].page
                            : 0;
    ed.section_marks.push_back(Edition::SectionMark{start.first, page});
  }

  // A lede's tap target is the union of its kicker, headline and summary.
  for (const LedeSpan& span : lede_spans) {
    StoryRef& ref = ed.stories[span.story];
    Rect bounds;
    size_t page = 0;
    bool have_page = false;
    for (size_t k = 0; k < span.element_count; ++k) {
      const size_t idx = span.first_element + k;
      if (idx >= lede_placements.size()) break;
      const Placement& pl = lede_placements[idx];
      if (pl.bounds.empty()) continue;
      // A lede split across a page boundary anchors to where it began.
      if (!have_page) {
        page = pl.page;
        have_page = true;
      }
      if (pl.page == page) bounds.extend(pl.bounds);
    }
    ref.lede_page = page;
    ref.lede_bounds = bounds;
  }

  // --- story pages: the full text, reached by selection --------------------
  PageTemplate story_tmpl;
  story_tmpl.columns = 1;

  size_t story_index = 0;
  for (const Section& s : sections) {
    for (const Item& it : s.items) {
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

      const size_t first = ed.pages.size();
      const size_t added = paginator.paginate(story, story_tmpl, &ed.pages);
      if (story_index < ed.stories.size()) {
        ed.stories[story_index].first_page = first;
        ed.stories[story_index].page_count = added;
      }
      ++story_index;
    }
  }

  // --- furniture ------------------------------------------------------------
  for (size_t i = 0; i < ed.pages.size(); ++i) {
    Page& p = ed.pages[i];
    p.is_front_page = i < front_pages;

    if (i < ed.browse_page_count) {
      p.folio_right = std::to_string(i + 1) + " / " +
                      std::to_string(ed.browse_page_count);
      p.folio_left = p.is_front_page ? format_masthead_date(opts.now) : "";
      for (const Edition::SectionMark& m : ed.section_marks) {
        if (i >= m.first_page) p.folio_left = m.name;
      }
      // The colophon belongs to no section, so it wears the paper's name.
      if (i == ed.colophon_page && !p.is_front_page) p.folio_left = ed.title;
    } else {
      // Inside a story the folio says where you are in *that* story, not in
      // the edition: the edition's page count is not what you're reading.
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

}  // namespace rsspaper
