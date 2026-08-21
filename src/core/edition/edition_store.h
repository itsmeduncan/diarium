// Saving a composed edition, so reading it costs nothing.
//
// An edition is immutable once composed. Re-parsing feeds and re-running
// layout to show page nine would be absurd on a device that has to last weeks
// on a battery, so composition happens once at wake and every page turn after
// that is a blit from storage.
//
// This works in strings rather than through `IStorage` on purpose: `src/core/`
// does not include the HAL, and a pure serialiser is far easier to test than
// one that needs a filesystem. The caller does the writing.
//
// The format is deliberately dumb — no compression, no back-references. It is
// read on a device with an 8 MB PSRAM budget and written once a day; the
// cost that matters is decode time, not bytes.
#pragma once

#include <cstdint>
#include <string>

#include "core/edition/edition.h"
#include "core/io/byte_sink.h"
#include "core/layout/page.h"

namespace diarium {

constexpr uint32_t kEditionMagic = 0x45505352;  // "RSPE"
// 2: StoryRef gained a stable key and a source, so a story can be
// recognised again in a later edition.
// 3: adds StoryRef::published.
// 4: the front page, section ledes and colophon are gone — an edition is
// nothing but story text. Drops Edition::browse_page_count/colophon_page/
// section_marks and StoryRef::lede_page/lede_bounds.
constexpr uint16_t kEditionVersion = 4;
// The oldest format still readable. Versions 2 and 3 carry browse pages this
// build no longer knows how to lay out, so a stale cache from before this
// format bump is not read — it degrades into "compose a fresh edition",
// exactly the safe path a format bump must always have.
constexpr uint16_t kMinEditionVersion = 4;

std::string serialize_edition(const Edition& edition);

// The little-endian primitives underneath serialize_edition, exposed so the
// streaming v5 writer (edition_stream.h) writes the same bytes for the same
// fields rather than forking its own copy.
void put_u8(ByteSink& sink, uint8_t v);
void put_u16(ByteSink& sink, uint16_t v);
void put_u32(ByteSink& sink, uint32_t v);
void put_i32(ByteSink& sink, int32_t v);
void put_i64(ByteSink& sink, int64_t v);
void put_str(ByteSink& sink, const std::string& s);

// The per-page encoding, shared by the whole-blob v4 writer above and the
// streaming v5 writer (edition_stream.h) — a page looks the same on disk
// either way.
void write_page(ByteSink& sink, const Page& page);

// Returns false and leaves `edition` untouched if the blob is truncated,
// corrupt, or written by a newer build. A stale cache must degrade into
// "compose a fresh edition", never into a crash on a device with no console.
bool deserialize_edition(const std::string& blob, Edition* edition,
                         std::string* error);

// A cursor over a string that can't read off the end. Every failure is the
// same failure — the blob is not what we expected — so it is tracked once
// (`ok()`) rather than at every call site. Public so the streaming v5 reader
// (edition_stream.h) can decode its header/index/pages with the same cursor
// deserialize_edition uses, rather than a second implementation.
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

// The per-page decode, mirroring write_page. Returns false (and leaves
// `page` possibly partially filled) if the reader ran out of bytes or found
// an implausible count — the caller checks `reader.ok()` once at the end
// rather than after every page.
bool read_page(Reader& reader, Page* page);

}  // namespace diarium
