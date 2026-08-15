// The one input abstraction the parsing stack sees. Everything upstream — a
// socket, a file on SD, a fixture in a unit test — is just a ByteSource, which
// is what keeps the parsers free of platform headers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rsspaper {

class ByteSource {
 public:
  virtual ~ByteSource() = default;

  // Fills up to `n` bytes; returns 0 at end of stream. Short reads are normal
  // and never mean EOF on their own.
  virtual size_t read(char* dst, size_t n) = 0;

  // True once the source has failed (network drop, read error). Parsers stop
  // and report partial results rather than looping.
  virtual bool failed() const { return false; }
};

class MemoryByteSource final : public ByteSource {
 public:
  explicit MemoryByteSource(std::string data) : data_(std::move(data)) {}

  size_t read(char* dst, size_t n) override {
    const size_t left = data_.size() - pos_;
    const size_t take = n < left ? n : left;
    for (size_t i = 0; i < take; ++i) dst[i] = data_[pos_ + i];
    pos_ += take;
    return take;
  }

 private:
  std::string data_;
  size_t pos_ = 0;
};

// Caps how many bytes a source will yield. Wraps any feed so a runaway or
// hostile response can't exhaust the device's heap.
class LimitedByteSource final : public ByteSource {
 public:
  LimitedByteSource(ByteSource& inner, size_t limit)
      : inner_(inner), left_(limit) {}

  size_t read(char* dst, size_t n) override {
    if (left_ == 0) {
      truncated_ = true;
      return 0;
    }
    const size_t take = n < left_ ? n : left_;
    const size_t got = inner_.read(dst, take);
    left_ -= got;
    return got;
  }

  bool failed() const override { return inner_.failed(); }
  bool truncated() const { return truncated_; }

 private:
  ByteSource& inner_;
  size_t left_;
  bool truncated_ = false;
};

}  // namespace rsspaper
