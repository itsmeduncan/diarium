// A stub until issue #3. It exists so Hal::complete() holds and the colophon
// can say why there is no network, rather than the reader wondering.
#pragma once

#include <memory>

#include "hal/hal.h"

namespace rsspaper {
namespace device {

class DeviceHttpClient final : public IHttpClient {
 public:
  std::unique_ptr<ByteSource> get(const HttpRequest& request,
                                  HttpResponse* out) override {
    (void)request;
    if (out != nullptr) {
      out->status = 0;
      out->error = "no network yet";
    }
    return nullptr;
  }
};

}  // namespace device
}  // namespace rsspaper
