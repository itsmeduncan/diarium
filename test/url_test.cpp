// Splitting URLs and resolving redirects. Location headers are relative more
// often than they should be, and a feed that redirects into nonsense should
// cost that feed rather than the edition.
#include "core/net/url.h"

#include <string>

#include "doctest.h"

using namespace rsspaper;

TEST_CASE("splits a plain https url") {
  Url u;
  REQUIRE(parse_url("https://example.com/feed.xml", &u));
  CHECK(u.secure);
  CHECK(u.host == "example.com");
  CHECK(u.port == 443);
  CHECK(u.path == "/feed.xml");
}

TEST_CASE("http defaults to port 80") {
  Url u;
  REQUIRE(parse_url("http://example.com/f", &u));
  CHECK_FALSE(u.secure);
  CHECK(u.port == 80);
}

TEST_CASE("an explicit port wins") {
  Url u;
  REQUIRE(parse_url("https://example.com:8443/f", &u));
  CHECK(u.port == 8443);
  CHECK(u.host == "example.com");
}

TEST_CASE("an empty path becomes a slash") {
  Url u;
  REQUIRE(parse_url("https://example.com", &u));
  CHECK(u.path == "/");
}

TEST_CASE("a query string stays with the path") {
  Url u;
  REQUIRE(parse_url("https://example.com/f?format=rss&n=10", &u));
  CHECK(u.path == "/f?format=rss&n=10");
}

TEST_CASE("an unknown scheme is refused") {
  Url u;
  CHECK_FALSE(parse_url("gopher://example.com/f", &u));
  CHECK_FALSE(parse_url("example.com/f", &u));
  CHECK_FALSE(parse_url("", &u));
}

TEST_CASE("an absolute redirect replaces everything") {
  CHECK(resolve_url("https://a.example/old", "https://b.example/new") ==
        "https://b.example/new");
}

TEST_CASE("a root-relative redirect keeps the host") {
  CHECK(resolve_url("https://a.example/old/feed.xml", "/new/feed.xml") ==
        "https://a.example/new/feed.xml");
}

TEST_CASE("a path-relative redirect keeps the directory") {
  CHECK(resolve_url("https://a.example/old/feed.xml", "other.xml") ==
        "https://a.example/old/other.xml");
}

TEST_CASE("a protocol-relative redirect keeps the scheme") {
  CHECK(resolve_url("https://a.example/f", "//b.example/g") ==
        "https://b.example/g");
}

TEST_CASE("an unresolvable redirect yields nothing rather than a guess") {
  CHECK(resolve_url("not a url", "/somewhere").empty());
  CHECK(resolve_url("https://a.example/f", "").empty());
}
