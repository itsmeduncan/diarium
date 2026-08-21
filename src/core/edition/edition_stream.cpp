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

}  // namespace diarium
