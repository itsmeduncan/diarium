#include "core/layout/line_breaker.h"

#include "core/base/str.h"

namespace diarium {
namespace {

// Which face a style run maps to, within the block's base face. Bold and
// italic only exist for body-sized text; display sizes fall back to the base
// face rather than shipping four more cuts of a 66 px headline.
FaceId face_for(FaceId base, uint8_t flags) {
  const bool bold = (flags & kStyleBold) != 0;
  const bool italic = (flags & kStyleItalic) != 0;
  if (base == FaceId::Body || base == FaceId::BodyItalic ||
      base == FaceId::BodyBold) {
    if (bold) return FaceId::BodyBold;
    if (italic) return FaceId::BodyItalic;
    return base;
  }
  if (base == FaceId::Head && italic) return FaceId::HeadItalic;
  return base;
}

uint8_t flags_at(const Block& block, size_t offset) {
  for (const StyleRun& r : block.runs) {
    if (offset >= r.start && offset < static_cast<size_t>(r.start) + r.length) {
      return r.flags;
    }
  }
  return kStyleNone;
}

bool is_word_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

class NullHyphenator final : public Hyphenator {};

// Splits `atoms[index]` so that its first part, plus a hyphen, fits in
// `room`. Returns false when no break point fits, which is the common case
// and has to be cheap.
bool try_hyphenate(std::vector<Atom>* atoms, size_t index, int room,
                   const FontPack& fonts, const Hyphenator& hyphenator) {
  Atom& atom = (*atoms)[index];
  if (room <= 0 || atom.text.size() < 4) return false;

  // Punctuation is not part of the word the patterns know, but it travels
  // with it: "hyphenation," has to hyphenate as "hyphenation".
  size_t core_begin = 0, core_end = atom.text.size();
  while (core_begin < core_end && !is_word_char(atom.text[core_begin])) ++core_begin;
  while (core_end > core_begin && !is_word_char(atom.text[core_end - 1])) --core_end;
  if (core_end - core_begin < 4) return false;

  std::vector<size_t> points;
  hyphenator.break_points(atom.text.substr(core_begin, core_end - core_begin),
                          &points);
  if (points.empty()) return false;

  const Face& face = fonts.face(atom.face);
  // Longest first: take as much of the word as will fit.
  for (size_t k = points.size(); k-- > 0;) {
    const size_t cut = core_begin + points[k];
    const std::string head = atom.text.substr(0, cut) + "-";
    const int head_width = face.measure(head);
    if (head_width > room) continue;

    Atom tail;
    tail.text = atom.text.substr(cut);
    tail.face = atom.face;
    tail.width = face.measure(tail.text);
    tail.space_after = atom.space_after;
    tail.break_after = atom.break_after;
    tail.kern_to_next = atom.kern_to_next;

    atom.text = head;
    atom.width = head_width;
    atom.space_after = 0;
    atom.break_after = false;
    atom.kern_to_next = 0;

    atoms->insert(atoms->begin() + static_cast<std::ptrdiff_t>(index) + 1,
                  std::move(tail));
    return true;
  }
  return false;
}

// The maximum a space may stretch when justifying, as a multiple of its
// natural width. Beyond this the line is left short instead: rivers of white
// are worse than a ragged edge.
constexpr int kMaxStretchNumerator = 9;
constexpr int kMaxStretchDenominator = 4;  // 2.25x

}  // namespace

const Hyphenator& null_hyphenator() {
  static const NullHyphenator instance;
  return instance;
}

std::vector<Atom> atomize(const Block& block, const RoleStyle& style,
                          const FontPack& fonts) {
  std::vector<Atom> atoms;
  const std::string& text = block.text;

  size_t i = 0;
  while (i < text.size()) {
    // Skip whitespace, remembering whether it separated the previous atom
    // and whether it contained a hard break.
    bool saw_break = false;
    size_t spaces = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\n')) {
      if (text[i] == '\n') saw_break = true;
      ++spaces;
      ++i;
    }
    if (spaces > 0 && !atoms.empty()) {
      Atom& prev = atoms.back();
      if (saw_break) {
        prev.break_after = true;
        prev.space_after = 0;
      } else {
        prev.space_after =
            fonts.face(prev.face).advance_of(static_cast<uint32_t>(' '));
      }
    }
    if (i >= text.size()) break;

    // A word runs to the next space, but is cut wherever the style changes so
    // each atom has exactly one face.
    const size_t word_start = i;
    uint8_t current_flags = flags_at(block, i);
    size_t seg_start = i;

    auto emit = [&](size_t end, bool joins_next) {
      if (end <= seg_start) return;
      Atom a;
      a.text = text.substr(seg_start, end - seg_start);
      a.face = face_for(style.face, current_flags);
      a.width = fonts.face(a.face).measure(a.text);
      a.space_after = 0;
      a.break_after = false;
      if (joins_next) {
        // Mark a zero-width join so the breaker keeps the fragments together.
        a.kern_to_next = -1;
      }
      atoms.push_back(std::move(a));
    };

    while (i < text.size() && text[i] != ' ' && text[i] != '\n') {
      const uint8_t f = flags_at(block, i);
      if (f != current_flags && i > seg_start) {
        emit(i, true);
        seg_start = i;
        current_flags = f;
      }
      size_t next = i;
      utf8_next(text, next);
      i = next;
    }
    emit(i, false);
    (void)word_start;
  }

