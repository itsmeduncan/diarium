#include "sim/sim_hal.h"

#include <sys/stat.h>

#include <cstdio>

#include "core/base/str.h"
#include "core/io/file_byte_source.h"

namespace rsspaper {
namespace sim {
namespace {

// Panel timings from the 6FLICK's datasheet. Used only to report what a
// refresh policy would cost; nothing waits on them.
constexpr int kPartialMs = 225;
constexpr int kFullMs = 1260;
constexpr int kDeepCleanMs = 2400;

}  // namespace

void SimDisplay::flush(RefreshMode mode) {
  flushes_.push_back(mode);
  char name[64];
  std::snprintf(name, sizeof(name), "/frame-%03zu.png", flushes_.size());
  last_path_ = out_dir_ + name;
  write_png(fb_, depth_, last_path_);
}

int SimDisplay::refresh_cost_ms() const {
  int total = 0;
  for (const RefreshMode m : flushes_) {
    total += m == RefreshMode::Partial   ? kPartialMs
             : m == RefreshMode::Full    ? kFullMs
                                         : kDeepCleanMs;
  }
  return total;
}

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

std::unique_ptr<ByteSource> SimHttpClient::get(const HttpRequest& request,
                                               HttpResponse* out) {
  HttpResponse response;

  std::string body;
  if (!read_file(request.url, &body)) {
    response.status = 0;
    response.error = "no fixture at " + request.url;
    if (out != nullptr) *out = response;
    return nullptr;
  }

  // A fixture never changes, so its hash is a perfectly good ETag — which
  // makes the 304 path exercisable without a server.
  response.etag = "\"" + to_hex64(fnv1a(body)) + "\"";
  if (honour_conditional_ && !request.etag.empty() &&
      request.etag == response.etag) {
    response.status = 304;
    if (out != nullptr) *out = response;
    return nullptr;
  }

  response.status = 200;
  if (body.size() > request.max_bytes) body.resize(request.max_bytes);
  if (out != nullptr) *out = response;
  return std::unique_ptr<ByteSource>(new MemoryByteSource(std::move(body)));
}

}  // namespace sim
}  // namespace rsspaper
