// The write-side mirror of ByteSource: everything downstream of the page
// codec — the desktop's whole-string blob, later a file on SD — is just a
// ByteSink, which is what lets a single encoder serve both without knowing
// which one it is writing to.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace diarium {

struct ByteSink {
  virtual ~ByteSink() = default;
  // Append n bytes. Returns false once the sink has failed; a failed sink
  // stays failed so callers can check once at the end.
  virtual bool write(const void* data, size_t n) = 0;
  // Bytes successfully written so far — the writer needs this for offsets.
  virtual size_t position() const = 0;
  virtual bool ok() const = 0;
};

// A sink that appends to a std::string: the desktop path and the test
// double. Never fails — the only failure mode (allocation) is not one this
// codebase recovers from anywhere else either.
class StringSink final : public ByteSink {
 public:
  explicit StringSink(std::string* out) : out_(out) {}

  bool write(const void* data, size_t n) override {
    out_->append(static_cast<const char*>(data), n);
    pos_ += n;
    return true;
  }
  size_t position() const override { return pos_; }
  bool ok() const override { return true; }

 private:
  std::string* out_;
  size_t pos_ = 0;
};

}  // namespace diarium
