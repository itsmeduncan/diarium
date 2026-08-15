#include "core/edition/edition_store.h"

#include <vector>

namespace rsspaper {
namespace {

void put_u8(std::string& out, uint8_t v) {
  out.push_back(static_cast<char>(v));
}
void put_u32(std::string& out, uint32_t v) {
  out.push_back(static_cast<char>(v & 0xFF));
  out.push_back(static_cast<char>((v >> 8) & 0xFF));
  out.push_back(static_cast<char>((v >> 16) & 0xFF));
  out.push_back(static_cast<char>((v >> 24) & 0xFF));
}
void put_i32(std::string& out, int32_t v) {
  put_u32(out, static_cast<uint32_t>(v));
}
void put_u16(std::string& out, uint16_t v) {
  out.push_back(static_cast<char>(v & 0xFF));
  out.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void put_i64(std::string& out, int64_t v) {
  const uint64_t u = static_cast<uint64_t>(v);
  put_u32(out, static_cast<uint32_t>(u & 0xFFFFFFFFu));
  put_u32(out, static_cast<uint32_t>(u >> 32));
}
void put_str(std::string& out, const std::string& s) {
  put_u32(out, static_cast<uint32_t>(s.size()));
  out += s;
}

// A cursor that can't read off the end. Every failure is the same failure —
// the blob is not what we expected — so it is tracked once rather than at
// every call site.
class Reader {
 public:
  explicit Reader(const std::string& data) : d_(data) {}

  bool ok() const { return ok_; }
  size_t remaining() const { return ok_ ? d_.size() - at_ : 0; }

  uint8_t u8() {
    if (!want(1)) return 0;
    return static_cast<uint8_t>(d_[at_++]);
  }
  uint16_t u16() {
    if (!want(2)) return 0;
    const uint16_t v = static_cast<uint16_t>(
        static_cast<uint8_t>(d_[at_]) |
        (static_cast<uint8_t>(d_[at_ + 1]) << 8));
    at_ += 2;
    return v;
  }
  uint32_t u32() {
    if (!want(4)) return 0;
    const uint32_t v = static_cast<uint32_t>(static_cast<uint8_t>(d_[at_])) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(d_[at_ + 1]))
                        << 8) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(d_[at_ + 2]))
                        << 16) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(d_[at_ + 3]))
                        << 24);
    at_ += 4;
    return v;
  }
  int32_t i32() { return static_cast<int32_t>(u32()); }
  int64_t i64() {
    const uint32_t lo = u32();
    const uint32_t hi = u32();
    return static_cast<int64_t>((static_cast<uint64_t>(hi) << 32) | lo);
  }
  std::string str() {
    const uint32_t n = u32();
    // A length longer than the blob is the signature of a corrupt file, and
    // the one that would otherwise allocate gigabytes.
    if (!ok_ || n > remaining()) {
      ok_ = false;
      return std::string();
    }
    std::string s = d_.substr(at_, n);
    at_ += n;
    return s;
  }

  // Guards every count read from the blob against the space left, so a
  // corrupt length can't make us reserve memory we don't have.
  bool plausible_count(uint32_t n, size_t min_bytes_each) {
    if (!ok_) return false;
    if (min_bytes_each > 0 && n > remaining() / min_bytes_each) {
      ok_ = false;
      return false;
    }
    return true;
  }

 private:
  bool want(size_t n) {
    if (!ok_ || d_.size() - at_ < n) {
      ok_ = false;
      return false;
    }
    return true;
  }

  const std::string& d_;
  size_t at_ = 0;
  bool ok_ = true;
};

}  // namespace

