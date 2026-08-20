#include "core/net/feed_cache.h"

#include <cstdint>

namespace rsspaper {
namespace {

constexpr char kMagic[4] = {'R', 'S', 'C', '1'};
constexpr size_t kMaxField = 512;

void put_u16(std::string& out, uint16_t v) {
  out.push_back(static_cast<char>(v & 0xFF));
  out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void put_str(std::string& out, const std::string& s) {
  const uint16_t n =
      static_cast<uint16_t>(s.size() > kMaxField ? kMaxField : s.size());
  put_u16(out, n);
  out.append(s, 0, n);
}

bool get_u16(const std::string& in, size_t* at, uint16_t* out) {
  if (*at + 2 > in.size()) return false;
  *out = static_cast<uint16_t>(static_cast<uint8_t>(in[*at]) |
                               (static_cast<uint8_t>(in[*at + 1]) << 8));
  *at += 2;
  return true;
}

bool get_str(const std::string& in, size_t* at, std::string* out) {
  uint16_t n = 0;
  if (!get_u16(in, at, &n)) return false;
  if (n > kMaxField || *at + n > in.size()) return false;
  out->assign(in, *at, n);
  *at += n;
  return true;
}

}  // namespace

bool FeedCache::deserialize(const std::string& blob) {
  entries_.clear();
  if (blob.size() < 6 || blob.compare(0, 4, kMagic, 4) != 0) return false;

  size_t at = 4;
  uint16_t count = 0;
  if (!get_u16(blob, &at, &count)) return false;
  if (count > kMaxEntries) count = kMaxEntries;

  for (uint16_t i = 0; i < count; ++i) {
    Entry e;
    // A half-read cache is worse than none: it would send validators for a
    // feed they do not belong to.
    if (!get_str(blob, &at, &e.url) ||
        !get_str(blob, &at, &e.validators.etag) ||
        !get_str(blob, &at, &e.validators.last_modified)) {
      entries_.clear();
      return false;
    }
    entries_.push_back(std::move(e));
  }
  return true;
}

std::string FeedCache::serialize() const {
  std::string blob(kMagic, 4);
  put_u16(blob, static_cast<uint16_t>(entries_.size()));
  for (const Entry& e : entries_) {
    put_str(blob, e.url);
    put_str(blob, e.validators.etag);
    put_str(blob, e.validators.last_modified);
  }
  return blob;
}

FeedValidators FeedCache::get(const std::string& url) const {
  for (const Entry& e : entries_) {
    if (e.url == url) return e.validators;
  }
  return FeedValidators{};
}

void FeedCache::put(const std::string& url, const FeedValidators& v) {
  for (Entry& e : entries_) {
    if (e.url == url) {
      e.validators = v;
      return;
    }
  }
  if (entries_.size() >= kMaxEntries) return;
  entries_.push_back(Entry{url, v});
}

}  // namespace rsspaper
