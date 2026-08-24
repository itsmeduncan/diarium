// Draws a laid-out Page onto a Framebuffer, plus the furniture that makes it
// read as a newspaper: the nameplate, the date line, section rules, folios.
#pragma once

#include <string>

#include "core/layout/page.h"
#include "core/render/framebuffer.h"
#include "core/text/font_pack.h"

namespace diarium {

struct MastheadInfo {
  std::string title = "DIARIUM";
  std::string date_line;   // "Saturday, 15 August 2026"
  std::string strap;       // "Composed on device · 11 feeds"
};

class PageRenderer {
 public:
  explicit PageRenderer(const FontPack& fonts) : fonts_(fonts) {}

  // Paints `page` into `fb`, clearing it first.
  void render(const Page& page, Framebuffer* fb) const;

  // The nameplate block at the top of page one. Returns its height in px so
  // the page template can reserve exactly that much.
  int render_masthead(const MastheadInfo& info, Framebuffer* fb) const;
  int masthead_height() const;

 private:
  void render_folio(const Page& page, Framebuffer* fb) const;

  const FontPack& fonts_;
};

}  // namespace diarium
