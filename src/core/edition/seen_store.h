// Remembers which stories have already run, so yesterday's paper doesn't
// reappear in today's.
//
// Keyed on Item::dedup_key (guid, else link, else title+date). Entries expire,
// because a feed that reissues a guid after a month is republishing, and
// because an unbounded set on a device with no eviction policy is a slow leak.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/base/datetime.h"

namespace rsspaper {

class SeenStore {
 public:
  // Entries older than `retain_days` are dropped on load and never written
  // back. Thirty days is long enough that nothing recirculates and short
  // enough that the file stays under a few KB.
  explicit SeenStore(int retain_days = 30) : retain_days_(retain_days) {}

  // Path-based, for the desktop. On device these reach stdio rather than the
  // card, so the firmware uses the blob API below and does its own I/O
  // through IStorage — the same split clippings already uses.
  bool load(const std::string& path, Epoch now);
  bool save(const std::string& path) const;

  // Replaces the contents. Entries older than `retain_days` before `now` are
  // dropped. An empty blob is an empty store, not a failure.
  bool deserialize(const std::string& blob, Epoch now);

  bool has(uint64_t key) const;
  // Records the key. Returns false if it was already there, which is the
  // signal to drop the item from this edition.
  bool mark(uint64_t key, Epoch when);

  size_t size() const { return entries_.size(); }

 private:
  friend std::string serialize_seen_store(const SeenStore& store);

 private:
  struct Entry {
    uint64_t key;
    Epoch when;
  };

  int retain_days_;
  std::vector<Entry> entries_;
};

std::string serialize_seen_store(const SeenStore& store);

}  // namespace rsspaper
