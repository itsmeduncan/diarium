// Liang hyphenation.
//
// The algorithm from Frank Liang's 1983 thesis, the same one TeX uses: slide
// every pattern that matches anywhere in the word over it, take the highest
// priority at each position, and break where the result is odd. It is small,
// it is data-driven, and it has been right about English for forty years.
//
// The patterns live in `hyphen_patterns_en.cpp`, generated from
// `assets/hyphenation/hyph-en-us.tex` by `tools/hyphgen`. About 57 KB of
// const tables in flash and nothing in RAM: the search runs directly over
// them, so there is no load step and no allocation at wake.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/layout/line_breaker.h"

namespace rsspaper {

// The tables. Declared here so the generated file has something to define.
namespace hyphen_en {
extern const size_t kPatternCount;
extern const uint8_t kLetters[];
extern const uint16_t kOffsets[];
extern const uint8_t kValues[];
extern const char* const kExceptions[];
extern const size_t kExceptionCount;
}  // namespace hyphen_en

struct HyphenLimits {
  // Characters that must precede the first break and follow the last. The
  // pattern file documents 2 and 3 as what these patterns were designed for,
  // and breaking closer than that looks wrong even when it is legal.
  size_t left_min = 2;
  size_t right_min = 3;
  // Below this there is nothing to gain and something to lose.
  size_t min_word_length = 5;
};

class LiangHyphenator final : public Hyphenator {
 public:
  explicit LiangHyphenator(HyphenLimits limits = HyphenLimits())
      : limits_(limits) {}

  // Byte offsets in `word` where a hyphen may be inserted, ascending. A break
  // at offset k means the hyphen goes after word[k-1].
  void break_points(const std::string& word,
                    std::vector<size_t>* out) const override;

 private:
  HyphenLimits limits_;
};

// The shared instance. Hyphenation is stateless, so there is no reason for
// every caller to build its own.
const Hyphenator& english_hyphenator();

}  // namespace rsspaper
