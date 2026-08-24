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
#include <memory>
#include <string>
#include <vector>

#include "core/edition/edition.h"
#include "core/io/byte_sink.h"

namespace diarium {

// What the lazy reader needs from wherever the v5 bytes live: how big it is,
// and a slice of it on demand. Deliberately not IStorage — src/core/ never
// sees the HAL — so a device main loop wraps its storage in one of these
// (a ranged read is already what Task 1 added to IStorage) and the sim does
// the same over SimStorage; this file only ever sees the seam.
struct RangedSource {
  virtual ~RangedSource() = default;
  virtual size_t size() const = 0;
  // Fills `out` with [offset, offset+length). False if the range runs past
  // the source, matching IStorage::read_range's contract.
  virtual bool read(size_t offset, size_t length, std::string* out) const = 0;
};

// A RangedSource that owns a copy of the whole blob — what backs
// StreamingEditionReader::open(const std::string&, ...), so that overload
// keeps its existing contract (the reader is self-sufficient once open()
// returns; the caller's string can go away). It reads lazily against its own
// copy exactly like any other RangedSource; the copy is what is not lazy,
// and is the price of that overload's convenience. A caller that wants the
// real memory win opens against a RangedSource of its own instead.
class StringRangedSource final : public RangedSource {
 public:
  explicit StringRangedSource(std::string data) : data_(std::move(data)) {}

  size_t size() const override { return data_.size(); }
  bool read(size_t offset, size_t length, std::string* out) const override {
    if (offset > data_.size() || length > data_.size() - offset) return false;
    *out = data_.substr(offset, length);
    return true;
  }

 private:
  std::string data_;
};

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

// Parses a v5 file's header, footer and index up front — small, and cheap to
// hold resident — and loads a story's pages only when asked, via one ranged
// read of exactly that story's byte range. Nothing else is ever resident:
// this is what lets a device with 8 MB of PSRAM hold an edition of hundreds
// of stories, one story's pages at a time, rather than the whole paper.
class StreamingEditionReader {
 public:
  // Parses header + footer + index from `src`, holding on to `src` itself
  // (not a copy) for later load_story_pages calls — `src` must outlive this
  // reader. Returns false and sets `error` on truncation, corruption, or a
  // version this build doesn't know how to read — never throws, matching
  // deserialize_edition's "degrade, don't crash" contract for a stale or
  // damaged card. Reads only the footer, a header prefix and the index: the
  // proof (test/edition_stream_test.cpp) counts bytes read and shows this
  // never touches a story's pages.
  bool open(const RangedSource& src, std::string* error);

  // The same parse, over a whole string it copies into a StringRangedSource
  // it owns — the reader is then self-sufficient and the caller's string
  // can go away, exactly as before Stage C. Not lazy about the copy itself;
  // see StringRangedSource. What Stage A's callers (desktop tools, tests)
  // still use.
  bool open(const std::string& file, std::string* error);

  Epoch date() const { return date_; }
  const std::string& title() const { return title_; }
  const ComposeStats& stats() const { return stats_; }
  const std::vector<StreamIndexEntry>& index() const { return index_; }

  // Deserialises story i's pages from its byte range via one ranged read.
  // Empty on any error (out-of-range index, or a corrupt story's worth of
  // bytes).
  std::vector<Page> load_story_pages(size_t i) const;

 private:
  // Non-owning; either points at owned_source_ (the whole-string overload)
  // or at a RangedSource the caller keeps alive (the lazy overload).
  const RangedSource* source_ = nullptr;
  std::unique_ptr<RangedSource> owned_source_;
  Epoch date_ = kNoDate;
  std::string title_;
  ComposeStats stats_;
  std::vector<StreamIndexEntry> index_;
};

// Bridges an already-composed, resident Edition to the streaming writer —
// what Stage B's compose calls per-story instead, once selection and
// pagination themselves work a story at a time. Returns false if the sink
// failed. Copies each story's page slice before writing it, so this does not
// itself avoid holding the whole edition resident; it exists to prove the
// v5 format round-trips a real composed edition and that a story's pages
// load independently of the rest.
bool write_edition_streaming(ByteSink& sink, const Edition& edition);

}  // namespace diarium
