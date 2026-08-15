// Clippings: the corner of a page you fold over.
//
// Deliberately not a read-later service. No tags, no folders, no sync, no
// export, no unread state. A saved story keeps its headline, where it came
// from, and when you saved it, and it survives the edition it was cut from —
// which is the whole of the feature.
//
// Like the edition store, this works in strings rather than through
// `IStorage`: `src/core/` does not include the HAL, and a pure serialiser is
// easier to test than one that needs a filesystem.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/base/datetime.h"

namespace rsspaper {

struct Clipping {
  // The story's dedup key, so a clipping can be matched back to the story in
  // whatever edition happens to contain it.
  uint64_t key = 0;
  std::string title;
  std::string section;
  std::string source;
  Epoch published = kNoDate;
  Epoch saved = kNoDate;
};

class Clippings {
 public:
  // A device with no eviction policy and a folder that only grows is a slow
  // leak. The oldest clipping falls off the end.
  explicit Clippings(size_t max_entries = 200) : max_entries_(max_entries) {}

  // Newest first. Returns false if the story was already clipped.
  bool add(const Clipping& clipping);
  bool remove(uint64_t key);
  bool has(uint64_t key) const;
  // Adds if absent, removes if present. Returns true if it is now saved.
  bool toggle(const Clipping& clipping);

  const std::vector<Clipping>& all() const { return entries_; }
  size_t size() const { return entries_.size(); }
  bool empty() const { return entries_.empty(); }
  void clear() { entries_.clear(); }

 private:
  size_t max_entries_;
  std::vector<Clipping> entries_;
};

std::string serialize_clippings(const Clippings& clippings);
bool deserialize_clippings(const std::string& blob, Clippings* out,
                           std::string* error);

}  // namespace rsspaper
