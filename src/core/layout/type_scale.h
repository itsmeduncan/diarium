// The type scale: every vertical measurement on the page in one table.
//
// Leading, space above and below, and indents are set here rather than being
// computed from the font, because a newspaper's rhythm is a design decision
// and not a property of the typeface. Changing how the paper feels should mean
// editing this file and nothing else.
#pragma once

#include <cstdint>

#include "core/html/block.h"
#include "core/layout/page.h"
#include "core/text/faces.h"

namespace rsspaper {

enum class TextRole : uint8_t {
  Nameplate,    // the masthead on page one
  LeadHead,     // front-page lead story headline
  SectionHead,  // "TECHNOLOGY" section opener
  ArticleHead,  // an article's own headline
  Subhead,      // headings inside an article body
  Deck,         // standfirst under a lead headline
  Byline,       // author and source
  Kicker,       // small caps label above a headline
  Body,
  ListItem,
  Quote,
  Code,
  Caption,
  Folio,        // running foot
  Count,
};

struct RoleStyle {
  FaceId face = FaceId::Body;
  int leading = 0;        // px, baseline to baseline
  int space_before = 0;   // px
  int space_after = 0;    // px
  int indent = 0;         // px, first line only
  int left_margin = 0;    // px, whole block
  Align align = Align::Left;
  // Minimum lines of a paragraph that may sit alone at the foot of a column
  // (orphan control) or at its head (widow control). 0 disables.
  uint8_t min_orphan = 2;
  uint8_t min_widow = 2;
};

const RoleStyle& role_style(TextRole role);

// Which role a parsed block is set in. Headings collapse to two levels
// because feed HTML uses h1-h6 arbitrarily and a reader does not need six.
TextRole role_for_block(const Block& block);

// Body text is set ragged-right by default; justification is a config choice
// because it needs hyphenation to look right in a narrow column.
void set_body_alignment(Align align);
Align body_alignment();

}  // namespace rsspaper
