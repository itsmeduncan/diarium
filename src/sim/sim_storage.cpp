#include "sim/sim_storage.h"

#include <sys/stat.h>

#include <cstdio>
#include <fstream>

#include "core/io/byte_sink.h"
#include "core/io/file_byte_source.h"

namespace diarium {
namespace sim {
namespace {

// Appends to an ofstream held open for the sink's lifetime — the desktop
// mirror of DeviceStorage's FileByteSink, which writes straight to the SD
// card instead. Neither buffers a whole edition in memory to write it.
class FileByteSink final : public ByteSink {
 public:
  explicit FileByteSink(const std::string& path)
      : file_(path, std::ios::binary | std::ios::trunc) {}

  bool write(const void* data, size_t n) override {
    if (failed_) return false;
    file_.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
    if (!file_) {
      failed_ = true;
      return false;
    }
    pos_ += n;
    return true;
  }
  size_t position() const override { return pos_; }
  bool ok() const override { return !failed_; }
  bool is_open() const { return file_.is_open(); }

 private:
  std::ofstream file_;
  size_t pos_ = 0;
  bool failed_ = false;
};

}  // namespace

bool SimStorage::read(const std::string& path, std::string* out) {
  return read_file(full(path), out);
}

bool SimStorage::write(const std::string& path, const std::string& data) {
  return write_file(full(path), data);
}

bool SimStorage::exists(const std::string& path) {
  struct stat st;
  return ::stat(full(path).c_str(), &st) == 0;
}

bool SimStorage::remove(const std::string& path) {
  return ::remove(full(path).c_str()) == 0;
}

std::unique_ptr<ByteSink> SimStorage::open_write(const std::string& path) {
  auto sink = std::unique_ptr<FileByteSink>(new FileByteSink(full(path)));
  if (!sink->is_open()) return nullptr;
  return sink;
}

size_t SimStorage::size(const std::string& path) {
  struct stat st;
  if (::stat(full(path).c_str(), &st) != 0) return 0;
  return static_cast<size_t>(st.st_size);
}

bool SimStorage::read_range(const std::string& path, size_t offset,
                            size_t length, std::string* out) {
  std::string data;
  if (!read_file(full(path), &data)) return false;
  if (offset > data.size() || length > data.size() - offset) return false;
  *out = data.substr(offset, length);
  return true;
}

}  // namespace sim
}  // namespace diarium
