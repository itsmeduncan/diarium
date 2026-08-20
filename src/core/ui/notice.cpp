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

  std::string caps;
  for (char c : title) {
    caps.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c);
  }

  // The nameplate is knocked out of a full-bleed band. Glyphs composite by
  // darkening, so white type cannot simply be drawn: the band is set black
  // on white and then the whole strip is inverted.
  const int band_top = 236;
  const int band_height = 172;
  const int baseline = band_top + 116;

  if (lead.valid()) {
    const int tracking = 10 * kSubpixel;
    const int measured =
        lead.measure(caps) + tracking * static_cast<int>(caps.size());
    const int x = (kPageWidth * kSubpixel - measured) / 2;
    fb->draw_text_tracked(lead, caps, x, baseline, kInk, tracking);
  }

  for (int y = band_top; y < band_top + band_height; ++y) {
    for (int x = 0; x < kPageWidth; ++x) {
      fb->set(x, y, static_cast<uint8_t>(255 - fb->get(x, y)));
    }
  }

  // Thick-thin above, thin-thick below: a printer's rule stack, which is what
  // makes a masthead read as pressed rather than typed. Full bleed, like the
  // band, so nothing appears to float inside a margin it does not share.
  fb->fill_rect(0, band_top - 22, kPageWidth, 7, kInk);
  fb->fill_rect(0, band_top - 10, kPageWidth, 2, kInk);
  fb->fill_rect(0, band_top + band_height + 8, kPageWidth, 2, kInk);
  fb->fill_rect(0, band_top + band_height + 15, kPageWidth, 7, kInk);

  // The date, letterspaced and small, the way a dateline is set.
  if (meta.valid() && !date_line.empty()) {
    std::string date_caps;
    for (char c : date_line) {
      date_caps.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c);
    }
    const int tracking = 5 * kSubpixel;
    const int measured =
        meta.measure(date_caps) + tracking * static_cast<int>(date_caps.size());
    const int x = (kPageWidth * kSubpixel - measured) / 2;
    fb->draw_text_tracked(meta, date_caps, x, band_top + band_height + 84, 40,
                          tracking);
  }

  // A tone bar, the way a press prints one in the trim: eight swatches, which
  // is exactly what this panel can show. It is an honest test card and it
  // says what the machine is.
  const int bar_y = kPageHeight - 84;
  const int swatch = width / 8;
  for (int i = 0; i < 8; ++i) {
    const uint8_t tone = static_cast<uint8_t>(255 - (255 * i) / 7);
    fb->fill_rect(left + i * swatch, bar_y, swatch, 26, tone);
  }
  fb->frame_rect(left, bar_y, swatch * 8, 26, kInk);

  if (meta.valid()) {
    const char* hint = "TOUCH TO WAKE";
    const int tracking = 4 * kSubpixel;
    const int measured =
        meta.measure(hint) + tracking * static_cast<int>(std::string(hint).size());
    const int x = (kPageWidth * kSubpixel - measured) / 2;
    fb->draw_text_tracked(meta, hint, x, kPageHeight - 28, 110, tracking);
  }
}

}  // namespace rsspaper
