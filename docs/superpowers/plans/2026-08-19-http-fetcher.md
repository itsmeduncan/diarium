# HTTP Fetcher Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The device fetches its own feeds over HTTPS on a compose wake, skips what has not changed, and composes an edition without a computer involved.

**Architecture:** As much as possible is portable and unit-tested — response parsing, body framing, and the conditional-GET cache all live in `src/core/net/` and are exercised against fixtures on a laptop. Only the TLS socket, the WiFi join, and gzip inflate live in `src/device/`, behind `IHttpClient`.

**Tech Stack:** `WiFiClientSecure` (Arduino ESP32 core 2.0.17), the platform's zlib, and the existing `ByteSource` / `LimitedByteSource` seam.

**Spec:** `docs/superpowers/specs/2026-08-19-inkplate-firmware-design.md`

## Global Constraints

- **`src/core/` and `src/hal/` must never include a platform header.** `tools/check-portability.sh` gates it. Everything in `src/core/net/` is plain C++17 over `ByteSource`.
- **Never bring up WiFi on the read path.** A resident `Edition` costs ~221 KB of internal RAM, leaving ~69 KB — less than mbedTLS needs. Fetching happens only on a compose wake, which never constructs a `Reader`.
- **Nothing scales with input size.** Every buffer gets a cap in an options struct, matching the existing `XmlLimits` / `HtmlLimits` pattern.
- **Parsers recover, they never abort.** A malformed response costs one feed, never the edition.
- **TLS is encrypted but unverified** (`setInsecure()`), a deliberate decision recorded in DECISIONS.md. Do not silently "improve" this to certificate pinning; it interacts with an RTC that starts unset.
- **Secrets never enter the repo.** WiFi credentials live in `feeds.toml` on the card. No credential, SSID or password may appear in a commit, a test fixture, or a log line.
- **Serial is 115200.** Device verification steps read it non-interactively.

---

### Task 1: HTTP response head parser

**Files:**
- Create: `src/core/net/http_response.h`, `src/core/net/http_response.cpp`
- Test: `test/http_response_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `struct HttpHead { int status = 0; std::string etag, last_modified, location, content_encoding; long long content_length = -1; bool chunked = false; }` and `class HttpHeadParser` with `bool feed(const char* data, size_t n)`, `bool done() const`, `const HttpHead& head() const`, `size_t body_offset() const`.

The parser is incremental because the head arrives in whatever chunks TLS hands over, and it must tolerate a head split mid-header-name.

- [ ] **Step 1: Write the failing test**

```cpp
#include "core/net/http_response.h"

#include <string>

#include "doctest.h"

using namespace diarium;

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
  const std::string raw =
      "HTTP/1.1 304 Not Modified\r\nETag: \"zz\"\r\n\r\n";
  HttpHeadParser p;
  for (size_t i = 0; i < raw.size(); ++i) p.feed(raw.data() + i, 1);
  REQUIRE(p.done());
  CHECK(p.head().status == 304);
  CHECK(p.head().etag == "\"zz\"");
}

