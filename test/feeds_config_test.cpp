// Config keys that the device cannot ask the network about.
//
// Every fixture carries a feed: a config with none is rejected by design.
#include <string>

#include "core/config/feeds_config.h"
#include "doctest.h"

using namespace rsspaper;

TEST_CASE("edition carries a utc offset in minutes") {
  FeedList list;
  std::string error;
  REQUIRE(parse_feeds_toml("[edition]\nutc_offset_minutes = -300\n"
                           "[[feed]]\nurl = \"http://example.com/f\"\n", &list,
                           &error));
  CHECK(list.edition.utc_offset_minutes == -300);
}

TEST_CASE("a positive utc offset parses") {
  FeedList list;
  std::string error;
  REQUIRE(parse_feeds_toml("[edition]\nutc_offset_minutes = 330\n"
                           "[[feed]]\nurl = \"http://example.com/f\"\n", &list,
                           &error));
  CHECK(list.edition.utc_offset_minutes == 330);
}

TEST_CASE("utc offset defaults to UTC") {
  FeedList list;
  std::string error;
  REQUIRE(parse_feeds_toml("[edition]\ntitle = \"Paper\"\n"
                           "[[feed]]\nurl = \"http://example.com/f\"\n", &list, &error));
  CHECK(list.edition.utc_offset_minutes == 0);
}

TEST_CASE("a nonsense utc offset is an error that names the key") {
  FeedList list;
  std::string error;
  CHECK_FALSE(parse_feeds_toml("[edition]\nutc_offset_minutes = east\n"
                               "[[feed]]\nurl = \"http://example.com/f\"\n", &list,
                               &error));
  CHECK(error.find("utc_offset_minutes") != std::string::npos);
}
