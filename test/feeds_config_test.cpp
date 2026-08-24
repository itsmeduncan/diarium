// Config keys that the device cannot ask the network about.
//
// Every fixture carries a feed: a config with none is rejected by design.
#include <string>

#include "core/config/feeds_config.h"
#include "doctest.h"

using namespace diarium;

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

TEST_CASE("orientation defaults to portrait and parses both ways") {
  // Portrait is the product default: an [edition] with no orientation key is
  // the common case, and the device stands on its short edge out of the box.
  FeedList list;
  std::string error;
  REQUIRE(parse_feeds_toml("[[feed]]\nurl = \"http://example.com/f\"\n",
                           &list, &error));
  CHECK(list.edition.orientation == Orientation::Portrait);

  REQUIRE(parse_feeds_toml("[edition]\norientation = \"landscape\"\n"
                           "[[feed]]\nurl = \"http://example.com/f\"\n",
                           &list, &error));
  CHECK(list.edition.orientation == Orientation::Landscape);

  REQUIRE(parse_feeds_toml("[edition]\norientation = \"portrait\"\n"
                           "[[feed]]\nurl = \"http://example.com/f\"\n",
                           &list, &error));
  CHECK(list.edition.orientation == Orientation::Portrait);
}

TEST_CASE("an unknown orientation is a legible error, not a silent default") {
  FeedList list;
  std::string error;
  CHECK_FALSE(parse_feeds_toml("[edition]\norientation = \"sideways\"\n"
                               "[[feed]]\nurl = \"http://example.com/f\"\n",
                               &list, &error));
  CHECK(error.find("orientation") != std::string::npos);
}

TEST_CASE("wifi credentials parse from their own section") {
  // Dummies only. Real credentials live on the card and never in the repo.
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

TEST_CASE("a password may contain characters a shell would hate") {
  FeedList list;
  std::string error;
  REQUIRE(parse_feeds_toml("[wifi]\nssid = \"EXAMPLE\"\n"
                           "password = \"a b#c=d[e]\"\n"
                           "[[feed]]\nurl = \"http://example.com/f\"\n",
                           &list, &error));
  CHECK(list.wifi.password == "a b#c=d[e]");
}
