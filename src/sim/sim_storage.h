// IStorage over the local filesystem: the simulator's storage backing.
//
// Kept in its own translation unit rather than folded into sim_hal.cpp,
// which pulls in the PNG writer for SimDisplay: a host test that wants the
// real SimStorage (test/storage_stream_test.cpp) can then link this one
// class without pulling in stb_image_write for a display it never touches,
// or sim's own main().
#pragma once

#include <memory>
#include <string>

#include "hal/hal.h"

namespace diarium {
namespace sim {

class SimStorage final : public IStorage {
 public:
  explicit SimStorage(std::string root) : root_(std::move(root)) {}
  bool read(const std::string& path, std::string* out) override;
  bool write(const std::string& path, const std::string& data) override;
  bool exists(const std::string& path) override;
  bool remove(const std::string& path) override;

  std::unique_ptr<ByteSink> open_write(const std::string& path) override;
  size_t size(const std::string& path) override;
  bool read_range(const std::string& path, size_t offset, size_t length,
                  std::string* out) override;

 private:
  std::string full(const std::string& path) const { return root_ + "/" + path; }
  std::string root_;
};

}  // namespace sim
}  // namespace diarium
