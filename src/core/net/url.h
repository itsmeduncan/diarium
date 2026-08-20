// Splitting URLs and resolving redirects.
//
// Portable because Location headers are relative more often than they should
// be, and getting that wrong on device produces a feed that silently fetches
// the wrong thing — which is much easier to catch in a test than on a panel.
#pragma once

#include <cstdint>
#include <string>

namespace rsspaper {

struct Url {
  bool secure = false;
  std::string host;
  uint16_t port = 0;
  std::string path = "/";  // includes any query string
};

// Only http and https. Anything else is refused rather than guessed at.
bool parse_url(const std::string& text, Url* out);

// Resolves `location` against `base`, handling absolute, protocol-relative,
// root-relative and path-relative forms. Returns empty when it cannot, which
// costs one feed.
std::string resolve_url(const std::string& base, const std::string& location);

}  // namespace rsspaper
