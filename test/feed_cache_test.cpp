// ETag and Last-Modified per feed. Most mornings most feeds are unchanged,
// and this is the difference between a cheap wake and an expensive one.
//
// The cache takes data rather than a HAL, so these tests need no storage.
#include "core/net/feed_cache.h"

#include <string>

#include "doctest.h"

using namespace diarium;

TEST_CASE("an unknown feed has no validators") {
  FeedCache cache;
  CHECK(cache.get("https://example.com/f").etag.empty());
}

TEST_CASE("validators survive a round trip through a blob") {
  FeedCache cache;
  cache.put("https://example.com/f",
            {"\"abc\"", "Wed, 21 Oct 2026 07:28:00 GMT"});
  const std::string blob = cache.serialize();

  FeedCache reloaded;
  REQUIRE(reloaded.deserialize(blob));
  CHECK(reloaded.get("https://example.com/f").etag == "\"abc\"");
  CHECK(reloaded.get("https://example.com/f").last_modified ==
        "Wed, 21 Oct 2026 07:28:00 GMT");
}

TEST_CASE("a second feed does not overwrite the first") {
  FeedCache cache;
  cache.put("https://a.example/f", {"\"a\"", ""});
  cache.put("https://b.example/f", {"\"b\"", ""});
  CHECK(cache.get("https://a.example/f").etag == "\"a\"");
  CHECK(cache.get("https://b.example/f").etag == "\"b\"");
}

TEST_CASE("putting a url twice replaces rather than appends") {
  FeedCache cache;
  cache.put("https://a.example/f", {"\"old\"", ""});
  cache.put("https://a.example/f", {"\"new\"", ""});
  CHECK(cache.get("https://a.example/f").etag == "\"new\"");
  CHECK(cache.size() == 1);

  FeedCache reloaded;
  REQUIRE(reloaded.deserialize(cache.serialize()));
  CHECK(reloaded.get("https://a.example/f").etag == "\"new\"");
  CHECK(reloaded.size() == 1);
}

TEST_CASE("an empty cache round-trips") {
  FeedCache cache;
  FeedCache reloaded;
  REQUIRE(reloaded.deserialize(cache.serialize()));
  CHECK(reloaded.size() == 0);
}

TEST_CASE("a corrupt blob costs the cache, not the edition") {
  FeedCache cache;
  CHECK_FALSE(cache.deserialize(std::string("\x01\x02not a cache at all", 20)));
  CHECK(cache.size() == 0);
  CHECK(cache.get("https://example.com/f").etag.empty());
}

TEST_CASE("an empty blob is refused rather than half-read") {
  FeedCache cache;
  CHECK_FALSE(cache.deserialize(""));
  CHECK(cache.size() == 0);
}

TEST_CASE("a truncated blob is discarded rather than half-trusted") {
  FeedCache cache;
  cache.put("https://a.example/f", {"\"a\"", "yesterday"});
  std::string blob = cache.serialize();
  blob.resize(blob.size() - 4);

  FeedCache reloaded;
  CHECK_FALSE(reloaded.deserialize(blob));
  CHECK(reloaded.size() == 0);
}

TEST_CASE("deserializing replaces whatever was held before") {
  FeedCache cache;
  cache.put("https://old.example/f", {"\"old\"", ""});

  FeedCache other;
  other.put("https://new.example/f", {"\"new\"", ""});
  REQUIRE(cache.deserialize(other.serialize()));

  CHECK(cache.get("https://old.example/f").etag.empty());
  CHECK(cache.get("https://new.example/f").etag == "\"new\"");
}

TEST_CASE("the cache is bounded") {
  FeedCache cache;
  for (int i = 0; i < 500; ++i) {
    cache.put("https://example.com/" + std::to_string(i), {"\"e\"", ""});
  }
  CHECK(cache.size() <= FeedCache::kMaxEntries);
}

TEST_CASE("an absurd validator is clipped rather than stored whole") {
  FeedCache cache;
  cache.put("https://example.com/f", {std::string(4096, 'e'), ""});
  FeedCache reloaded;
  REQUIRE(reloaded.deserialize(cache.serialize()));
  CHECK(reloaded.get("https://example.com/f").etag.size() <= 512);
}
