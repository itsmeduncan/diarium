#include "core/base/datetime.h"
#include "core/base/str.h"
#include "core/xml/entities.h"
#include "doctest.h"

using namespace rsspaper;

TEST_CASE("utf8 round-trips and rejects malformed input") {
  std::string s;
  utf8_append(s, 0x41);
  utf8_append(s, 0xE9);      // é
  utf8_append(s, 0x2019);    // ’
  utf8_append(s, 0x1F600);   // emoji, 4 bytes
  CHECK(s.size() == 1 + 2 + 3 + 4);

  size_t i = 0;
  CHECK(utf8_next(s, i) == 0x41);
  CHECK(utf8_next(s, i) == 0xE9);
  CHECK(utf8_next(s, i) == 0x2019);
  CHECK(utf8_next(s, i) == 0x1F600);
  CHECK(i == s.size());
  CHECK(utf8_length(s) == 4);

  SUBCASE("a truncated sequence consumes one byte and yields U+FFFD") {
    const std::string bad = "\xE2\x80";  // start of ’, cut short
    size_t j = 0;
    CHECK(utf8_next(bad, j) == 0xFFFD);
    CHECK(j == 1);
  }

  SUBCASE("overlong encodings are rejected") {
    const std::string overlong = "\xC0\x80";  // overlong NUL
    size_t j = 0;
    CHECK(utf8_next(overlong, j) == 0xFFFD);
  }
}

TEST_CASE("whitespace collapsing") {
  CHECK(collapse_ws("  a   b \n c  ") == "a b c");
  CHECK(collapse_ws("") == "");
  CHECK(collapse_ws("   ") == "");
  CHECK(collapse_ws("single") == "single");
}

TEST_CASE("case-insensitive helpers") {
  CHECK(iequals("Atom", "atom"));
  CHECK(iequals("ATOM", "atom"));
  CHECK_FALSE(iequals("atomic", "atom"));
  CHECK_FALSE(iequals("ato", "atom"));
  CHECK(starts_with("content:encoded", "content"));
  CHECK(ends_with("story…", "\xE2\x80\xA6"));
  CHECK(icontains("Please Continue Reading here", "continue reading"));
  CHECK_FALSE(icontains("short", "continue reading"));
}

TEST_CASE("entity decoding covers the forms feeds actually use") {
  uint32_t cp = 0;
  CHECK(decode_entity("amp", &cp));
  CHECK(cp == '&');
  CHECK(decode_entity("#8217", &cp));
  CHECK(cp == 0x2019);
  CHECK(decode_entity("#x2014", &cp));
  CHECK(cp == 0x2014);
  CHECK(decode_entity("nbsp", &cp));
  CHECK(cp == 0xA0);

  SUBCASE("numeric refs in the cp1252 range are remapped, per HTML5") {
    CHECK(decode_entity("#146", &cp));
    CHECK(cp == 0x2019);  // not U+0092
  }
  SUBCASE("unknown entities are refused so the caller can pass them through") {
    CHECK_FALSE(decode_entity("bogus", &cp));
    CHECK_FALSE(decode_entity("", &cp));
    CHECK_FALSE(decode_entity("#", &cp));
  }
  SUBCASE("a bare ampersand survives") {
    CHECK(decode_entities("Tom & Jerry") == "Tom & Jerry");
    CHECK(decode_entities("AT&amp;T") == "AT&T");
    CHECK(decode_entities("a &notreal; b") == "a &notreal; b");
  }
}

TEST_CASE("RFC 822 dates") {
  const Epoch t = parse_feed_date("Tue, 12 Aug 2025 09:31:00 -0400");
  REQUIRE(t != kNoDate);
  const CivilTime c = civil_from_epoch(t);
  CHECK(c.year == 2025);
  CHECK(c.month == 8);
  CHECK(c.day == 12);
  CHECK(c.hour == 13);  // -0400 normalised to UTC
  CHECK(c.minute == 31);

  CHECK(parse_feed_date("Wed, 13 Aug 2025 00:00:00 GMT") != kNoDate);
  CHECK(parse_feed_date("13 Aug 2025 00:00:00 GMT") != kNoDate);
  CHECK(parse_feed_date("Wed, 13 Aug 2025 05:00:00 EST") ==
        parse_feed_date("Wed, 13 Aug 2025 10:00:00 GMT"));
}

TEST_CASE("RFC 3339 dates") {
  const Epoch t = parse_feed_date("2025-08-12T09:31:00Z");
  REQUIRE(t != kNoDate);
  const CivilTime c = civil_from_epoch(t);
  CHECK(c.year == 2025);
  CHECK(c.month == 8);
  CHECK(c.day == 12);
  CHECK(c.hour == 9);

  CHECK(parse_feed_date("2025-08-12T09:31:00.123456Z") == t);
  CHECK(parse_feed_date("2025-08-12T05:31:00-04:00") == t);
  CHECK(parse_feed_date("2025-08-12T11:31:00+02:00") == t);
}

TEST_CASE("unparseable dates report failure rather than guessing") {
  CHECK(parse_feed_date("") == kNoDate);
  CHECK(parse_feed_date("last tuesday") == kNoDate);
  CHECK(parse_feed_date("not a date at all") == kNoDate);
}

TEST_CASE("civil/epoch round-trip across leap years and epochs") {
  for (const Epoch t : {Epoch(0), Epoch(951782400), Epoch(1767225600),
                        Epoch(-86400), Epoch(4102444800)}) {
    CHECK(epoch_from_civil(civil_from_epoch(t)) == t);
  }
  // 1970-01-01 was a Thursday.
  CHECK(civil_from_epoch(0).weekday == 4);
}

TEST_CASE("date formatting") {
  const Epoch t = parse_feed_date("2026-08-15T06:05:00Z");
  CHECK(format_masthead_date(t) == "Saturday, 15 August 2026");
  CHECK(format_short_date(t) == "15 Aug 2026");
  CHECK(format_clock(t) == "06:05");
}
