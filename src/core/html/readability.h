// Finding the article inside a whole web page.
//
// A publisher that truncates its feed gives two paragraphs and an ellipsis.
// The page it links to has the rest — wrapped in navigation, related-story
// rails, a cookie notice and a footer. This decides which of those is the
// story.
//
// There is no DOM to score, because `html_to_blocks` is a push parser that
// throws structure away as it goes — which is exactly what makes it able to
// run over a socket. So the heuristic works on the block sequence instead:
// prose is long and consecutive, furniture is short and scattered. Scoring
// each block by how far its length exceeds a floor and taking the best
// contiguous run finds the body without ever building a tree.
#pragma once

#include <cstddef>
#include <vector>

#include "core/html/block.h"

namespace diarium {

struct ReadabilityLimits {
  // Blocks shorter than this are furniture until proven otherwise. A nav item
  // is a word or two; a sentence of prose is far longer.
  size_t prose_floor = 90;
  // Nothing scales with input: a hostile page cannot produce an article
  // longer than a paper could print.
  size_t max_blocks = 400;
};

// Returns the blocks that look like the article, in order, or empty when the
// page has nothing that reads like prose. Empty means "keep what the feed
// gave us" — never "print nothing".
std::vector<Block> extract_article(const std::vector<Block>& page,
                                   ReadabilityLimits limits = ReadabilityLimits());

}  // namespace diarium
