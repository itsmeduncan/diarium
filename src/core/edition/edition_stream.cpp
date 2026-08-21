#include "core/edition/edition_stream.h"

#include "core/edition/edition_store.h"

namespace diarium {

StreamingEditionWriter::StreamingEditionWriter(ByteSink& sink, Epoch date,
                                                const std::string& title,
                                                const ComposeStats& stats)
    : sink_(sink) {
  put_u32(sink_, kEditionMagic);
  put_u16(sink_, kStreamEditionVersion);
  put_u16(sink_, 0);  // flags

  put_i64(sink_, date);
  put_str(sink_, title);

  put_u32(sink_, static_cast<uint32_t>(stats.items_in));
  put_u32(sink_, static_cast<uint32_t>(stats.dropped_seen));
  put_u32(sink_, static_cast<uint32_t>(stats.dropped_stale));
  put_u32(sink_, static_cast<uint32_t>(stats.dropped_over_budget));
  put_u32(sink_, static_cast<uint32_t>(stats.items_published));
  put_u32(sink_, static_cast<uint32_t>(stats.truncated_published));
}

void StreamingEditionWriter::add_story(const StoryRef& meta,
                                        const std::vector<Page>& pages) {
  StreamIndexEntry entry;
  entry.ref = meta;
  entry.byte_offset = static_cast<uint32_t>(sink_.position());
  for (const Page& p : pages) write_page(sink_, p);
  entry.byte_length =
      static_cast<uint32_t>(sink_.position()) - entry.byte_offset;
  index_.push_back(std::move(entry));
}

bool StreamingEditionWriter::finish() {
  const uint32_t index_offset = static_cast<uint32_t>(sink_.position());
  put_u32(sink_, static_cast<uint32_t>(index_.size()));
  for (const StreamIndexEntry& e : index_) {
    put_i64(sink_, static_cast<int64_t>(e.ref.key));
    put_str(sink_, e.ref.title);
    put_str(sink_, e.ref.section);
    put_str(sink_, e.ref.source);
    put_i64(sink_, static_cast<int64_t>(e.ref.published));
    put_u8(sink_, e.ref.truncated ? 1 : 0);
    put_u32(sink_, static_cast<uint32_t>(e.ref.page_count));
    put_u32(sink_, e.byte_offset);
    put_u32(sink_, e.byte_length);
  }

  put_u32(sink_, index_offset);
  put_u32(sink_, kEditionMagic);

  return sink_.ok();
}

namespace {
// u32 index_offset + u32 magic.
constexpr size_t kFooterSize = 8;
}  // namespace

bool StreamingEditionReader::open(const std::string& file, std::string* error) {
  auto fail = [&](const char* why) {
    if (error != nullptr) *error = why;
    return false;
  };

  if (file.size() < kFooterSize) return fail("stream edition is truncated");

  const std::string footer_bytes = file.substr(file.size() - kFooterSize);
  Reader footer(footer_bytes);
  const uint32_t index_offset = footer.u32();
  const uint32_t footer_magic = footer.u32();
  if (!footer.ok() || footer_magic != kEditionMagic) {
    return fail("not a saved stream edition");
  }
  if (index_offset > file.size() - kFooterSize) {
    return fail("stream edition index is corrupt");
  }

  Reader header(file);
  if (header.u32() != kEditionMagic) return fail("not a saved stream edition");
  const uint16_t version = header.u16();
  if (version != kStreamEditionVersion) {
    return fail("stream edition was written by a different build");
  }
  header.u16();  // flags

  const Epoch date = header.i64();
  const std::string title = header.str();

  ComposeStats stats;
  stats.items_in = header.u32();
  stats.dropped_seen = header.u32();
  stats.dropped_stale = header.u32();
  stats.dropped_over_budget = header.u32();
  stats.items_published = header.u32();
  stats.truncated_published = header.u32();

  if (!header.ok()) return fail("stream edition header is corrupt");

  const std::string index_bytes = file.substr(index_offset);
  Reader index_reader(index_bytes);
  const uint32_t story_count = index_reader.u32();
  if (!index_reader.plausible_count(story_count, 37)) {
    return fail("stream edition index is corrupt");
  }

  std::vector<StreamIndexEntry> index;
  index.reserve(story_count);
  for (uint32_t i = 0; i < story_count && index_reader.ok(); ++i) {
    StreamIndexEntry e;
    e.ref.key = static_cast<uint64_t>(index_reader.i64());
    e.ref.title = index_reader.str();
    e.ref.section = index_reader.str();
    e.ref.source = index_reader.str();
    e.ref.published = static_cast<Epoch>(index_reader.i64());
    e.ref.truncated = index_reader.u8() != 0;
    e.ref.page_count = index_reader.u32();
    e.byte_offset = index_reader.u32();
    e.byte_length = index_reader.u32();
    index.push_back(std::move(e));
  }
  if (!index_reader.ok()) return fail("stream edition index is corrupt");

  // A story's byte range must sit inside the stories region (before the
  // index) — a range pointing past it would send a later load into the
  // index or the footer instead of page data.
  for (const StreamIndexEntry& e : index) {
    const uint64_t end = static_cast<uint64_t>(e.byte_offset) + e.byte_length;
    if (end > index_offset) {
      return fail("stream edition has a story pointing past its pages");
    }
  }

  file_ = file;
  date_ = date;
  title_ = title;
  stats_ = stats;
  index_ = std::move(index);
  return true;
}

std::vector<Page> StreamingEditionReader::load_story_pages(size_t i) const {
  std::vector<Page> pages;
  if (i >= index_.size()) return pages;

  const StreamIndexEntry& e = index_[i];
  if (static_cast<uint64_t>(e.byte_offset) + e.byte_length > file_.size()) {
    return pages;
  }

  const std::string story_bytes = file_.substr(e.byte_offset, e.byte_length);
  Reader r(story_bytes);
  pages.reserve(e.ref.page_count);
  for (uint32_t k = 0; k < e.ref.page_count && r.ok(); ++k) {
    Page p;
    if (!read_page(r, &p)) return {};
    pages.push_back(std::move(p));
  }
  if (!r.ok()) return {};
  return pages;
}

bool write_edition_streaming(ByteSink& sink, const Edition& edition) {
  StreamingEditionWriter w(sink, edition.date, edition.title, edition.stats);
  for (const StoryRef& s : edition.stories) {
    const auto begin = edition.pages.begin() + static_cast<ptrdiff_t>(s.first_page);
    const auto end = begin + static_cast<ptrdiff_t>(s.page_count);
    w.add_story(s, std::vector<Page>(begin, end));
  }
  return w.finish();
}

}  // namespace diarium
