#include "core/net/url.h"

#include <cstdlib>

namespace diarium {
namespace {

constexpr size_t kMaxUrl = 2048;

}  // namespace

bool parse_url(const std::string& text, Url* out) {
  if (out == nullptr || text.empty() || text.size() > kMaxUrl) return false;

  size_t at = 0;
  if (text.compare(0, 8, "https://") == 0) {
    out->secure = true;
    out->port = 443;
    at = 8;
  } else if (text.compare(0, 7, "http://") == 0) {
    out->secure = false;
    out->port = 80;
    at = 7;
  } else {
    return false;
  }

  const size_t slash = text.find('/', at);
  std::string authority =
      slash == std::string::npos ? text.substr(at) : text.substr(at, slash - at);
  out->path = slash == std::string::npos ? "/" : text.substr(slash);
  if (out->path.empty()) out->path = "/";

  // Strip any userinfo: it is not something a feed reader should be carrying.
  const size_t at_sign = authority.find('@');
  if (at_sign != std::string::npos) authority = authority.substr(at_sign + 1);

  const size_t colon = authority.rfind(':');
  if (colon != std::string::npos) {
    const std::string port = authority.substr(colon + 1);
    bool digits = !port.empty();
    for (char c : port) {
      if (c < '0' || c > '9') digits = false;
    }
    if (digits) {
      const long v = std::atol(port.c_str());
      if (v <= 0 || v > 65535) return false;
      out->port = static_cast<uint16_t>(v);
      authority = authority.substr(0, colon);
    }
  }

  if (authority.empty()) return false;
  out->host = authority;
  return true;
}

std::string resolve_url(const std::string& base, const std::string& location) {
  if (location.empty() || location.size() > kMaxUrl) return std::string();

  // Already absolute.
  if (location.compare(0, 7, "http://") == 0 ||
      location.compare(0, 8, "https://") == 0) {
    return location;
  }

  Url b;
  if (!parse_url(base, &b)) return std::string();
  const std::string scheme = b.secure ? "https://" : "http://";

  std::string authority = b.host;
  if ((b.secure && b.port != 443) || (!b.secure && b.port != 80)) {
    authority += ":" + std::to_string(b.port);
  }

  // Protocol-relative: //host/path
  if (location.compare(0, 2, "//") == 0) return scheme + location.substr(2);

  // Root-relative: /path
  if (location[0] == '/') return scheme + authority + location;

  // Path-relative: resolved against the base's directory.
  const size_t last_slash = b.path.rfind('/');
  const std::string dir =
      last_slash == std::string::npos ? "/" : b.path.substr(0, last_slash + 1);
  return scheme + authority + dir + location;
}

}  // namespace diarium
