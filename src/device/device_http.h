// IHttpClient over TLS. The only network requests Diarium ever makes.
//
// Conditional GET rides on the fields hal.h already carries: the caller puts
// last time's validators in the request and reads the new ones out of the
// response, so this client never needs to know a cache exists.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "hal/hal.h"

namespace diarium {
namespace device {

class DeviceHttpClient final : public IHttpClient {
 public:
  // Returns a stream over the body, or null on failure or 304. `out` is
  // filled either way, because "not modified" and "the wifi is down" mean
  // very different things to a reader.
  std::unique_ptr<ByteSource> get(const HttpRequest& request,
                                  HttpResponse* out) override;

  // Redirect hops allowed before giving up. A feed that redirects forever
  // costs that feed, not the wake.
  static constexpr int kMaxHops = 3;
};

}  // namespace device
}  // namespace diarium
