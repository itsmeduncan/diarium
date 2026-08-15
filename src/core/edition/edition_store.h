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

namespace rsspaper {

constexpr uint32_t kEditionMagic = 0x45505352;  // "RSPE"
constexpr uint16_t kEditionVersion = 1;

std::string serialize_edition(const Edition& edition);

// Returns false and leaves `edition` untouched if the blob is truncated,
// corrupt, or written by a newer build. A stale cache must degrade into
// "compose a fresh edition", never into a crash on a device with no console.
bool deserialize_edition(const std::string& blob, Edition* edition,
                         std::string* error);

}  // namespace rsspaper
