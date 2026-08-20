#include "core/net/http_body.h"

namespace rsspaper {
namespace {

// A chunk header is at most a hex length plus extensions; anything longer is
// a server misbehaving and is cut off rather than buffered.
constexpr size_t kMaxChunkHeader = 32;

}  // namespace

HttpBodySource::HttpBodySource(ByteSource& inner, const HttpHead& head,
                               std::string prefetched)
    : inner_(inner),
      prefetched_(std::move(prefetched)),
      remaining_(head.content_length),
      chunked_(head.chunked) {}

size_t HttpBodySource::take_prefetched(char* dst, size_t n) {
  const size_t left = prefetched_.size() - prefetched_pos_;
  const size_t take = n < left ? n : left;
  for (size_t i = 0; i < take; ++i) dst[i] = prefetched_[prefetched_pos_ + i];
  prefetched_pos_ += take;
  return take;
}

size_t HttpBodySource::read_raw(char* dst, size_t n) {
  if (prefetched_pos_ < prefetched_.size()) return take_prefetched(dst, n);
  return inner_.read(dst, n);
}

// Reads "1a3f;ext\r\n" and leaves chunk_left_ set. False means the stream
// ended or lied, which costs this feed and nothing else.
bool HttpBodySource::read_chunk_header() {
  std::string line;
  char c = 0;
  for (;;) {
    if (read_raw(&c, 1) == 0) return false;
    if (c == '\n') break;
    if (c != '\r' && line.size() < kMaxChunkHeader) line.push_back(c);
  }

  long long size = 0;
  bool any = false;
  for (char d : line) {
    int v;
    if (d >= '0' && d <= '9') {
      v = d - '0';
    } else if (d >= 'a' && d <= 'f') {
      v = d - 'a' + 10;
    } else if (d >= 'A' && d <= 'F') {
      v = d - 'A' + 10;
    } else {
      break;  // a chunk extension, or the end of the number
    }
    size = size * 16 + v;
    any = true;
  }
  if (!any) return false;
  chunk_left_ = size;
  return true;
}

size_t HttpBodySource::read(char* dst, size_t n) {
  if (finished_ || n == 0) return 0;

  if (chunked_) {
    if (chunk_left_ == 0) {
      // A zero-length chunk is the end of the body, and so is a header that
      // never arrives.
      if (!read_chunk_header() || chunk_left_ == 0) {
        finished_ = true;
        return 0;
      }
    }
    const size_t want = n < static_cast<size_t>(chunk_left_)
                            ? n
                            : static_cast<size_t>(chunk_left_);
    const size_t got = read_raw(dst, want);
    if (got == 0) {
      finished_ = true;
      return 0;
    }
    chunk_left_ -= static_cast<long long>(got);
    if (chunk_left_ == 0) {
      char crlf[2];
      read_raw(crlf, 2);  // the CRLF that closes a chunk
    }
    return got;
  }

  if (remaining_ >= 0) {
    if (remaining_ == 0) {
      finished_ = true;
      return 0;
    }
    const size_t want = n < static_cast<size_t>(remaining_)
                            ? n
                            : static_cast<size_t>(remaining_);
    const size_t got = read_raw(dst, want);
    remaining_ -= static_cast<long long>(got);
    if (got == 0) finished_ = true;  // the server promised more than it sent
    return got;
  }

  const size_t got = read_raw(dst, n);
  if (got == 0) finished_ = true;
  return got;
}

}  // namespace rsspaper