TEST_CASE("chunked transfer encoding is recognised") {
  HttpHeadParser p = parse_all(
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
  REQUIRE(p.done());
  CHECK(p.head().chunked);
  CHECK(p.head().content_length == -1);
}

TEST_CASE("a redirect carries its location") {
  HttpHeadParser p = parse_all(
      "HTTP/1.1 301 Moved Permanently\r\n"
      "Location: https://example.com/feed.xml\r\n\r\n");
  REQUIRE(p.done());
  CHECK(p.head().status == 301);
  CHECK(p.head().location == "https://example.com/feed.xml");
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
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `make tests && ./bin/diarium-tests --test-case="*status line*"`
Expected: FAIL — `core/net/http_response.h` does not exist.

- [ ] **Step 3: Write the header**

```cpp
// Parsing an HTTP response head, incrementally.
//
// Incremental because the head arrives in whatever chunks TLS hands over, and
// a header can be split anywhere — including between a name and its colon.
// Portable, so it can be tested against real captured responses on a laptop.
#pragma once

#include <cstddef>
#include <string>

namespace diarium {

struct HttpHead {
  int status = 0;
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

  // Returns false once the head has exceeded its cap; the caller drops the
  // response. `done()` goes true at the blank line.
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

}  // namespace diarium
```

- [ ] **Step 4: Write the implementation**

```cpp
#include "core/net/http_response.h"

#include <cstdlib>

namespace diarium {
namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

bool name_is(const std::string& line, size_t colon, const char* want) {
  size_t i = 0;
  for (; i < colon && want[i] != '\0'; ++i) {
    if (lower(line[i]) != want[i]) return false;
  }
  return i == colon && want[i] == '\0';
}

std::string trimmed(const std::string& s, size_t from) {
  size_t a = from;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
  size_t b = s.size();
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return s.substr(a, b - a);
}

}  // namespace

void HttpHeadParser::take_line(const std::string& line) {
  if (!saw_status_) {
    saw_status_ = true;
    // "HTTP/1.1 200 OK" — anything else and the status stays 0, which the
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
    for (size_t i = 0; i + 6 < value.size() + 1; ++i) {
      if (name_is(value.substr(i, 7) + ":", 7, "chunked")) {
        head_.chunked = true;
        break;
      }
    }
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
    // Strip a trailing CR; bare LF is tolerated because servers emit it.
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

}  // namespace diarium
```

- [ ] **Step 5: Run the tests**

Run: `make check`
Expected: PASS, 182 test cases.

- [ ] **Step 6: Commit**

```bash
git add src/core/net/http_response.h src/core/net/http_response.cpp test/http_response_test.cpp
git commit -m "Parse HTTP response heads incrementally"
```

---

### Task 2: Body framing as a ByteSource

**Files:**
- Create: `src/core/net/http_body.h`, `src/core/net/http_body.cpp`
- Test: `test/http_body_test.cpp`

**Interfaces:**
- Consumes: `HttpHead` from Task 1.
- Produces: `class HttpBodySource final : public ByteSource` with `HttpBodySource(ByteSource& inner, const HttpHead& head, std::string prefetched)`, where `prefetched` is the body bytes that arrived alongside the head.

Chunked framing is portable and is where real servers break parsers, so it is tested here rather than discovered on device.

- [ ] **Step 1: Write the failing test**

```cpp
#include "core/net/http_body.h"

#include <string>

#include "core/io/byte_source.h"
#include "core/net/http_response.h"
#include "doctest.h"

using namespace diarium;

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
  MemoryByteSource inner("56789");
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

TEST_CASE("a truncated chunked body stops rather than looping") {
  HttpHead head;
  head.chunked = true;
  MemoryByteSource inner("4\r\nWi");  // the stream dies mid-chunk
  HttpBodySource body(inner, head, "");
  CHECK(drain(&body) == "Wi");
}

TEST_CASE("an unstated length reads until the stream ends") {
  HttpHead head;  // no content-length, not chunked
  MemoryByteSource inner("everything until close");
  HttpBodySource body(inner, head, "");
  CHECK(drain(&body) == "everything until close");
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `make tests && ./bin/diarium-tests --test-case="*chunked*"`
Expected: FAIL — `core/net/http_body.h` does not exist.

- [ ] **Step 3: Write the header**

```cpp
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
  long long remaining_ = -1;   // for content-length framing
  bool chunked_ = false;
  long long chunk_left_ = 0;   // bytes left in the current chunk
  bool finished_ = false;
};

}  // namespace diarium
```

- [ ] **Step 4: Write the implementation**

```cpp
#include "core/net/http_body.h"

