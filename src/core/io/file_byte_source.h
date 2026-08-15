// A ByteSource over stdio. Portable C, so it serves the simulator reading
// fixtures and the device reading a cached edition off SD.
#pragma once

#include <cstdio>
#include <string>

#include "core/io/byte_source.h"

namespace rsspaper {

class FileByteSource final : public ByteSource {
 public:
  explicit FileByteSource(const std::string& path)
      : fp_(std::fopen(path.c_str(), "rb")) {}
  ~FileByteSource() override {
    if (fp_ != nullptr) std::fclose(fp_);
  }

  FileByteSource(const FileByteSource&) = delete;
  FileByteSource& operator=(const FileByteSource&) = delete;

  bool ok() const { return fp_ != nullptr; }

  size_t read(char* dst, size_t n) override {
    if (fp_ == nullptr) return 0;
    const size_t got = std::fread(dst, 1, n, fp_);
    if (got == 0 && std::ferror(fp_) != 0) failed_ = true;
    return got;
  }

  bool failed() const override { return failed_ || fp_ == nullptr; }

 private:
  std::FILE* fp_ = nullptr;
  bool failed_ = false;
};

// Reads a whole file. For config and font packs, never for feeds.
bool read_file(const std::string& path, std::string* out);
bool write_file(const std::string& path, const std::string& data);

}  // namespace rsspaper
