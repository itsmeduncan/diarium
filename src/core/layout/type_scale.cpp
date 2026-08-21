#include "core/layout/type_scale.h"

namespace diarium {
namespace {

Align g_body_align = Align::Left;

// Body is 27 px on a ~212 PPI panel, so roughly 9.2 pt. Leading is 38 px,
// about 1.41x — looser than a book because e-ink has lower contrast than
// paper and tight leading reads as grey mush at this size.
constexpr RoleStyle kStyles[static_cast<size_t>(TextRole::Count)] = {
    // Nameplate: set in the display face, letterspaced by the renderer so it
    // spans the measure the way a broadsheet nameplate does.
    {FaceId::Lead, 74, 0, 10, 0, 0, Align::Center, 0, 0},
    // LeadHead
    {FaceId::Lead, 68, 4, 8, 0, 0, Align::Left, 0, 0},
    // SectionHead
    {FaceId::Head, 48, 6, 10, 0, 0, Align::Left, 0, 0},
    // ArticleHead
    {FaceId::Head, 48, 4, 8, 0, 0, Align::Left, 0, 0},
    // Subhead
    {FaceId::BodyBold, 34, 12, 4, 0, 0, Align::Left, 2, 2},
    // Deck
    {FaceId::Deck, 40, 2, 8, 0, 0, Align::Left, 0, 0},
    // Byline
    {FaceId::Meta, 26, 0, 10, 0, 0, Align::Left, 0, 0},
    // Kicker
    {FaceId::MetaBold, 24, 0, 4, 0, 0, Align::Left, 0, 0},
    // LedeKicker: the source, small and quiet above the headline.
    {FaceId::MetaBold, 24, 10, 2, 0, 0, Align::Left, 0, 0},
    // LedeHead: what the reader taps. Kept to body size — a section page is
    // an index, and an index of 44 px headlines is four stories a page.
    // Never split: half a headline at the foot of a page is not a headline.
    {FaceId::BodyBold, 34, 0, 2, 0, 0, Align::Left, 9, 9},
    // LedeText
    {FaceId::Meta, 27, 0, 6, 0, 0, Align::Left, 9, 9},
    // Body. The first line of every paragraph but the story's opener is
    // indented one em — with no space between paragraphs, that indent is the
    // only thing telling a reader where one ends and the next begins.
    {FaceId::Body, 36, 0, 0, 27, 0, Align::Left, 2, 2},
    // ListItem: hanging indent, marker drawn in the left margin.
    {FaceId::Body, 36, 2, 2, 0, 30, Align::Left, 2, 2},
    // Quote: indented both sides and set in italic, no quotation marks —
    // the indent is the signal.
    {FaceId::BodyItalic, 36, 8, 8, 0, 40, Align::Left, 2, 2},
    // Code
    {FaceId::Meta, 25, 8, 8, 0, 24, Align::Left, 0, 0},
    // Caption
    {FaceId::Meta, 25, 2, 10, 0, 0, Align::Left, 0, 0},
    // Folio
    {FaceId::Meta, 24, 0, 0, 0, 0, Align::Left, 0, 0},
};

}  // namespace

const RoleStyle& role_style(TextRole role) {
  const size_t i = static_cast<size_t>(role);
  static RoleStyle resolved;
  resolved = kStyles[i < static_cast<size_t>(TextRole::Count) ? i : 0];
  if (role == TextRole::Body || role == TextRole::ListItem) {  // NOLINT
    resolved.align = g_body_align;
  }
  return resolved;
}

TextRole role_for_block(const Block& block) {
  switch (block.type) {
    case BlockType::Heading:
      // Feed HTML uses h1-h6 with no consistency, and an article body inside
      // an edition never outranks the article's own headline.
      return TextRole::Subhead;
    case BlockType::Blockquote:
      return TextRole::Quote;
    case BlockType::ListItem:
      return TextRole::ListItem;
    case BlockType::Code:
      return TextRole::Code;
    case BlockType::Caption:
    case BlockType::Image:
      return TextRole::Caption;
    case BlockType::Rule:
    case BlockType::Paragraph:
      break;
  }
  return TextRole::Body;
}

void set_body_alignment(Align align) { g_body_align = align; }
Align body_alignment() { return g_body_align; }

}  // namespace diarium
