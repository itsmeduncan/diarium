// ETag and Last-Modified per feed, so a wake that finds nothing new costs a
// handshake instead of a download. Most mornings, most feeds are unchanged,
// and that is the difference between a cheap wake and an expensive one.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace diarium {

struct FeedValidators {
  std::string etag;
  std::string last_modified;
};

// Takes data, not a HAL: nothing in `src/core/` touches the HAL except the
// reader, so the caller does the reading and writing and hands the bytes
// over.
class FeedCache {
 public:
  std::string serialize() const;

  // Absent, corrupt or truncated is not an error, it costs a round of
  // downloads. Returns false on a blob it could not use, having cleared
  // itself — a half-read cache would send validators for the wrong feed.
  bool deserialize(const std::string& blob);

  FeedValidators get(const std::string& url) const;
  void put(const std::string& url, const FeedValidators& v);
  size_t size() const { return entries_.size(); }

  // A reader with more feeds than this has other problems. The cap is here
  // because nothing may scale with input.
  static constexpr size_t kMaxEntries = 128;

 private:
  struct Entry {
    std::string url;
    FeedValidators validators;
  };

  std::vector<Entry> entries_;
};

}  // namespace diarium
