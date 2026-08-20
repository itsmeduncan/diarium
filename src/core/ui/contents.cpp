#include "core/ui/contents.h"

#include "core/base/datetime.h"
#include "core/layout/page.h"
#include "core/text/faces.h"

namespace rsspaper {
namespace {

std::string upper(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c);
  }
  return out;
}

// Clips to the measure rather than wrapping: the running order is a list, and
// a list whose rows are different heights stops being scannable.
std::string fitted(const Face& face, std::string text, int measure_px) {
  const int limit = measure_px * kSubpixel;
  if (face.measure(text) <= limit) return text;
  while (!text.empty() && face.measure(text + "…") > limit) {
    text.resize(text.size() - 1);
  }
  return text + "…";
}

}  // namespace

void render_contents(const FontPack& fonts, const Edition& edition,
                     const std::vector<size_t>& order,
                     const std::vector<bool>& unread, const std::string& strap,
                     Framebuffer* fb) {
  fb->fill(kPaper);

  const int left = kSideMargin;
  const int right = kPageWidth - kSideMargin;
  const int width = right - left;

  const Face& lead = fonts.face(FaceId::Lead);
  const Face& head = fonts.face(FaceId::BodyBold);
  const Face& body = fonts.face(FaceId::Body);
  const Face& meta = fonts.face(FaceId::Meta);

  // The nameplate, in the same language as the sleep screen so the two read
  // as the same object: heavy rule, letterspaced caps, hairline under.
  int y = 96;
  if (lead.valid()) {
    const std::string caps = upper(edition.title);
    const int tracking = 10 * kSubpixel;
    const int measured =
        lead.measure(caps) + tracking * static_cast<int>(caps.size());
    const int x = (kPageWidth * kSubpixel - measured) / 2;
    fb->draw_text_tracked(lead, caps, x, y, kInk, tracking);
  }

  y += 26;
  fb->fill_rect(left, y, width, 5, kInk);
  fb->fill_rect(left, y + 9, width, 1, kInk);
  y += 22;

  // Dateline left, strap right, the way a masthead's underline is set.
  if (meta.valid()) {
    const std::string date_caps = upper(format_masthead_date(edition.date));
    fb->draw_text_tracked(meta, date_caps, left * kSubpixel, y + meta.ascent(),
                          60, 3 * kSubpixel);
    if (!strap.empty()) {
      const int w = meta.measure(strap);
      fb->draw_text(meta, strap, right * kSubpixel - w, y + meta.ascent(), 110);
    }
  }
  y += 40;
  fb->fill_rect(left, y, width, 2, kInk);
  y += 36;

  // The running order. Each row is one story, in the order you will meet it.
  const int row = 66;
  const int rule_gap = 20;
  const int number_column = 46;

  size_t shown = 0;
  for (size_t i = 0; i < order.size(); ++i) {
    if (i < unread.size() && !unread[i]) continue;
    if (y + row > kPageHeight - 96) break;

    const StoryRef& s = edition.stories[order[i]];
    ++shown;

    // A figure in the margin, so the list reads as a running order rather
    // than a pile of headlines.
    if (meta.valid()) {
      const std::string n = std::to_string(shown);
      const int w = meta.measure(n);
      fb->draw_text(meta, n, (left + number_column) * kSubpixel - w,
                    y + head.ascent(), 130);
    }

    const int text_left = left + number_column + 16;
    const int measure = right - text_left;

    if (head.valid()) {
      fb->draw_text(head, fitted(head, s.title, measure),
                    text_left * kSubpixel, y + head.ascent(), kInk);
    }
    if (meta.valid()) {
      std::string line = upper(s.section);
      if (!s.source.empty()) line += " · " + s.source;
      if (s.truncated) line += " · excerpt";
      fb->draw_text(meta, fitted(meta, line, measure), text_left * kSubpixel,
                    y + head.ascent() + 26, 110);
    }

    y += row;
    if (y + rule_gap < kPageHeight - 96) {
      fb->fill_rect(text_left, y - rule_gap, right - text_left, 1, 200);
    }
  }

  if (shown == 0 && body.valid()) {
    fb->draw_text(body, "You have read everything in this edition.",
                  left * kSubpixel, y + body.ascent(), kInk);
  }

  // The instruction, once, at the foot.
  if (meta.valid()) {
    const char* hint = "SWIPE RIGHT TO BEGIN";
    const int tracking = 4 * kSubpixel;
    const int measured =
        meta.measure(hint) + tracking * static_cast<int>(std::string(hint).size());
    const int x = (kPageWidth * kSubpixel - measured) / 2;
    fb->fill_rect(left, kPageHeight - 74, width, 1, 200);
    fb->draw_text_tracked(meta, hint, x, kPageHeight - 34, 110, tracking);
  }
}

}  // namespace rsspaper
