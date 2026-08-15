#include "core/edition/clippings.h"

#include <utility>

namespace rsspaper {
namespace {

constexpr uint32_t kClippingsMagic = 0x50494C43;  // "CLIP"
constexpr uint16_t kClippingsVersion = 1;

void put_u16(std::string& out, uint16_t v) {
  out.push_back(static_cast<char>(v & 0xFF));
  out.push_back(static_cast<char>(v >> 8));
}
void put_u32(std::string& out, uint32_t v) {
  put_u16(out, static_cast<uint16_t>(v & 0xFFFF));
  put_u16(out, static_cast<uint16_t>(v >> 16));
}
void put_i64(std::string& out, int64_t v) {
  const uint64_t u = static_cast<uint64_t>(v);
  put_u32(out, static_cast<uint32_t>(u & 0xFFFFFFFFu));
  put_u32(out, static_cast<uint32_t>(u >> 32));
}
void put_str(std::string& out, const std::string& s) {
  put_u32(out, static_cast<uint32_t>(s.size()));
  out += s;
}

class Cursor {
 public:
  explicit Cursor(const std::string& d) : d_(d) {}
  bool ok() const { return ok_; }
  size_t remaining() const { return ok_ ? d_.size() - at_ : 0; }

  uint16_t u16() {
    if (!want(2)) return 0;
    const uint16_t v = static_cast<uint16_t>(
        static_cast<uint8_t>(d_[at_]) | (static_cast<uint8_t>(d_[at_ + 1]) << 8));
    at_ += 2;
    return v;
  }
  uint32_t u32() {
    const uint32_t lo = u16();
    const uint32_t hi = u16();
    return lo | (hi << 16);
  }
  int64_t i64() {
    const uint64_t lo = u32();
    const uint64_t hi = u32();
    return static_cast<int64_t>(lo | (hi << 32));
  }
  std::string str() {
    const uint32_t n = u32();
    if (!ok_ || n > remaining()) {
      ok_ = false;
      return std::string();
    }
    std::string s = d_.substr(at_, n);
    at_ += n;
    return s;
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

}  // namespace

bool Clippings::has(uint64_t key) const {
  for (const Clipping& c : entries_) {
    if (c.key == key) return true;
  }
  return false;
}

bool Clippings::add(const Clipping& clipping) {
  if (has(clipping.key)) return false;
  // Newest first: the thing you just saved is the thing you want to see.
  entries_.insert(entries_.begin(), clipping);
  if (max_entries_ > 0 && entries_.size() > max_entries_) {
    entries_.resize(max_entries_);
  }
  return true;
}

bool Clippings::remove(uint64_t key) {
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].key != key) continue;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
    return true;
  }
  return false;
}

bool Clippings::toggle(const Clipping& clipping) {
  if (has(clipping.key)) {
    remove(clipping.key);
    return false;
  }
  add(clipping);
  return true;
}

std::string serialize_clippings(const Clippings& clippings) {
  std::string out;
  put_u32(out, kClippingsMagic);
  put_u16(out, kClippingsVersion);
  put_u16(out, 0);
  put_u32(out, static_cast<uint32_t>(clippings.size()));
  for (const Clipping& c : clippings.all()) {
    put_i64(out, static_cast<int64_t>(c.key));
    put_str(out, c.title);
    put_str(out, c.section);
    put_str(out, c.source);
    put_i64(out, c.published);
    put_i64(out, c.saved);
  }
  return out;
}

bool deserialize_clippings(const std::string& blob, Clippings* out,
                           std::string* error) {
  auto fail = [&](const char* why) {
    if (error != nullptr) *error = why;
    return false;
  };

  Cursor c(blob);
  if (c.u32() != kClippingsMagic) return fail("not a clippings file");
  if (c.u16() != kClippingsVersion) {
    return fail("clippings were written by a different build");
  }
  c.u16();

  const uint32_t count = c.u32();
  // Each entry is at least 40 bytes, so a count larger than the file allows
  // is corruption rather than a very long list.
  if (!c.ok() || count > c.remaining() / 40 + 1) {
    return fail("clippings file is corrupt");
  }

  std::vector<Clipping> entries;
  entries.reserve(count);
  for (uint32_t i = 0; i < count && c.ok(); ++i) {
    Clipping entry;
    entry.key = static_cast<uint64_t>(c.i64());
    entry.title = c.str();
    entry.section = c.str();
    entry.source = c.str();
    entry.published = c.i64();
    entry.saved = c.i64();
    if (!c.ok()) break;
    entries.push_back(std::move(entry));
  }
  if (!c.ok()) return fail("clippings file is truncated");

  // The file is newest-first and add() prepends, so replaying it backwards is
  // what restores the order it was written in.
  Clippings loaded;
  for (size_t i = entries.size(); i-- > 0;) loaded.add(entries[i]);

  *out = loaded;
  return true;
}

}  // namespace rsspaper
