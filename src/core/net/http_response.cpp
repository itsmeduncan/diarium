#include "core/net/http_response.h"

#include <cstdlib>

namespace rsspaper {
namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

// Compares a header name — the bytes before the colon — case-insensitively.
bool name_is(const std::string& line, size_t colon, const char* want) {
  size_t i = 0;
  for (; i < colon && want[i] != '\0'; ++i) {
    if (lower(line[i]) != want[i]) return false;
  }
  return i == colon && want[i] == '\0';
}

// Transfer-Encoding may list several codings; chunked is the one that changes
// how the body is framed, and it can arrive in any case.
bool mentions_chunked(const std::string& value) {
  static const char kWant[] = "chunked";
  const size_t n = sizeof(kWant) - 1;
  if (value.size() < n) return false;
  for (size_t i = 0; i + n <= value.size(); ++i) {
    size_t j = 0;
    while (j < n && lower(value[i + j]) == kWant[j]) ++j;
    if (j == n) return true;
  }
  return false;
}

std::string trimmed(const std::string& s, size_t from) {
  size_t a = from;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
  size_t b = s.size();
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
  return s.substr(a, b - a);
}

}  // namespace

void HttpHeadParser::take_line(const std::string& line) {
  if (!saw_status_) {
    saw_status_ = true;
    // "HTTP/1.1 200 OK". Anything else leaves the status at 0, which the
    // caller reads as "the request never completed".
    if (line.compare(0, 5, "HTTP/") != 0) return;
    const size_t sp = line.find(' ');
    if (sp == std::string::npos) return;
    head_.status = std::atoi(line.c_str() + sp + 1);
    return;
  }

  const size_t colon = line.find(':');
  if (colon == std::string::npos) return;
  std::string value = trimmed(line, colon + 1);
  if (value.size() > limits_.max_header_value) {
    value.resize(limits_.max_header_value);
  }

  if (name_is(line, colon, "etag")) {
    head_.etag = value;
  } else if (name_is(line, colon, "last-modified")) {
    head_.last_modified = value;
  } else if (name_is(line, colon, "location")) {
    head_.location = value;
  } else if (name_is(line, colon, "content-encoding")) {
    head_.content_encoding = value;
  } else if (name_is(line, colon, "content-length")) {
    head_.content_length = std::atoll(value.c_str());
  } else if (name_is(line, colon, "transfer-encoding")) {
    if (mentions_chunked(value)) head_.chunked = true;
  }
}

bool HttpHeadParser::feed(const char* data, size_t n) {
  if (done_ || overflowed_) return !overflowed_;

  for (size_t i = 0; i < n; ++i) {
    const char c = data[i];
    ++consumed_;
    if (consumed_ > limits_.max_head_bytes) {
      overflowed_ = true;
      return false;
    }
    if (c != '\n') {
      pending_.push_back(c);
      continue;
    }
    // Strip a trailing CR; a bare LF is tolerated because servers emit it.
    if (!pending_.empty() && pending_.back() == '\r') pending_.pop_back();
    if (pending_.empty()) {
      done_ = true;
      body_offset_ = consumed_;
      return true;
    }
    take_line(pending_);
    pending_.clear();
  }
  return true;
}

}  // namespace rsspaper
