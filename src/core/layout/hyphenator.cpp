#include "core/layout/hyphenator.h"

#include <cstring>

#include "core/base/str.h"

namespace rsspaper {
namespace {

using hyphen_en::kExceptionCount;
using hyphen_en::kExceptions;
using hyphen_en::kLetters;
using hyphen_en::kOffsets;
using hyphen_en::kPatternCount;
using hyphen_en::kValues;

size_t pattern_length(size_t i) { return kOffsets[i + 1] - kOffsets[i]; }

// The character a pattern has at `depth`, or -1 when it is shorter than that.
// Short patterns sort before longer ones sharing their prefix, so treating
// "no character" as less than every character keeps the range search correct.
int pattern_char(size_t i, size_t depth) {
  if (depth >= pattern_length(i)) return -1;
  return kLetters[kOffsets[i] + depth];
}

// Values for pattern i start at kOffsets[i] + i: each pattern contributes one
// more value than it has letters, so the running total is the letter offset
// plus the number of patterns before it. No second table needed.
const uint8_t* pattern_values(size_t i) { return &kValues[kOffsets[i] + i]; }

// Only ASCII letters. The patterns are ASCII, and a word carrying anything
// else — an accent, an apostrophe, a digit — is one they cannot speak to.
bool is_ascii_letter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// The exception list holds words with their breaks marked: "as-so-ciate".
bool exception_breaks(const std::string& lower, std::vector<size_t>* out) {
  for (size_t i = 0; i < kExceptionCount; ++i) {
    const char* e = kExceptions[i];
    size_t at = 0;
    bool match = true;
    std::vector<size_t> breaks;
    for (const char* p = e; *p != '\0'; ++p) {
      if (*p == '-') {
        breaks.push_back(at);
        continue;
      }
      if (at >= lower.size() || lower[at] != *p) {
        match = false;
        break;
      }
      ++at;
    }
    if (match && at == lower.size()) {
      *out = breaks;
      return true;
    }
  }
  return false;
}

}  // namespace

void LiangHyphenator::break_points(const std::string& word,
                                   std::vector<size_t>* out) const {
  out->clear();
  if (word.size() < limits_.min_word_length) return;

  std::string lower;
  lower.reserve(word.size());
  for (char c : word) {
    if (!is_ascii_letter(c)) return;  // not a word these patterns know
    lower.push_back(ascii_lower(c));
  }

  if (exception_breaks(lower, out)) {
    // Even an exception has to respect the minimums.
    std::vector<size_t> kept;
    for (size_t b : *out) {
      if (b >= limits_.left_min && lower.size() - b >= limits_.right_min) {
        kept.push_back(b);
      }
    }
    *out = kept;
    return;
  }

  // "." marks a word boundary in the pattern language, and patterns anchored
  // to it are how the algorithm knows about prefixes and suffixes.
  const std::string padded = "." + lower + ".";
  // values[k] is the priority of a break before padded[k].
  std::vector<uint8_t> values(padded.size() + 1, 0);

  for (size_t i = 0; i < padded.size(); ++i) {
    size_t lo = 0, hi = kPatternCount;
    for (size_t depth = 0; i + depth < padded.size(); ++depth) {
      const int c = static_cast<uint8_t>(padded[i + depth]);

      // Narrow to the patterns whose character at `depth` is c. The table is
      // sorted, so this is two binary searches and the range only shrinks.
      size_t l = lo, h = hi;
      while (l < h) {
        const size_t mid = l + (h - l) / 2;
        if (pattern_char(mid, depth) < c) l = mid + 1;
        else h = mid;
      }
      const size_t first = l;
      h = hi;
      while (l < h) {
        const size_t mid = l + (h - l) / 2;
        if (pattern_char(mid, depth) <= c) l = mid + 1;
        else h = mid;
      }
      lo = first;
      hi = l;
      if (lo >= hi) break;

      // Within the range every pattern shares this prefix. One of length
      // exactly depth+1 is an exact match, and sorts first.
      if (pattern_length(lo) == depth + 1) {
        const uint8_t* v = pattern_values(lo);
        for (size_t k = 0; k <= depth + 1; ++k) {
          if (v[k] > values[i + k]) values[i + k] = v[k];
        }
      }
    }
  }

  // A break between lower[j-1] and lower[j] sits before padded[j + 1].
  for (size_t j = limits_.left_min; j + limits_.right_min <= lower.size(); ++j) {
    if ((values[j + 1] % 2) == 1) out->push_back(j);
  }
}

const Hyphenator& english_hyphenator() {
  static const LiangHyphenator instance;
  return instance;
}

}  // namespace rsspaper
