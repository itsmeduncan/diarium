#include "core/html/readability.h"

namespace rsspaper {
namespace {

// What a block is worth to the claim "this is the article". Prose scores by
// how far it exceeds the floor; everything short scores against, so a run of
// navigation can never accumulate into a body.
long score_of(const Block& b, const ReadabilityLimits& limits) {
  const long len = static_cast<long>(b.text.size());
  const long floor = static_cast<long>(limits.prose_floor);

  switch (b.type) {
    case BlockType::Paragraph:
    case BlockType::Blockquote:
      return len - floor;
    case BlockType::Heading:
      // A subheading belongs to the article it sits in, but a page full of
      // headings is a section index rather than a story.
      return len > 0 ? -floor / 3 : -floor;
    case BlockType::ListItem:
      // Lists are often navigation and sometimes the substance of a piece.
      return (len - floor) / 2;
    default:
      return -floor;
  }
}

}  // namespace

std::vector<Block> extract_article(const std::vector<Block>& page,
                                   ReadabilityLimits limits) {
  if (page.empty()) return {};

  // The best contiguous run, by summed score. Furniture drags a run negative
  // and ends it; prose extends it. This is a maximum-subarray scan, which is
  // linear and needs no structure the parser has already discarded.
  long best = 0;
  size_t best_from = 0;
  size_t best_to = 0;

  long running = 0;
  size_t from = 0;

  for (size_t i = 0; i < page.size(); ++i) {
    const long value = score_of(page[i], limits);
    if (running <= 0) {
      running = value;
      from = i;
    } else {
      running += value;
    }
    if (running > best) {
      best = running;
      best_from = from;
      best_to = i;
    }
  }

  // Nothing scored positively: the page is all furniture, and the feed's own
  // excerpt is the better thing to print.
  if (best <= 0) return {};

  std::vector<Block> article;
  for (size_t i = best_from; i <= best_to && article.size() < limits.max_blocks;
       ++i) {
    article.push_back(page[i]);
  }

  // A run that is only headings is a contents page, not a story.
  bool has_prose = false;
  for (const Block& b : article) {
    if (b.type == BlockType::Paragraph && b.text.size() >= limits.prose_floor) {
      has_prose = true;
      break;
    }
  }
  if (!has_prose) return {};

  return article;
}

}  // namespace rsspaper