namespace diarium {

HttpBodySource::HttpBodySource(ByteSource& inner, const HttpHead& head,
                               std::string prefetched)
    : inner_(inner), prefetched_(std::move(prefetched)),
      remaining_(head.content_length), chunked_(head.chunked) {}

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
    if (c != '\r' && line.size() < 32) line.push_back(c);
  }
  long long size = 0;
  bool any = false;
  for (char d : line) {
    int v;
    if (d >= '0' && d <= '9') v = d - '0';
    else if (d >= 'a' && d <= 'f') v = d - 'a' + 10;
    else if (d >= 'A' && d <= 'F') v = d - 'A' + 10;
    else break;  // a chunk extension, or the end of the number
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
      if (!read_chunk_header() || chunk_left_ == 0) {
        finished_ = true;
        return 0;
      }
    }
    const size_t want =
        n < static_cast<size_t>(chunk_left_) ? n : static_cast<size_t>(chunk_left_);
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
    const size_t want =
        n < static_cast<size_t>(remaining_) ? n : static_cast<size_t>(remaining_);
    const size_t got = read_raw(dst, want);
    remaining_ -= static_cast<long long>(got);
    if (got == 0) finished_ = true;
    return got;
  }

  const size_t got = read_raw(dst, n);
  if (got == 0) finished_ = true;
  return got;
}

}  // namespace diarium
```

- [ ] **Step 5: Run the tests**

Run: `make check`
Expected: PASS, 188 test cases.

- [ ] **Step 6: Commit**

```bash
git add src/core/net/http_body.h src/core/net/http_body.cpp test/http_body_test.cpp
git commit -m "Frame HTTP bodies as a ByteSource"
```

---

### Task 3: The conditional-GET cache

**Files:**
- Create: `src/core/net/feed_cache.h`, `src/core/net/feed_cache.cpp`
- Test: `test/feed_cache_test.cpp`

**Interfaces:**
- Consumes: `IStorage` from `hal/hal.h`.
- Produces: `struct FeedValidators { std::string etag, last_modified; }` and `class FeedCache` with `explicit FeedCache(IStorage* storage)`, `void load(const std::string& path)`, `bool save(const std::string& path)`, `FeedValidators get(const std::string& url) const`, `void put(const std::string& url, const FeedValidators&)`.

This is what makes most mornings cheap: most feeds have not changed, and a 304 costs a handshake instead of a download.

- [ ] **Step 1: Write the failing test**

```cpp
#include "core/net/feed_cache.h"

#include <map>
#include <string>

#include "doctest.h"
#include "hal/hal.h"

using namespace diarium;

namespace {

// A storage that lives in a map, so the cache is testable without a card.
class FakeStorage final : public IStorage {
 public:
  bool read(const std::string& path, std::string* out) override {
    auto it = files_.find(path);
    if (it == files_.end()) return false;
    *out = it->second;
    return true;
  }
  bool write(const std::string& path, const std::string& data) override {
    files_[path] = data;
    return true;
  }
  bool exists(const std::string& path) override {
    return files_.count(path) != 0;
  }
  bool remove(const std::string& path) override {
    return files_.erase(path) != 0;
  }
  std::map<std::string, std::string> files_;
};

}  // namespace

TEST_CASE("an unknown feed has no validators") {
  FakeStorage fs;
  FeedCache cache(&fs);
  CHECK(cache.get("https://example.com/f").etag.empty());
}

TEST_CASE("validators survive a save and load") {
  FakeStorage fs;
  {
    FeedCache cache(&fs);
    cache.put("https://example.com/f", {"\"abc\"", "Wed, 21 Oct 2026 07:28:00 GMT"});
    REQUIRE(cache.save("/cache.dat"));
  }
  FeedCache reloaded(&fs);
  reloaded.load("/cache.dat");
  CHECK(reloaded.get("https://example.com/f").etag == "\"abc\"");
  CHECK(reloaded.get("https://example.com/f").last_modified ==
        "Wed, 21 Oct 2026 07:28:00 GMT");
}

TEST_CASE("a second feed does not overwrite the first") {
  FakeStorage fs;
  FeedCache cache(&fs);
  cache.put("https://a.example/f", {"\"a\"", ""});
  cache.put("https://b.example/f", {"\"b\"", ""});
  CHECK(cache.get("https://a.example/f").etag == "\"a\"");
  CHECK(cache.get("https://b.example/f").etag == "\"b\"");
}