std::string serialize_edition(const Edition& ed) {
  std::string out;
  // Pages dominate; roughly 3 KB each in practice.
  out.reserve(ed.pages.size() * 3072 + 1024);

  put_u32(out, kEditionMagic);
  put_u16(out, kEditionVersion);
  put_u16(out, 0);  // flags

  put_i64(out, ed.date);
  put_str(out, ed.title);
  put_u32(out, static_cast<uint32_t>(ed.browse_page_count));
  put_u32(out, static_cast<uint32_t>(ed.colophon_page));

  put_u32(out, static_cast<uint32_t>(ed.stats.items_in));
  put_u32(out, static_cast<uint32_t>(ed.stats.dropped_seen));
  put_u32(out, static_cast<uint32_t>(ed.stats.dropped_stale));
  put_u32(out, static_cast<uint32_t>(ed.stats.dropped_over_budget));
  put_u32(out, static_cast<uint32_t>(ed.stats.items_published));
  put_u32(out, static_cast<uint32_t>(ed.stats.front_page_overflow));
  put_u32(out, static_cast<uint32_t>(ed.stats.truncated_published));

  put_u32(out, static_cast<uint32_t>(ed.section_marks.size()));
  for (const Edition::SectionMark& m : ed.section_marks) {
    put_str(out, m.name);
    put_u32(out, static_cast<uint32_t>(m.first_page));
  }

  put_u32(out, static_cast<uint32_t>(ed.stories.size()));
  for (const StoryRef& s : ed.stories) {
    put_i64(out, static_cast<int64_t>(s.key));
    put_str(out, s.title);
    put_str(out, s.section);
    put_str(out, s.source);
    put_u32(out, static_cast<uint32_t>(s.lede_page));
    put_i32(out, s.lede_bounds.x);
    put_i32(out, s.lede_bounds.y);
    put_i32(out, s.lede_bounds.w);
    put_i32(out, s.lede_bounds.h);
    put_u32(out, static_cast<uint32_t>(s.first_page));
    put_u32(out, static_cast<uint32_t>(s.page_count));
    put_u8(out, s.truncated ? 1 : 0);
  }

  put_u32(out, static_cast<uint32_t>(ed.pages.size()));
  for (const Page& p : ed.pages) {
    put_u8(out, p.is_front_page ? 1 : 0);
    put_str(out, p.folio_left);
    put_str(out, p.folio_right);

    put_u32(out, static_cast<uint32_t>(p.lines.size()));
    for (const Line& line : p.lines) {
      put_i32(out, line.baseline);
      put_u32(out, static_cast<uint32_t>(line.runs.size()));
      for (const PositionedRun& r : line.runs) {
        put_u8(out, static_cast<uint8_t>(r.face));
        put_i32(out, r.x);
        put_str(out, r.text);
      }
    }

    put_u32(out, static_cast<uint32_t>(p.rules.size()));
    for (const Rule& r : p.rules) {
      put_i32(out, r.x);
      put_i32(out, r.y);
      put_i32(out, r.w);
      put_i32(out, r.h);
      put_u8(out, r.grey);
    }

    put_u32(out, static_cast<uint32_t>(p.images.size()));
    for (const ImageSlot& s : p.images) {
      put_i32(out, s.x);
      put_i32(out, s.y);
      put_i32(out, s.w);
      put_i32(out, s.h);
      put_str(out, s.alt);
    }
  }

  return out;
}

