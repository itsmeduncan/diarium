// Parsing an HTTP response head, incrementally.
//
// Incremental because the head arrives in whatever chunks TLS hands over, and
// a header can be split anywhere — including between a name and its colon.
// Portable, so the shapes real servers produce can be tested on a laptop
// rather than discovered on a device with no debugger.
#pragma once

#include <cstddef>
#include <string>

namespace rsspaper {

struct HttpHead {
  int status = 0;                 // 0 means the head was never understood
  std::string etag;
  std::string last_modified;
  std::string location;
  std::string content_encoding;
  long long content_length = -1;  // -1 means unstated
  bool chunked = false;
};

// Caps live in a struct, like XmlLimits and HtmlLimits, so nothing here
// scales with what a server chooses to send.
struct HttpHeadLimits {
  size_t max_head_bytes = 16u * 1024;
  size_t max_header_value = 1024;
};

class HttpHeadParser {
 public:
  explicit HttpHeadParser(HttpHeadLimits limits = HttpHeadLimits())
      : limits_(limits) {}

  // Returns false once the head has outgrown its cap, at which point the
  // caller drops the response. `done()` goes true at the blank line.
  bool feed(const char* data, size_t n);
  bool done() const { return done_; }
  const HttpHead& head() const { return head_; }

  // Offset into the concatenation of everything fed, where the body starts.
  size_t body_offset() const { return body_offset_; }

 private:
  void take_line(const std::string& line);

  HttpHeadLimits limits_;
  HttpHead head_;
  std::string pending_;
  size_t consumed_ = 0;
  size_t body_offset_ = 0;
  bool done_ = false;
  bool saw_status_ = false;
  bool overflowed_ = false;
};

}  // namespace rsspaper
