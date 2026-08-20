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

}  // namespace rsspaper