TEST_CASE("putting a url twice replaces rather than appends") {
  FakeStorage fs;
  FeedCache cache(&fs);
  cache.put("https://a.example/f", {"\"old\"", ""});
  cache.put("https://a.example/f", {"\"new\"", ""});
  CHECK(cache.get("https://a.example/f").etag == "\"new\"");
  REQUIRE(cache.save("/cache.dat"));
  FeedCache reloaded(&fs);
  reloaded.load("/cache.dat");
  CHECK(reloaded.get("https://a.example/f").etag == "\"new\"");
}

TEST_CASE("a missing cache file is not an error") {
  FakeStorage fs;
  FeedCache cache(&fs);
  cache.load("/nothing-here.dat");
  CHECK(cache.get("https://example.com/f").etag.empty());
}

TEST_CASE("a corrupt cache file costs the cache, not the edition") {
  FakeStorage fs;
  fs.write("/cache.dat", std::string("\x01\x02not a cache at all", 20));
  FeedCache cache(&fs);
  cache.load("/cache.dat");
  CHECK(cache.get("https://example.com/f").etag.empty());
}

TEST_CASE("the cache is bounded") {
  FakeStorage fs;
  FeedCache cache(&fs);
  for (int i = 0; i < 500; ++i) {
    cache.put("https://example.com/" + std::to_string(i), {"\"e\"", ""});
  }
  CHECK(cache.size() <= 128);
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `make tests && ./bin/diarium-tests --test-case="*validators*"`
Expected: FAIL — `core/net/feed_cache.h` does not exist.

- [ ] **Step 3: Write the header**

```cpp
// ETag and Last-Modified per feed, so a wake that finds nothing new costs a
// handshake instead of a download. Most mornings, most feeds are unchanged,
// and that is the difference between a cheap wake and an expensive one.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "hal/hal.h"

namespace diarium {

struct FeedValidators {
  std::string etag;
  std::string last_modified;
};

class FeedCache {
 public:
  explicit FeedCache(IStorage* storage) : storage_(storage) {}

  // Absent or corrupt is not an error: it costs a round of downloads, not
  // the edition.
  void load(const std::string& path);
  bool save(const std::string& path);

  FeedValidators get(const std::string& url) const;
  void put(const std::string& url, const FeedValidators& v);
  size_t size() const { return entries_.size(); }

  // A reader with more feeds than this has other problems; the cap is here
  // because nothing may scale with input.
  static constexpr size_t kMaxEntries = 128;

 private:
  struct Entry {
    std::string url;
    FeedValidators validators;
  };

  IStorage* storage_;
  std::vector<Entry> entries_;
};

}  // namespace diarium
```

- [ ] **Step 4: Write the implementation**

```cpp
#include "core/net/feed_cache.h"

namespace diarium {
namespace {

constexpr char kMagic[4] = {'R', 'S', 'C', '1'};
constexpr size_t kMaxField = 512;

void put_u16(std::string& out, uint16_t v) {
  out.push_back(static_cast<char>(v & 0xFF));
  out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void put_str(std::string& out, const std::string& s) {
  const uint16_t n = static_cast<uint16_t>(s.size() > kMaxField ? kMaxField : s.size());
  put_u16(out, n);
  out.append(s, 0, n);
}

bool get_u16(const std::string& in, size_t* at, uint16_t* out) {
  if (*at + 2 > in.size()) return false;
  *out = static_cast<uint16_t>(static_cast<uint8_t>(in[*at]) |
                               (static_cast<uint8_t>(in[*at + 1]) << 8));
  *at += 2;
  return true;
}

bool get_str(const std::string& in, size_t* at, std::string* out) {
  uint16_t n = 0;
  if (!get_u16(in, at, &n)) return false;
  if (n > kMaxField || *at + n > in.size()) return false;
  out->assign(in, *at, n);
  *at += n;
  return true;
}

}  // namespace

void FeedCache::load(const std::string& path) {
  entries_.clear();
  if (storage_ == nullptr) return;

  std::string blob;
  if (!storage_->read(path, &blob)) return;
  if (blob.size() < 6 || blob.compare(0, 4, kMagic, 4) != 0) return;

  size_t at = 4;
  uint16_t count = 0;
  if (!get_u16(blob, &at, &count)) return;
  if (count > kMaxEntries) count = kMaxEntries;

  for (uint16_t i = 0; i < count; ++i) {
    Entry e;
    if (!get_str(blob, &at, &e.url)) { entries_.clear(); return; }
    if (!get_str(blob, &at, &e.validators.etag)) { entries_.clear(); return; }
    if (!get_str(blob, &at, &e.validators.last_modified)) { entries_.clear(); return; }
    entries_.push_back(std::move(e));
  }
}

bool FeedCache::save(const std::string& path) {
  if (storage_ == nullptr) return false;
  std::string blob(kMagic, 4);
  put_u16(blob, static_cast<uint16_t>(entries_.size()));
  for (const Entry& e : entries_) {
    put_str(blob, e.url);
    put_str(blob, e.validators.etag);
    put_str(blob, e.validators.last_modified);
  }
  return storage_->write(path, blob);
}

FeedValidators FeedCache::get(const std::string& url) const {
  for (const Entry& e : entries_) {
    if (e.url == url) return e.validators;
  }
  return FeedValidators{};
}

void FeedCache::put(const std::string& url, const FeedValidators& v) {
  for (Entry& e : entries_) {
    if (e.url == url) {
      e.validators = v;
      return;
    }
  }
  if (entries_.size() >= kMaxEntries) return;
  entries_.push_back(Entry{url, v});
}

}  // namespace diarium
```

- [ ] **Step 5: Run the tests**

Run: `make check`
Expected: PASS, 195 test cases.

- [ ] **Step 6: Commit**

```bash
git add src/core/net/feed_cache.h src/core/net/feed_cache.cpp test/feed_cache_test.cpp
git commit -m "Remember ETag and Last-Modified per feed"
```

---

### Task 4: WiFi credentials in feeds.toml

**Files:**
- Modify: `src/core/config/feeds_config.h`, `src/core/config/feeds_config.cpp`
- Modify: `test/feeds_config_test.cpp`

**Interfaces:**
- Produces: `struct WifiConfig { std::string ssid, password; bool configured() const { return !ssid.empty(); } }` on `FeedList` as `wifi`.

**No credential may appear in a commit, a fixture, or a log line.** The tests below use obvious dummies; the real values live only on the card.

- [ ] **Step 1: Write the failing test**

Append to `test/feeds_config_test.cpp`:

```cpp
TEST_CASE("wifi credentials parse from their own section") {
  FeedList list;
  std::string error;
  REQUIRE(parse_feeds_toml("[wifi]\nssid = \"EXAMPLE-SSID\"\n"
                           "password = \"EXAMPLE-PASSWORD\"\n"
                           "[[feed]]\nurl = \"http://example.com/f\"\n",
                           &list, &error));
  CHECK(list.wifi.ssid == "EXAMPLE-SSID");
  CHECK(list.wifi.password == "EXAMPLE-PASSWORD");
  CHECK(list.wifi.configured());
}

TEST_CASE("no wifi section means not configured") {
  FeedList list;
  std::string error;
  REQUIRE(parse_feeds_toml("[[feed]]\nurl = \"http://example.com/f\"\n",
                           &list, &error));
  CHECK_FALSE(list.wifi.configured());
}

TEST_CASE("an open network needs no password") {
  FeedList list;
  std::string error;
  REQUIRE(parse_feeds_toml("[wifi]\nssid = \"EXAMPLE-OPEN\"\n"
                           "[[feed]]\nurl = \"http://example.com/f\"\n",
                           &list, &error));
  CHECK(list.wifi.configured());
  CHECK(list.wifi.password.empty());
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `make tests && ./bin/diarium-tests --test-case="*wifi*"`
Expected: FAIL — no member named `wifi`.

- [ ] **Step 3: Add the struct**

In `src/core/config/feeds_config.h`, above `FeedList`:

```cpp
// Credentials live on the card and never in the repo. The device has no
// keyboard, so this is how a network gets configured.
struct WifiConfig {
  std::string ssid;
  std::string password;
  bool configured() const { return !ssid.empty(); }
};
```

and add to `FeedList`, beside `edition`:

```cpp
  WifiConfig wifi;
```

- [ ] **Step 4: Parse the section**

`parse_feeds_toml` dispatches on a `Table` enum, not a string. Add a member to it:

```cpp
enum class Table { None, Edition, Feed, Wifi };
```

Recognise the header alongside the existing two (around `feeds_config.cpp:142`):

```cpp
      } else if (name == "[wifi]") {
        table = Table::Wifi;
```

and handle its two keys where the other tables are handled:

```cpp
    if (table == Table::Wifi) {
      if (key == "ssid") {
        out->wifi.ssid = value;
      } else if (key == "password") {
        out->wifi.password = value;
      }
      continue;
    }
```

Note that an unrecognised table is already skipped rather than treated as
fatal, so a `feeds.toml` carrying `[wifi]` still parses on a build that
predates this change.

- [ ] **Step 5: Run the tests**

Run: `make check`
Expected: PASS, 198 test cases.

- [ ] **Step 6: Commit**

```bash
git add src/core/config test/feeds_config_test.cpp
git commit -m "Carry wifi credentials on the card"
```

---

### Task 5: The device HTTP client

**Files:**
- Create: `src/device/device_wifi.h`, `src/device/device_wifi.cpp`
- Rewrite: `src/device/device_http.h`, create `src/device/device_http.cpp`
- Modify: `src/device/device_hal.h`

**Interfaces:**
- Consumes: `HttpHeadParser`, `HttpBodySource`, `FeedCache`, `WifiConfig`.
- Produces: `class DeviceWifi` with `bool connect(const WifiConfig&, uint32_t timeout_ms)`, `void disconnect()`, `bool connected() const`; and a real `DeviceHttpClient` with `void set_cache(FeedCache*)`.

TLS is `setInsecure()` — encrypted, unverified — per the recorded decision. Do not change that here.

- [ ] **Step 1: Write the WiFi join**

```cpp
// Joining a network, and getting off it again promptly: the radio is the
// dominant draw on a compose wake.
#pragma once

#include <cstdint>

#include "core/config/feeds_config.h"

namespace diarium {
namespace device {

class DeviceWifi {
 public:
  bool connect(const WifiConfig& config, uint32_t timeout_ms = 20000);
  void disconnect();
  bool connected() const;
};

}  // namespace device
}  // namespace diarium
```

```cpp
#include "device/device_wifi.h"

#include <Arduino.h>
#include <WiFi.h>

namespace diarium {
namespace device {

bool DeviceWifi::connect(const WifiConfig& config, uint32_t timeout_ms) {
  if (!config.configured()) return false;
  WiFi.mode(WIFI_STA);
  // Never log the credentials: this line is the one that would leak them.
  WiFi.begin(config.ssid.c_str(),
             config.password.empty() ? nullptr : config.password.c_str());

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < timeout_ms) {
    delay(200);
  }
  return WiFi.status() == WL_CONNECTED;
}

void DeviceWifi::disconnect() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
}

bool DeviceWifi::connected() const { return WiFi.status() == WL_CONNECTED; }

}  // namespace device
}  // namespace diarium
```

- [ ] **Step 2: Write the client**

The code for this step and for Task 6 is written when the task is reached
rather than here: both are shaped by the exact `WiFiClientSecure` and `z_stream`
surfaces on Arduino core 2.0.17, and guessing at them in a plan is how the
first plan ended up asserting `src_dir` in the wrong section. The behaviour it
must implement is fully specified below.

`DeviceHttpClient::get` opens `WiFiClientSecure` with `setInsecure()`, sends the request with `If-None-Match` / `If-Modified-Since` from the cache when present and `Accept-Encoding: gzip`, drives `HttpHeadParser` until `done()`, then:

- **304** — fills `out` with the status, returns `nullptr`. The caller reuses the previous items.
- **301/302/307/308** — follows `Location`, at most 3 hops, then gives up with an error naming the limit.
- **200** — records the new validators in the cache, wraps the socket in `HttpBodySource`, then in `GzipByteSource` if `content_encoding` contains `gzip`, then in `LimitedByteSource` at `request.max_bytes`.
- **anything else** — `out->error` gets the status, returns `nullptr`.

A socket adapter implements `ByteSource` over `WiFiClientSecure`, returning 0 when the connection closes and setting `failed()` on error.

- [ ] **Step 3: Verify against one real feed**

Put a `feeds.toml` on the card carrying a `[wifi]` section and one feed. Add a temporary compose-path call that fetches it and prints status, byte count and elapsed time.

Run: `cd src/device && pio run -t upload`
Expected: a 200 with a plausible byte count on the first run, and a **304 on the second run within a minute** — which is the whole point of the issue.

- [ ] **Step 4: Commit**

```bash
git add src/device
git commit -m "Fetch feeds over TLS with conditional GET"
```

---

### Task 6: gzip

**Files:**
- Create: `src/device/gzip_byte_source.h`, `src/device/gzip_byte_source.cpp`

**Interfaces:**
- Produces: `class GzipByteSource final : public ByteSource` wrapping another source, inflating on the fly.

Uses the platform's zlib, which ships with ESP-IDF — no new vendored dependency, and it sits below the HAL so the core never sees it.

- [ ] **Step 1: Implement streaming inflate**

Wrap `z_stream` with `inflateInit2(&z, 16 + MAX_WBITS)` for gzip framing. Pull from the inner source into a fixed input buffer (4 KB) and inflate into the caller's buffer. On `Z_DATA_ERROR`, mark failed and return 0 — a corrupt response costs one feed.

- [ ] **Step 2: Verify the win**

Fetch the same feed with and without `Accept-Encoding: gzip`, printing bytes on the wire and elapsed radio time.
Expected: a 3-5x reduction in bytes for a typical XML feed.

- [ ] **Step 3: Commit**

```bash
git add src/device
git commit -m "Inflate gzipped feeds on the fly"
```

---

### Task 7: The compose wake does its job

**Files:**
- Modify: `src/device/main.cpp`
- Modify: `DECISIONS.md`

- [ ] **Step 1: Fill in the compose path**

Replace the "no network yet" stub. On a compose wake: read `feeds.toml`, join WiFi, load the cache, fetch each feed through `parse_feed` into items, compose, persist the edition to the card, save the cache, drop the radio, arm tomorrow's alarm, sleep. **Never construct a `Reader`.**

A failed feed costs that feed: record it in `feed_problems` so the colophon can say what happened, and carry on.

- [ ] **Step 2: Record the TLS decision**

Append to `DECISIONS.md`:

```markdown
### 31. TLS is encrypted but not verified

`WiFiClientSecure::setInsecure()`. Feed traffic is encrypted against passive
observation, and an active attacker who can intercept the connection can
substitute content.

The alternatives were both worse here. A bundled CA store costs flash that is
already scarce at 4 MB, needs refreshing as roots rotate, and — more
awkwardly — certificate expiry checks need a real clock, while this device's
RTC starts unset and has no network to ask until after the connection it is
trying to validate. Per-feed pinning breaks silently whenever a publisher
rotates a certificate, which on a device that fetches once a day means a feed
quietly dying with no visible cause.

This is a known weakness, written down rather than left implicit. The threat
it does not cover is an attacker on the reader's own network substituting
newspaper content, which is a strange thing to want.
```

- [ ] **Step 3: Verify end to end**

Run a compose wake with a real `feeds.toml` on the card.
Expected: an edition composed from live feeds, persisted, and readable after a reset — and a second compose within the minute that is visibly faster and mostly 304s, which is issue #3's stated done-condition.

- [ ] **Step 4: Commit**

```bash
git add src/device DECISIONS.md
git commit -m "Compose an edition from live feeds"
```