bool deserialize_edition(const std::string& blob, Edition* out,
                         std::string* error) {
  auto fail = [&](const char* why) {
    if (error != nullptr) *error = why;
    return false;
  };

  Reader r(blob);
  if (r.u32() != kEditionMagic) return fail("not a saved edition");
  const uint16_t version = r.u16();
  if (version != kEditionVersion) {
    return fail("saved edition was written by a different build");
  }
  r.u16();  // flags

  Edition ed;
  ed.date = r.i64();
  ed.title = r.str();
  ed.browse_page_count = r.u32();
  ed.colophon_page = r.u32();

  ed.stats.items_in = r.u32();
  ed.stats.dropped_seen = r.u32();
  ed.stats.dropped_stale = r.u32();
  ed.stats.dropped_over_budget = r.u32();
  ed.stats.items_published = r.u32();
  ed.stats.front_page_overflow = r.u32();
  ed.stats.truncated_published = r.u32();

  const uint32_t section_count = r.u32();
  if (!r.plausible_count(section_count, 8)) return fail("section table is corrupt");
  for (uint32_t i = 0; i < section_count && r.ok(); ++i) {
    Edition::SectionMark m;
    m.name = r.str();
    m.first_page = r.u32();
    ed.section_marks.push_back(std::move(m));
  }

  const uint32_t story_count = r.u32();
  if (!r.plausible_count(story_count, 33)) return fail("story table is corrupt");
  for (uint32_t i = 0; i < story_count && r.ok(); ++i) {
    StoryRef s;
    s.key = static_cast<uint64_t>(r.i64());
    s.title = r.str();
    s.section = r.str();
    s.source = r.str();
    s.lede_page = r.u32();
    s.lede_bounds.x = r.i32();
    s.lede_bounds.y = r.i32();
    s.lede_bounds.w = r.i32();
    s.lede_bounds.h = r.i32();
    s.first_page = r.u32();
    s.page_count = r.u32();
    s.truncated = r.u8() != 0;
    ed.stories.push_back(std::move(s));
  }

  const uint32_t page_count = r.u32();
  if (!r.plausible_count(page_count, 17)) return fail("page table is corrupt");
  ed.pages.reserve(page_count);
  for (uint32_t i = 0; i < page_count && r.ok(); ++i) {
    Page p;
    p.is_front_page = r.u8() != 0;
    p.folio_left = r.str();
    p.folio_right = r.str();

    const uint32_t line_count = r.u32();
    if (!r.plausible_count(line_count, 8)) return fail("line table is corrupt");
    for (uint32_t l = 0; l < line_count && r.ok(); ++l) {
      Line line;
      line.baseline = r.i32();
      const uint32_t run_count = r.u32();
      if (!r.plausible_count(run_count, 9)) return fail("run table is corrupt");
      for (uint32_t k = 0; k < run_count && r.ok(); ++k) {
        PositionedRun run;
        const uint8_t face = r.u8();
        run.face = face < kFaceCount ? static_cast<FaceId>(face) : FaceId::Body;
        run.x = r.i32();
        run.text = r.str();
        line.runs.push_back(std::move(run));
      }
      p.lines.push_back(std::move(line));
    }

    const uint32_t rule_count = r.u32();
    if (!r.plausible_count(rule_count, 17)) return fail("rule table is corrupt");
    for (uint32_t k = 0; k < rule_count && r.ok(); ++k) {
      Rule rule;
      rule.x = r.i32();
      rule.y = r.i32();
      rule.w = r.i32();
      rule.h = r.i32();
      rule.grey = r.u8();
      p.rules.push_back(rule);
    }

    const uint32_t image_count = r.u32();
    if (!r.plausible_count(image_count, 20)) return fail("image table is corrupt");
    for (uint32_t k = 0; k < image_count && r.ok(); ++k) {
      ImageSlot slot;
      slot.x = r.i32();
      slot.y = r.i32();
      slot.w = r.i32();
      slot.h = r.i32();
      slot.alt = r.str();
      p.images.push_back(std::move(slot));
    }

    ed.pages.push_back(std::move(p));
  }

  if (!r.ok()) return fail("saved edition is truncated");
  if (ed.browse_page_count > ed.pages.size()) {
    return fail("saved edition claims more browse pages than it has");
  }
  // A story pointing outside the pages it was saved with would send the
  // reader into nothing.
  for (const StoryRef& s : ed.stories) {
    if (s.first_page + s.page_count > ed.pages.size() ||
        s.lede_page >= ed.pages.size()) {
      return fail("saved edition has a story pointing past its pages");
    }
  }

  *out = std::move(ed);
  return true;
}

}  // namespace rsspaper
