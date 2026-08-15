#include "core/base/str.h"

namespace rsspaper {

uint32_t utf8_next(const std::string& s, size_t& i) {
  if (i >= s.size()) return 0;
  const auto b0 = static_cast<uint8_t>(s[i]);
  auto cont = [&](size_t k) -> bool {
    return i + k < s.size() && (static_cast<uint8_t>(s[i + k]) & 0xC0) == 0x80;
  };
  auto lo = [&](size_t k) -> uint32_t {
    return static_cast<uint8_t>(s[i + k]) & 0x3Fu;
  };

  if (b0 < 0x80) {
    i += 1;
    return b0;
  }
  if ((b0 & 0xE0) == 0xC0 && cont(1)) {
    const uint32_t cp = ((b0 & 0x1Fu) << 6) | lo(1);
    i += 2;
    return cp < 0x80 ? 0xFFFDu : cp;  // reject overlong
  }
  if ((b0 & 0xF0) == 0xE0 && cont(1) && cont(2)) {
    const uint32_t cp = ((b0 & 0x0Fu) << 12) | (lo(1) << 6) | lo(2);
    i += 3;
    return cp < 0x800 ? 0xFFFDu : cp;
  }
  if ((b0 & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3)) {
    const uint32_t cp =
        ((b0 & 0x07u) << 18) | (lo(1) << 12) | (lo(2) << 6) | lo(3);
    i += 4;
    return (cp < 0x10000 || cp > 0x10FFFF) ? 0xFFFDu : cp;
  }
  i += 1;
  return 0xFFFDu;
}

void utf8_append(std::string& out, uint32_t cp) {
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

size_t utf8_length(const std::string& s) {
  size_t i = 0, n = 0;
  while (i < s.size()) {
    utf8_next(s, i);
    ++n;
  }
  return n;
}

char ascii_lower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

void ascii_lower_inplace(std::string& s) {
  for (char& c : s) c = ascii_lower(c);
}

bool iequals(const std::string& a, const char* b) {
  size_t i = 0;
  for (; i < a.size(); ++i) {
    if (b[i] == '\0') return false;
    if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
  }
  return b[i] == '\0';
}

bool starts_with(const std::string& s, const char* prefix) {
  size_t i = 0;
  for (; prefix[i] != '\0'; ++i) {
    if (i >= s.size() || s[i] != prefix[i]) return false;
  }
  return true;
}

bool ends_with(const std::string& s, const char* suffix) {
  const std::string suf(suffix);
  return s.size() >= suf.size() &&
         s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

bool icontains(const std::string& haystack, const char* needle) {
  const size_t n = std::string(needle).size();
  if (n == 0) return true;
  if (haystack.size() < n) return false;
  for (size_t i = 0; i + n <= haystack.size(); ++i) {
    size_t k = 0;
    while (k < n && ascii_lower(haystack[i + k]) == ascii_lower(needle[k])) ++k;
    if (k == n) return true;
  }
  return false;
}

bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

void trim_inplace(std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && is_space(s[b])) ++b;
  while (e > b && is_space(s[e - 1])) --e;
  if (b != 0 || e != s.size()) s = s.substr(b, e - b);
}

std::string trim(const std::string& s) {
  std::string out = s;
  trim_inplace(out);
  return out;
}

std::string collapse_ws(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool pending = false;
  for (char c : s) {
    if (is_space(c)) {
      pending = !out.empty();
    } else {
      if (pending) out.push_back(' ');
      pending = false;
      out.push_back(c);
    }
  }
  return out;
}

uint64_t fnv1a(const std::string& s) {
  uint64_t h = 1469598103934665603ull;
  for (char c : s) {
    h ^= static_cast<uint8_t>(c);
    h *= 1099511628211ull;
  }
  return h;
}

std::string to_hex64(uint64_t v) {
  static const char* kDigits = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<size_t>(i)] = kDigits[v & 0xF];
    v >>= 4;
  }
  return out;
}

}  // namespace rsspaper
