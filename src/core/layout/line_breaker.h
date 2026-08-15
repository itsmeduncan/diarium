// Breaking a block of text into lines.
//
// Greedy, first-fit — not Knuth-Plass. On a 65-character measure with ragged
// setting the two rarely disagree, and greedy runs in a fraction of the memory
// on a device that has to do this at 5:30 in the morning on battery. The seam
// for a better algorithm is `break_block`: it takes atoms and returns lines,
// and nothing above it knows how the decision was made.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/html/block.h"
#include "core/layout/page.h"
#include "core/layout/type_scale.h"
#include "core/text/font_pack.h"

namespace rsspaper {

// The smallest unit that can't be split: a word, or a run of a word that
// shares one face. Widths are in 1/16 px.
struct Atom {
  std::string text;
  FaceId face = FaceId::Body;
  int width = 0;
  int space_after = 0;      // advance of the following space, 0 if none
  bool break_after = false;  // a hard break (<br>) follows
  // Kerning across the join to the next atom, when no space separates them
  // (a style change mid-word).
  int kern_to_next = 0;
};

// Splits a block's text into atoms, honouring its style runs so bold and
// italic spans get the right face.
std::vector<Atom> atomize(const Block& block, const RoleStyle& style,
                          const FontPack& fonts);

// Hyphenation hook. The default never breaks a word; a real implementation
// (Liang patterns) drops in here without touching the breaker.
class Hyphenator {
 public:
  virtual ~Hyphenator() = default;
  // Byte offsets inside `word` where a hyphen may be inserted, ascending.
  virtual void break_points(const std::string& word,
                            std::vector<size_t>* out) const {
    (void)word;
    out->clear();
  }
};

const Hyphenator& null_hyphenator();

struct BrokenLine {
  std::vector<PositionedRun> runs;
  int width = 0;      // 1/16 px, the natural (unjustified) width
  bool last = false;  // last line of the paragraph — never justified
};

// Lays atoms into lines of `measure` px. Positions in the returned runs are
// relative to the line's left edge, in 1/16 px.
std::vector<BrokenLine> break_atoms(const std::vector<Atom>& atoms, int measure,
                                    Align align, const FontPack& fonts,
                                    const Hyphenator& hyphenator);

// Convenience: atomize then break.
std::vector<BrokenLine> break_block(const Block& block, const RoleStyle& style,
                                    int measure, const FontPack& fonts,
                                    const Hyphenator& hyphenator);

}  // namespace rsspaper
