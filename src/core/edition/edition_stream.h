// The v5 streaming edition format: a writer that appends one story's pages
// at a time to a ByteSink, tracking each story's byte range so the index —
// small, cheap to hold resident — can point at it. Nothing here ever holds
// a whole edition's page data in memory at once; that's what lets an edition
// grow past the size the whole-blob v4 format (edition_store.h) can compose
// or read on device.
//
//   header:  u32 magic('RSPE'), u16 version(5), u16 flags(0),
//            i64 date, str title, 6×u32 stats
//   stories: page-blocks concatenated (each = write_page's encoding)
//   index:   u32 story_count, then per story:
//              u64 key, str title, str section, str source, i64 published,
//              u8 truncated, u32 page_count, u32 byte_offset, u32 byte_length
//   footer:  u32 index_offset, u32 magic('RSPE')
//
// byte_offset/byte_length are relative to the start of the file, so a story's
// pages can be loaded on their own from a seekable source without decoding
// anything else.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/edition/edition.h"
#include "core/io/byte_sink.h"

namespace diarium {

constexpr uint16_t kStreamEditionVersion = 5;

// One story's place in a v5 file: the same metadata edition_store carries,
// plus where its pages live.
struct StreamIndexEntry {
  StoryRef ref;
  uint32_t byte_offset = 0;
  uint32_t byte_length = 0;
};

class StreamingEditionWriter {
 public:
  // Writes the header immediately.
  StreamingEditionWriter(ByteSink& sink, Epoch date, const std::string& title,
                          const ComposeStats& stats);

  // Appends one story's pages and records its index entry. `meta.page_count`
  // must equal pages.size(); byte_offset/byte_length are filled in by the
  // writer from the sink's position before and after writing the pages.
  void add_story(const StoryRef& meta, const std::vector<Page>& pages);

  // Writes the index and footer. After this the sink holds a complete v5
  // file. Returns false if the sink failed at any point.
  bool finish();

 private:
  ByteSink& sink_;
  std::vector<StreamIndexEntry> index_;
};

}  // namespace diarium
