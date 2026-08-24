#include "core/edition/edition_store.h"

#include <vector>

namespace diarium {

void put_u8(ByteSink& out, uint8_t v) { out.write(&v, 1); }
void put_u32(ByteSink& out, uint32_t v) {
  const uint8_t b[4] = {
      static_cast<uint8_t>(v & 0xFF),
      static_cast<uint8_t>((v >> 8) & 0xFF),
      static_cast<uint8_t>((v >> 16) & 0xFF),
      static_cast<uint8_t>((v >> 24) & 0xFF),
  };
  out.write(b, 4);
}
void put_i32(ByteSink& out, int32_t v) {
  put_u32(out, static_cast<uint32_t>(v));
}
void put_u16(ByteSink& out, uint16_t v) {
  const uint8_t b[2] = {static_cast<uint8_t>(v & 0xFF),
                         static_cast<uint8_t>((v >> 8) & 0xFF)};
  out.write(b, 2);
}
void put_i64(ByteSink& out, int64_t v) {
  const uint64_t u = static_cast<uint64_t>(v);
  put_u32(out, static_cast<uint32_t>(u & 0xFFFFFFFFu));
  put_u32(out, static_cast<uint32_t>(u >> 32));
}
void put_str(ByteSink& out, const std::string& s) {
  put_u32(out, static_cast<uint32_t>(s.size()));
  out.write(s.data(), s.size());
}

void write_page(ByteSink& out, const Page& p) {
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

bool read_page(ByteReader& r, Page* out) {
  Page p;
  p.is_front_page = r.u8() != 0;
  p.folio_left = r.str();
  p.folio_right = r.str();

  const uint32_t line_count = r.u32();
  if (!r.plausible_count(line_count, 8)) return false;
  for (uint32_t l = 0; l < line_count && r.ok(); ++l) {
    Line line;
    line.baseline = r.i32();
    const uint32_t run_count = r.u32();
    if (!r.plausible_count(run_count, 9)) return false;
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
  if (!r.plausible_count(rule_count, 17)) return false;
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
  if (!r.plausible_count(image_count, 20)) return false;
  for (uint32_t k = 0; k < image_count && r.ok(); ++k) {
    ImageSlot slot;
    slot.x = r.i32();
    slot.y = r.i32();
    slot.w = r.i32();
    slot.h = r.i32();
    slot.alt = r.str();
    p.images.push_back(std::move(slot));
  }

  if (!r.ok()) return false;
  *out = std::move(p);
  return true;
}

std::string serialize_edition(const Edition& ed) {
  std::string out;
  // Pages dominate; roughly 3 KB each in practice.
  out.reserve(ed.pages.size() * 3072 + 1024);
  StringSink sink(&out);

  put_u32(sink, kEditionMagic);
  put_u16(sink, kEditionVersion);
  put_u16(sink, 0);  // flags

  put_i64(sink, ed.date);
  put_str(sink, ed.title);

  put_u32(sink, static_cast<uint32_t>(ed.stats.items_in));
  put_u32(sink, static_cast<uint32_t>(ed.stats.dropped_seen));
  put_u32(sink, static_cast<uint32_t>(ed.stats.dropped_stale));
  put_u32(sink, static_cast<uint32_t>(ed.stats.dropped_over_budget));
  put_u32(sink, static_cast<uint32_t>(ed.stats.items_published));
  put_u32(sink, static_cast<uint32_t>(ed.stats.truncated_published));

  put_u32(sink, static_cast<uint32_t>(ed.stories.size()));
  for (const StoryRef& s : ed.stories) {
    put_i64(sink, static_cast<int64_t>(s.key));
    put_str(sink, s.title);
    put_str(sink, s.section);
    put_str(sink, s.source);
    put_u32(sink, static_cast<uint32_t>(s.first_page));
    put_u32(sink, static_cast<uint32_t>(s.page_count));
    put_u8(sink, s.truncated ? 1 : 0);
    put_i64(sink, static_cast<int64_t>(s.published));
  }

  put_u32(sink, static_cast<uint32_t>(ed.pages.size()));
  for (const Page& p : ed.pages) {
    write_page(sink, p);
  }

  return out;
}

bool deserialize_edition(const std::string& blob, Edition* out,
                         std::string* error) {
  auto fail = [&](const char* why) {
    if (error != nullptr) *error = why;
    return false;
  };

  ByteReader r(blob);
  if (r.u32() != kEditionMagic) return fail("not a saved edition");
  const uint16_t version = r.u16();
  if (version < kMinEditionVersion || version > kEditionVersion) {
    return fail("saved edition was written by a different build");
  }
  r.u16();  // flags

  Edition ed;
  ed.date = r.i64();
  ed.title = r.str();

  ed.stats.items_in = r.u32();
  ed.stats.dropped_seen = r.u32();
  ed.stats.dropped_stale = r.u32();
  ed.stats.dropped_over_budget = r.u32();
  ed.stats.items_published = r.u32();
  ed.stats.truncated_published = r.u32();

  const uint32_t story_count = r.u32();
  if (!r.plausible_count(story_count, 37)) return fail("story table is corrupt");
  for (uint32_t i = 0; i < story_count && r.ok(); ++i) {
    StoryRef s;
    s.key = static_cast<uint64_t>(r.i64());
    s.title = r.str();
    s.section = r.str();
    s.source = r.str();
    s.first_page = r.u32();
    s.page_count = r.u32();
    s.truncated = r.u8() != 0;
    s.published = static_cast<Epoch>(r.i64());
    ed.stories.push_back(std::move(s));
  }

  const uint32_t page_count = r.u32();
  if (!r.plausible_count(page_count, 17)) return fail("page table is corrupt");
  ed.pages.reserve(page_count);
  for (uint32_t i = 0; i < page_count && r.ok(); ++i) {
    Page p;
    if (!read_page(r, &p)) return fail("saved edition is truncated");
    ed.pages.push_back(std::move(p));
  }

  if (!r.ok()) return fail("saved edition is truncated");
  // A story pointing outside the pages it was saved with would send the
  // reader into nothing.
  for (const StoryRef& s : ed.stories) {
    if (s.first_page + s.page_count > ed.pages.size()) {
      return fail("saved edition has a story pointing past its pages");
    }
  }

  *out = std::move(ed);
  return true;
}

}  // namespace diarium
