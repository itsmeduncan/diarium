#include "core/ui/notice.h"

#include "core/layout/page.h"
#include "core/text/faces.h"

namespace rsspaper {

void render_notice(const FontPack& fonts, const char* headline,
                   const char* body, Framebuffer* fb) {
  fb->fill(kPaper);

  const int right = kPageWidth - kSideMargin;

  // The rule is drawn whether or not a face loaded, so the page reads as
  // deliberate rather than broken even when no type is available.
  fb->fill_rect(kSideMargin, 132, right - kSideMargin, 3, kInk);

  const Face& head = fonts.face(FaceId::Head);
  const Face& text = fonts.face(FaceId::Body);

  if (head.valid() && headline != nullptr) {
    fb->draw_text(head, headline, kSideMargin * kSubpixel, 112, kInk);
  }
  if (text.valid() && body != nullptr) {
    fb->draw_text(text, body, kSideMargin * kSubpixel, 132 + 60, kInk);
  }
}

void render_sleep_page(const FontPack& fonts, const std::string& title,
                       const std::string& date_line, Framebuffer* fb) {
  fb->fill(kPaper);

  const int left = kSideMargin;
  const int right = kPageWidth - kSideMargin;
  const int width = right - left;

  const Face& lead = fonts.face(FaceId::Lead);
  const Face& meta = fonts.face(FaceId::Meta);

  // A framed nameplate, set a little above true centre — optical centre sits
  // high, and this is a cover rather than a front page.
  const int mid = kPageHeight / 2 - 18;

  // Two weights, as a masthead rule would be: a heavy line above and a hair
  // below, so the block reads as the top of something rather than a box.
  fb->fill_rect(left, mid - 104, width, 4, kInk);
  fb->fill_rect(left, mid + 66, width, 1, 120);

  if (lead.valid()) {
    std::string caps;
    for (char c : title) {
      caps.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c);
    }
    const int tracking = 6 * kSubpixel;
    const int measured = lead.measure(caps) + tracking * (int)caps.size();
    const int x = left * kSubpixel + (width * kSubpixel - measured) / 2;
    fb->draw_text_tracked(lead, caps, x, mid, kInk, tracking);
  }

  if (meta.valid() && !date_line.empty()) {
    const int measured = meta.measure(date_line);
    const int x = left * kSubpixel + (width * kSubpixel - measured) / 2;
    fb->draw_text(meta, date_line, x, mid + 48, 90);
  }

  if (meta.valid()) {
    const char* hint = "Touch to wake";
    const int measured = meta.measure(hint);
    const int x = left * kSubpixel + (width * kSubpixel - measured) / 2;
    fb->draw_text(meta, hint, x, kPageHeight - 70, 130);
  }
}

}  // namespace rsspaper
