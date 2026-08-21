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

}  // namespace diarium
