// IStorage over the microSD card. The card is the storage substrate: flash
// holds firmware only, so the scarce 4 MB stays free and OTA stays possible.
#pragma once

#include <memory>
#include <string>

#include "Inkplate.h"
#include "hal/hal.h"

namespace diarium {
namespace device {

// A card that will not mount is a normal state, not an exception: the first
// card used on this project was HFS+ and unreadable while answering perfectly
// at block level.
enum class CardState { Ok, NoCard, Unreadable };

class DeviceStorage final : public IStorage {
 public:
  explicit DeviceStorage(Inkplate* panel) : panel_(panel) {}

  bool mount();
  bool format();
  CardState state() const { return state_; }

  bool read(const std::string& path, std::string* out) override;
  bool write(const std::string& path, const std::string& data) override;
  bool exists(const std::string& path) override;
  bool remove(const std::string& path) override;

  std::unique_ptr<ByteSink> open_write(const std::string& path) override;
  size_t size(const std::string& path) override;
  bool read_range(const std::string& path, size_t offset, size_t length,
                  std::string* out) override;

 private:
  Inkplate* panel_;
  CardState state_ = CardState::NoCard;
};

}  // namespace device
}  // namespace diarium
