// Parsing an HTTP response head. Real servers are inconsistent about case,
// line endings and which headers they bother to send, so this is tested
// against the shapes they actually produce rather than the ones they should.
#include "core/net/http_response.h"

#include <string>

#include "doctest.h"

using namespace rsspaper;

namespace {

HttpHeadParser parse_all(const std::string& raw) {
  HttpHeadParser p;
  p.feed(raw.data(), raw.size());
  return p;
}

}  // namespace

TEST_CASE("parses a status line and the headers that matter") {
  HttpHeadParser p = parse_all(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/xml\r\n"
      "ETag: \"abc123\"\r\n"
      "Last-Modified: Wed, 21 Oct 2026 07:28:00 GMT\r\n"
      "Content-Length: 4096\r\n"
      "\r\n");
  REQUIRE(p.done());
  CHECK(p.head().status == 200);
  CHECK(p.head().etag == "\"abc123\"");
  CHECK(p.head().last_modified == "Wed, 21 Oct 2026 07:28:00 GMT");
  CHECK(p.head().content_length == 4096);
  CHECK_FALSE(p.head().chunked);
}

TEST_CASE("header names are case-insensitive") {
  HttpHeadParser p = parse_all("HTTP/1.1 200 OK\r\netag: \"x\"\r\n\r\n");
  REQUIRE(p.done());
  CHECK(p.head().etag == "\"x\"");
}

TEST_CASE("a head split mid-header still parses") {
  const std::string raw = "HTTP/1.1 304 Not Modified\r\nETag: \"zz\"\r\n\r\n";
  HttpHeadParser p;
  for (size_t i = 0; i < raw.size(); ++i) p.feed(raw.data() + i, 1);
  REQUIRE(p.done());
  CHECK(p.head().status == 304);
  CHECK(p.head().etag == "\"zz\"");
}

TEST_CASE("chunked transfer encoding is recognised") {
  HttpHeadParser p =
      parse_all("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  REQUIRE(p.done());
  CHECK(p.head().chunked);
  CHECK(p.head().content_length == -1);
}

TEST_CASE("transfer encoding is matched whatever its case and company") {
  HttpHeadParser p =
      parse_all("HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, Chunked\r\n\r\n");
  REQUIRE(p.done());
  CHECK(p.head().chunked);
}

TEST_CASE("a redirect carries its location") {
  HttpHeadParser p = parse_all(
      "HTTP/1.1 301 Moved Permanently\r\n"
      "Location: https://example.com/feed.xml\r\n\r\n");
  REQUIRE(p.done());
  CHECK(p.head().status == 301);
  CHECK(p.head().location == "https://example.com/feed.xml");
}

TEST_CASE("content encoding is reported so the body can be inflated") {
  HttpHeadParser p =
      parse_all("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n");
  REQUIRE(p.done());
  CHECK(p.head().content_encoding == "gzip");
}

TEST_CASE("body offset points past the blank line") {
  const std::string raw = "HTTP/1.1 200 OK\r\nETag: \"a\"\r\n\r\nBODY";
  HttpHeadParser p = parse_all(raw);
  REQUIRE(p.done());
  CHECK(raw.substr(p.body_offset()) == "BODY");
}

TEST_CASE("bare LF line endings are tolerated") {
  // Not RFC-legal, and real servers emit them anyway.
  HttpHeadParser p = parse_all("HTTP/1.1 200 OK\nETag: \"a\"\n\n");
  REQUIRE(p.done());
  CHECK(p.head().status == 200);
  CHECK(p.head().etag == "\"a\"");
}

TEST_CASE("a nonsense status line fails rather than guessing") {
  HttpHeadParser p = parse_all("GARBAGE\r\n\r\n");
  CHECK(p.head().status == 0);
}

TEST_CASE("an oversized head is refused rather than buffered forever") {
  HttpHeadParser p;
  const std::string junk(1024, 'x');
  bool ok = true;
  for (int i = 0; i < 64 && ok; ++i) ok = p.feed(junk.data(), junk.size());
  CHECK_FALSE(ok);
  CHECK_FALSE(p.done());
}

TEST_CASE("an absurd header value is clipped rather than stored whole") {
  const std::string huge(4096, 'e');
  HttpHeadParser p = parse_all("HTTP/1.1 200 OK\r\nETag: " + huge + "\r\n\r\n");
  REQUIRE(p.done());
  CHECK(p.head().etag.size() <= 1024);
}

TEST_CASE("the server's date is carried, because the device has no other clock") {
  HttpHeadParser p = parse_all(
      "HTTP/1.1 200 OK\r\nDate: Wed, 19 Aug 2026 09:14:02 GMT\r\n\r\n");
  REQUIRE(p.done());
  CHECK(p.head().date == "Wed, 19 Aug 2026 09:14:02 GMT");
}

TEST_CASE("a 304 still carries the date") {
  // Most mornings most feeds answer 304, so if only 200s set the clock it
  // would drift on exactly the days nothing changed.
  HttpHeadParser p = parse_all(
      "HTTP/1.1 304 Not Modified\r\ndate: Wed, 19 Aug 2026 09:14:02 GMT\r\n\r\n");
  REQUIRE(p.done());
  CHECK(p.head().status == 304);
  CHECK(p.head().date == "Wed, 19 Aug 2026 09:14:02 GMT");
}
