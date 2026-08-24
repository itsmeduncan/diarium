// The response body as a ByteSource, so the feed parser neither knows nor
// cares whether the bytes were chunked, length-delimited, or read to close.
#pragma once

#include <cstddef>
#include <string>

#include "core/io/byte_source.h"
#include "core/net/http_response.h"

namespace diarium {

class HttpBodySource final : public ByteSource {
 public:
  // `prefetched` is whatever body arrived in the same read as the head.
  HttpBodySource(ByteSource& inner, const HttpHead& head,
                 std::string prefetched);

  size_t read(char* dst, size_t n) override;
  bool failed() const override { return inner_.failed(); }

 private:
  size_t take_prefetched(char* dst, size_t n);
  size_t read_raw(char* dst, size_t n);
  bool read_chunk_header();

  ByteSource& inner_;
  std::string prefetched_;
  size_t prefetched_pos_ = 0;
  long long remaining_ = -1;  // content-length framing; -1 means unstated
  bool chunked_ = false;
  long long chunk_left_ = 0;
  bool finished_ = false;
};

}  // namespace diarium
