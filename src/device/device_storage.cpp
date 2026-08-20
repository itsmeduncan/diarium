#include "device/device_storage.h"

namespace rsspaper {
namespace device {

bool DeviceStorage::mount() {
  if (panel_->sdCardInit()) {
    state_ = CardState::Ok;
    return true;
  }
  // The card answering at block level but refusing to mount means the
  // filesystem is wrong, which is a different problem from an empty slot and
  // deserves a different page.
  SdFat& sd = panel_->getSdFat();
  const bool answers = sd.card() != nullptr && sd.card()->sectorCount() > 0;
  state_ = answers ? CardState::Unreadable : CardState::NoCard;
  return false;
}

bool DeviceStorage::format() {
  if (state_ != CardState::Unreadable) return false;
  if (!panel_->getSdFat().format(&Serial)) return false;
  return mount();
}

bool DeviceStorage::read(const std::string& path, std::string* out) {
  if (state_ != CardState::Ok) return false;
  FsFile f = panel_->getSdFat().open(path.c_str(), O_READ);
  if (!f) return false;
  const size_t n = f.size();
  out->resize(n);
  const size_t got = n == 0 ? 0 : f.read(&(*out)[0], n);
  f.close();
  return got == n;
}

bool DeviceStorage::write(const std::string& path, const std::string& data) {
  if (state_ != CardState::Ok) return false;
  FsFile f = panel_->getSdFat().open(path.c_str(), O_WRITE | O_CREAT | O_TRUNC);
  if (!f) return false;
  const size_t put = data.empty() ? 0 : f.write(data.data(), data.size());
  f.close();
  return put == data.size();
}

bool DeviceStorage::exists(const std::string& path) {
  return state_ == CardState::Ok && panel_->getSdFat().exists(path.c_str());
}

bool DeviceStorage::remove(const std::string& path) {
  return state_ == CardState::Ok && panel_->getSdFat().remove(path.c_str());
}

}  // namespace device
}  // namespace rsspaper
