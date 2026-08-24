#include "core/render/page_renderer.h"

#include "core/base/str.h"
#include "core/layout/type_scale.h"

namespace diarium {
namespace {

// The nameplate is set in the display face, in caps, letterspaced to fill the
// measure — which is how a broadsheet nameplate works and why Diarium does
// not need a dedicated 100 px masthead font eating 200 KB of flash.
constexpr int kNameplateTop = 14;
constexpr int kNameplateRuleGap = 8;

std::string to_upper_ascii(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return out;
}

}  // namespace

int PageRenderer::masthead_height() const {
  const Face& lead = fonts_.face(FaceId::Lead);
  const Face& meta = fonts_.face(FaceId::Meta);
  return kNameplateTop + lead.ascent() + lead.descent() + kNameplateRuleGap +
         meta.ascent() + meta.descent() + 10;
}

int PageRenderer::render_masthead(const MastheadInfo& info,
                                  Framebuffer* fb) const {
  const Face& lead = fonts_.face(FaceId::Lead);
  const Face& meta = fonts_.face(FaceId::Meta);
  const Face& meta_b = fonts_.face(FaceId::MetaBold);
  if (!lead.valid()) return 0;

  const std::string title = to_upper_ascii(info.title);
  const int measure = page_width() - 2 * kSideMargin;

  // Solve for the tracking that makes the title span the measure exactly.
  const int natural = lead.measure(title);
  const int gaps = static_cast<int>(utf8_length(title)) - 1;
  int tracking = 0;
  if (gaps > 0 && natural < measure * kSubpixel) {
    tracking = (measure * kSubpixel - natural) / gaps;
    // Past a point letterspacing stops reading as a nameplate and starts
    // reading as a mistake.
    const int cap = lead.px_size() * kSubpixel / 2;
    if (tracking > cap) tracking = cap;
  }

  const int baseline = kNameplateTop + lead.ascent();
  const int drawn_width = natural + tracking * (gaps > 0 ? gaps : 0);
  const int start_x =
      (page_width() * kSubpixel - drawn_width) / 2;
  fb->draw_text_tracked(lead, title, start_x, baseline, kInk, tracking);

  int y = baseline + lead.descent() + kNameplateRuleGap;
  fb->fill_rect(kSideMargin, y, measure, 2, kInk);
  y += 2 + 7;

  // Date on the left, strap on the right, in the manner of a folio line.
  if (!info.date_line.empty() && meta_b.valid()) {
    fb->draw_text(meta_b, info.date_line, kSideMargin * kSubpixel,
                  y + meta_b.ascent(), kInk);
  }
  if (!info.strap.empty() && meta.valid()) {
    const int w = meta.measure(info.strap);
    fb->draw_text(meta, info.strap,
                  (page_width() - kSideMargin) * kSubpixel - w,
                  y + meta.ascent(), kInk);
  }
  y += meta.ascent() + meta.descent() + 3;
  fb->fill_rect(kSideMargin, y, measure, 1, 90);

  return masthead_height();
}

void PageRenderer::render_folio(const Page& page, Framebuffer* fb) const {
  const Face& meta = fonts_.face(FaceId::Meta);
  if (!meta.valid()) return;
  if (page.folio_left.empty() && page.folio_right.empty()) return;

  const int y = page_height() - 26;
  fb->fill_rect(kSideMargin, y - 11, page_width() - 2 * kSideMargin, 1, 185);

  if (!page.folio_left.empty()) {
    fb->draw_text(meta, page.folio_left, kSideMargin * kSubpixel,
                  y + meta.ascent() - 4, 90);
  }
  if (!page.folio_right.empty()) {
    const int w = meta.measure(page.folio_right);
    fb->draw_text(meta, page.folio_right,
                  (page_width() - kSideMargin) * kSubpixel - w,
                  y + meta.ascent() - 4, 90);
  }
}

void PageRenderer::render(const Page& page, Framebuffer* fb) const {
  fb->fill(kPaper);

  for (const Rule& r : page.rules) {
    fb->fill_rect(r.x, r.y, r.w, r.h, r.grey);
  }
  for (const ImageSlot& s : page.images) {
    fb->frame_rect(s.x, s.y, s.w, s.h, 170);
  }
  for (const Line& line : page.lines) {
    for (const PositionedRun& run : line.runs) {
      const Face& face = fonts_.face(run.face);
      if (!face.valid()) continue;
      fb->draw_text(face, run.text, run.x, line.baseline, kInk);
    }
  }
  render_folio(page, fb);
}

}  // namespace diarium
