#include "device/device_storage.h"

#include "core/io/byte_sink.h"

namespace diarium {
namespace device {
namespace {

// Writes straight through to the open FsFile as each chunk arrives — no
// whole-file buffer — which is the entire point of streaming the edition to
// the card instead of composing it resident and serialising it in one shot.
class FileByteSink final : public ByteSink {
 public:
  bool open(SdFat& sd, const std::string& path) {
    file_ = sd.open(path.c_str(), O_WRITE | O_CREAT | O_TRUNC);
    return static_cast<bool>(file_);
  }

  ~FileByteSink() override {
    if (file_) file_.close();
  }

  bool write(const void* data, size_t n) override {
    if (failed_ || !file_) return false;
    const size_t put =
        n == 0 ? 0 : file_.write(static_cast<const uint8_t*>(data), n);
    if (put != n) {
      failed_ = true;
      return false;
    }
    pos_ += n;
    return true;
  }
  size_t position() const override { return pos_; }
  bool ok() const override { return !failed_; }

 private:
  FsFile file_;
  size_t pos_ = 0;
  bool failed_ = false;
};

}  // namespace

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

std::unique_ptr<ByteSink> DeviceStorage::open_write(const std::string& path) {
  if (state_ != CardState::Ok) return nullptr;
  auto sink = std::unique_ptr<FileByteSink>(new FileByteSink());
  if (!sink->open(panel_->getSdFat(), path)) return nullptr;
  return sink;
}

size_t DeviceStorage::size(const std::string& path) {
  if (state_ != CardState::Ok) return 0;
  FsFile f = panel_->getSdFat().open(path.c_str(), O_READ);
  if (!f) return 0;
  const size_t n = f.size();
  f.close();
  return n;
}

bool DeviceStorage::read_range(const std::string& path, size_t offset,
                               size_t length, std::string* out) {
  if (state_ != CardState::Ok) return false;
  FsFile f = panel_->getSdFat().open(path.c_str(), O_READ);
  if (!f) return false;
  const size_t total = f.size();
  if (offset > total || length > total - offset) {
    f.close();
    return false;
  }
  out->resize(length);
  const size_t got =
      length == 0 ? 0
                  : (f.seekSet(offset) ? f.read(&(*out)[0], length) : 0);
  f.close();
  return got == length;
}

}  // namespace device
}  // namespace diarium
