// Body framing. Chunked encoding is where real servers break naive parsers,
// so it is exercised here rather than discovered on a device.
#include "core/net/http_body.h"

#include <string>

#include "core/io/byte_source.h"
#include "core/net/http_response.h"
#include "doctest.h"

using namespace rsspaper;

namespace {

std::string drain(ByteSource* src) {
  std::string out;
  char buf[7];  // deliberately not a round number
  for (;;) {
    const size_t n = src->read(buf, sizeof(buf));
    if (n == 0) break;
    out.append(buf, n);
  }
  return out;
}

}  // namespace

TEST_CASE("content-length framing yields exactly that many bytes") {
  HttpHead head;
  head.content_length = 5;
  MemoryByteSource inner("HELLOTRAILING JUNK");
  HttpBodySource body(inner, head, "");
  CHECK(drain(&body) == "HELLO");
}

TEST_CASE("bytes that arrived with the head are used first") {
  HttpHead head;
  head.content_length = 8;
  MemoryByteSource inner("5678");
  HttpBodySource body(inner, head, "1234");
  CHECK(drain(&body) == "12345678");
}

TEST_CASE("chunked framing reassembles the chunks") {
  HttpHead head;
  head.chunked = true;
  MemoryByteSource inner("4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n");
  HttpBodySource body(inner, head, "");
  CHECK(drain(&body) == "Wikipedia");
}

TEST_CASE("chunked framing ignores chunk extensions") {
  HttpHead head;
  head.chunked = true;
  MemoryByteSource inner("4;foo=bar\r\nWiki\r\n0\r\n\r\n");
  HttpBodySource body(inner, head, "");
  CHECK(drain(&body) == "Wiki");
}

TEST_CASE("chunk sizes are hexadecimal in either case") {
  HttpHead head;
  head.chunked = true;
  std::string payload(26, 'z');
  MemoryByteSource inner("1A\r\n" + payload + "\r\n0\r\n\r\n");
  HttpBodySource body(inner, head, "");
  CHECK(drain(&body) == payload);
}

TEST_CASE("a truncated chunked body stops rather than looping") {
  HttpHead head;
  head.chunked = true;
  MemoryByteSource inner("4\r\nWi");  // the stream dies mid-chunk
  HttpBodySource body(inner, head, "");
  CHECK(drain(&body) == "Wi");
}

TEST_CASE("a chunked body whose header never arrives stops") {
  HttpHead head;
  head.chunked = true;
  MemoryByteSource inner("");
  HttpBodySource body(inner, head, "");
  CHECK(drain(&body).empty());
}

TEST_CASE("an unstated length reads until the stream ends") {
  HttpHead head;  // no content-length, not chunked
  MemoryByteSource inner("everything until close");
  HttpBodySource body(inner, head, "");
  CHECK(drain(&body) == "everything until close");
}

TEST_CASE("a body shorter than its content-length ends rather than hanging") {
  HttpHead head;
  head.content_length = 100;
  MemoryByteSource inner("only this much");
  HttpBodySource body(inner, head, "");
  CHECK(drain(&body) == "only this much");
}
