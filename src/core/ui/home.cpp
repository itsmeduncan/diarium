#include "core/ui/home.h"

#include "core/base/datetime.h"
#include "core/layout/page.h"
#include "core/text/faces.h"

namespace diarium {
namespace {

std::string upper(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c);
  }
  return out;
}

}  // namespace

HomeSummary summarize_home(const Edition& edition,
                           const std::vector<size_t>& order,
                           const std::vector<bool>& unread) {
  HomeSummary out;
  for (size_t i = 0; i < order.size(); ++i) {
    if (i >= unread.size() || !unread[i]) continue;
    ++out.unread_total;
    const std::string& section = edition.stories[order[i]].section;
    bool found = false;
    for (HomeSummary::Section& s : out.sections) {
      if (s.name == section) { ++s.count; found = true; break; }
    }
    if (!found) out.sections.push_back({section, 1});
  }
  return out;
}

void render_home(const FontPack& fonts, const Edition& edition,
                 const std::vector<size_t>& order,
                 const std::vector<bool>& unread, const std::string& strap,
                 Framebuffer* fb, bool confirm_clear) {
  fb->fill(kPaper);

  const int left = kSideMargin;
  const int right = page_width() - kSideMargin;
  const int width = right - left;

  const Face& lead = fonts.face(FaceId::Lead);
  const Face& head = fonts.face(FaceId::BodyBold);
  const Face& body = fonts.face(FaceId::Body);
  const Face& meta = fonts.face(FaceId::Meta);

  // The nameplate, in the same language as the sleep screen: letterspaced
  // caps, heavy rule, hairline under. This is the masthead's only home now.
  int y = 96;
  if (lead.valid()) {
    const std::string caps = upper(edition.title);
    const int tracking = 10 * kSubpixel;
    const int measured =
        lead.measure(caps) + tracking * static_cast<int>(caps.size());
    const int x = (page_width() * kSubpixel - measured) / 2;
    fb->draw_text_tracked(lead, caps, x, y, kInk, tracking);
  }
  y += 26;
  fb->fill_rect(left, y, width, 5, kInk);
  y += 20;
  if (meta.valid()) {
    const std::string date = format_masthead_date(edition.date);
    const int x = (page_width() * kSubpixel - meta.measure(date)) / 2;
    // Baseline, not the text's top: draw_text positions glyphs upward from
    // where you tell it, so without the face's ascent the ascenders land on
    // the rule above rather than the row below it.
    fb->draw_text(meta, date, x, y + meta.ascent(), kInk);
  }
  y += meta.valid() ? meta.ascent() + meta.descent() + 8 : 10;
  fb->fill_rect(left, y, width, 1, 190);

  const HomeSummary summary = summarize_home(edition, order, unread);

  // The count, large, because it is the one number the page exists to show.
  y += 150;
  if (lead.valid()) {
    const std::string n = std::to_string(summary.unread_total);
    const int x = left * kSubpixel;
    const int after = fb->draw_text(lead, n, x, y, kInk);
    if (head.valid()) fb->draw_text(head, " unread", after, y, kInk);
  }

  // The breakdown, one section per line.
  y += 60;
  for (const HomeSummary::Section& s : summary.sections) {
    if (y > page_height() - 120) break;  // bounded by the page, like everything
    if (head.valid()) {
      const int after =
          fb->draw_text(head, s.name, left * kSubpixel, y, kInk);
      if (body.valid()) {
        fb->draw_text(body, "  " + std::to_string(s.count), after, y, kInk);
      }
    }
    y += 44;
  }

  // The strap and the hint, at the foot where the contents page kept them.
  if (meta.valid() && !strap.empty()) {
    const int x = (page_width() * kSubpixel - meta.measure(strap)) / 2;
    fb->fill_rect(left, page_height() - 74, width, 1, 200);
    fb->draw_text(meta, strap, x, page_height() - 52, kInk);
  }
  if (meta.valid()) {
    const std::string hint =
        confirm_clear
            ? upper("clear all " + std::to_string(summary.unread_total) +
                     " unread? tap again")
            : upper("swipe right to begin");
    const int tracking = 4 * kSubpixel;
    const int measured =
        meta.measure(hint) + tracking * static_cast<int>(hint.size());
    const int x = (page_width() * kSubpixel - measured) / 2;
    fb->draw_text_tracked(meta, hint, x, page_height() - 34, 110, tracking);
  }
}

}  // namespace diarium
