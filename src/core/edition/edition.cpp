#include "core/edition/edition.h"

#include <algorithm>

#include "core/base/str.h"
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
                        int measure, const FontPack& fonts) {
  int total = 0;
  for (size_t i = 0; i < count && i < flow.size(); ++i) {
    const RoleStyle style = role_style(flow[i].role);
    const std::vector<BrokenLine> lines = break_block(
        flow[i].block, style, measure - style.left_margin, fonts,
        null_hyphenator());
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
                           const FontPack& fonts) {
  for (const TextRole candidate : {TextRole::LeadHead, TextRole::ArticleHead}) {
    const RoleStyle style = role_style(candidate);
    Block b;
    b.text = title;
    const size_t lines =
        break_block(b, style, measure, fonts, null_hyphenator()).size();
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

  size_t budget = opts.max_items;
  for (Section& s : sections) {
    std::vector<Item> kept;
    for (Item& it : s.items) {
      ++ed.stats.items_in;
      if (cutoff != kNoDate && it.published != kNoDate && it.published < cutoff) {
        ++ed.stats.dropped_stale;
        continue;
      }
      if (kept.size() >= budget) {
        ++ed.stats.dropped_over_budget;
        continue;
      }
      kept.push_back(std::move(it));
    }
    std::stable_sort(kept.begin(), kept.end(), newer);
    budget -= kept.size() < budget ? kept.size() : budget;
    s.items = std::move(kept);
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
  const PageRenderer renderer(fonts);
  std::vector<FlowElement> front;
  size_t lead_element_count = 0;

  if (lead != nullptr) {
    const int lead_measure = kPageWidth - 2 * PageTemplate().margin_left;
    front.push_back(element(TextRole::Kicker, sections[lead_section].name));
    front.push_back(element(fit_lead_headline(lead->title, lead_measure, fonts),
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

    front.push_back(element(TextRole::Kicker, s.name));
    size_t listed = 0;
    for (size_t ii = 0; ii < s.items.size(); ++ii) {
      if (si == lead_section && ii == lead_index) continue;  // already the lead
      if (listed >= opts.front_page_per_section) break;
      const Item& it = s.items[ii];
      front.push_back(element(TextRole::Subhead, it.title));
      const std::string ex = standfirst(it, 150);
      if (!ex.empty()) {
        FlowElement e = element(TextRole::Body, ex);
        e.opens_story = true;
        front.push_back(std::move(e));
      }
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
                          fonts);
  // The banner may take most of the page below the nameplate, but not all of
  // it: something has to be left for the section teasers that make a front
  // page a front page.
  const int banner_cap = (kPageHeight - front_tmpl.first_page_header -
                          front_tmpl.margin_top - front_tmpl.margin_bottom) *
                         5 / 8;
  if (front_tmpl.banner_height > banner_cap) {
    front_tmpl.banner_height = banner_cap;
  }

  const Paginator paginator(fonts, null_hyphenator());
  std::vector<size_t> front_element_page;
  paginator.paginate(front, front_tmpl, &ed.pages, &front_element_page);

  // A front page is one page. Anything that didn't fit is dropped rather than
  // spilling into a second and third "front" page — but it is counted, because
  // silently losing stories is exactly the kind of thing a reader should be
  // able to notice.
  const size_t front_pages = ed.pages.empty() ? 0 : 1;
  if (ed.pages.size() > 1) {
    for (size_t i = 0; i < front_element_page.size(); ++i) {
      if (front_element_page[i] >= 1 && front[i].role == TextRole::Subhead) {
        ++ed.stats.front_page_overflow;
      }
    }
    ed.pages.resize(1);
  }

  // --- section pages --------------------------------------------------------
  std::vector<FlowElement> body;
  std::vector<std::pair<std::string, size_t>> section_starts;  // name, flow idx

  for (const Section& s : sections) {
    if (s.items.empty()) continue;
    section_starts.emplace_back(s.name, body.size());

    bool first_in_section = true;
    for (const Item& it : s.items) {
      // Each story opens a page, per the brief. A newspaper doesn't run two
      // unrelated stories into each other mid-column.
      const bool opens_section = first_in_section;
      first_in_section = false;

      if (opens_section) {
        FlowElement head = element(TextRole::SectionHead, s.name);
        head.page_break_before = true;
        body.push_back(std::move(head));
        body.push_back(element(TextRole::ArticleHead, it.title));
      } else {
        FlowElement head = element(TextRole::ArticleHead, it.title);
        head.page_break_before = true;
        body.push_back(std::move(head));
      }

      const std::string by = byline_for(it);
      if (!by.empty()) body.push_back(element(TextRole::Byline, by));

      bool opened = false;
      for (const Block& b : it.blocks) {
        FlowElement e;
        e.role = role_for_block(b);
        e.block = b;
        if (!opened && e.role == TextRole::Body) {
          e.opens_story = true;
          opened = true;
        }
        body.push_back(std::move(e));
      }

      if (it.looks_truncated()) {
        // Say so, rather than letting the reader wonder why a story stops.
        body.push_back(element(TextRole::Caption,
                               "The publisher's feed ends here."));
      }
      ++ed.stats.items_published;
      if (it.looks_truncated()) ++ed.stats.truncated_published;
    }
  }

  PageTemplate body_tmpl;
  body_tmpl.columns = 1;
  body_tmpl.first_page_header = 0;
  std::vector<size_t> element_page;
  paginator.paginate(body, body_tmpl, &ed.pages, &element_page);

  for (const std::pair<std::string, size_t>& start : section_starts) {
    const size_t page =
        start.second < element_page.size() ? element_page[start.second] : 0;
    ed.section_marks.push_back(Edition::SectionMark{start.first, page});
  }

  // --- furniture ------------------------------------------------------------
  for (size_t i = 0; i < ed.pages.size(); ++i) {
    Page& p = ed.pages[i];
    p.is_front_page = i < front_pages;
    p.folio_right = std::to_string(i + 1) + " / " +
                    std::to_string(ed.pages.size());
    if (p.is_front_page) {
      p.folio_left = format_masthead_date(opts.now);
    }
  }

  for (size_t i = 0; i < ed.pages.size(); ++i) {
    if (ed.pages[i].is_front_page) continue;
    for (const Edition::SectionMark& m : ed.section_marks) {
      if (i >= m.first_page) ed.pages[i].folio_left = m.name;
    }
  }

  return ed;
}

}  // namespace rsspaper