  if (!atoms.empty()) atoms.back().space_after = 0;
  return atoms;
}

std::vector<BrokenLine> break_atoms(const std::vector<Atom>& atoms, int measure,
                                    Align align, const FontPack& fonts,
                                    const Hyphenator& hyphenator) {
  std::vector<BrokenLine> lines;
  if (atoms.empty() || measure <= 0) return lines;

  const int measure_sub = measure * kSubpixel;

  // A mutable copy, because hyphenating splits an atom in two and the line
  // after the break has to carry the remainder.
  std::vector<Atom> work(atoms.begin(), atoms.end());

  size_t i = 0;
  while (i < work.size()) {
    // Gather atoms until the next one would not fit. Fragments joined by a
    // style change (kern_to_next < 0) are taken as one unit.
    size_t start = i;
    int natural = 0;      // width including trailing spaces between atoms
    int trailing_space = 0;
    size_t end = i;
    bool forced = false;

    while (end < work.size()) {
      // Measure the whole join group so a styled word never splits.
      size_t group_end = end;
      int group_width = 0;
      for (;;) {
        group_width += work[group_end].width;
        if (work[group_end].kern_to_next < 0 && group_end + 1 < work.size()) {
          ++group_end;
          continue;
        }
        break;
      }

      const int candidate = natural + trailing_space + group_width;
      if (candidate > measure_sub) {
        // The word won't fit. Before giving up the rest of the line, see
        // whether part of it will — this is the whole reason hyphenation
        // exists, and without it a narrow column is mostly white space.
        if (group_end == end &&
            try_hyphenate(&work, end, measure_sub - natural - trailing_space,
                          fonts, hyphenator)) {
          natural += trailing_space + work[end].width;
          trailing_space = 0;
          end = end + 1;
          forced = true;  // the line ends at the hyphen
          break;
        }
        if (end > start) break;  // it will fit on a line of its own

        // The first thing on the line and still too wide, with nowhere to
        // hyphenate: let it overhang rather than dropping it or looping.
        natural += group_width;
        trailing_space = work[group_end].space_after;
        end = group_end + 1;
        break;
      }

      natural += trailing_space + group_width;
      trailing_space = work[group_end].space_after;
      const bool break_here = work[group_end].break_after;
      end = group_end + 1;
      if (break_here) {
        forced = true;
        break;
      }
    }

    // A single unit wider than the whole measure even after hyphenation:
    // let it overhang rather than dropping it.
    if (end == start) {
      end = start + 1;
      natural = work[start].width;
    }

    BrokenLine line;
    line.width = natural;
    line.last = forced || end >= work.size();

    const int slack = measure_sub - natural;
    // Count the gaps that can absorb slack.
    size_t gaps = 0;
    for (size_t k = start; k + 1 < end; ++k) {
      if (work[k].space_after > 0) ++gaps;
    }

    int extra_per_gap = 0;
    if (align == Align::Justify && !line.last && gaps > 0 && slack > 0) {
      const int per = slack / static_cast<int>(gaps);
      // Cap the stretch; a line that would need more is left ragged.
      const int space_w =
          fonts.face(work[start].face).advance_of(static_cast<uint32_t>(' '));
      const int max_extra =
          space_w * kMaxStretchNumerator / kMaxStretchDenominator - space_w;
      extra_per_gap = per < max_extra ? per : max_extra;
    }

    int x = 0;
    if (align == Align::Center) x = slack / 2;
    else if (align == Align::Right) x = slack;

    for (size_t k = start; k < end; ++k) {
      PositionedRun run;
      run.face = work[k].face;
      run.x = x;
      run.text = work[k].text;
      x += work[k].width;
      if (k + 1 < end && work[k].space_after > 0) {
        x += work[k].space_after + extra_per_gap;
      }
      line.runs.push_back(std::move(run));
    }
    line.width = x;
    lines.push_back(std::move(line));

    i = end;
  }

  if (!lines.empty()) lines.back().last = true;
  return lines;
}

std::vector<BrokenLine> break_block(const Block& block, const RoleStyle& style,
                                    int measure, const FontPack& fonts,
                                    const Hyphenator& hyphenator) {
  const std::vector<Atom> atoms = atomize(block, style, fonts);
  return break_atoms(atoms, measure, style.align, fonts, hyphenator);
}

}  // namespace diarium
