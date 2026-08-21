// The write-side mirror of ByteSource: everything downstream — the desktop's
// string blob, the device's SD card — appends through a ByteSink, which is
// what lets the page codec write to either without knowing which.
#include <string>

#include "core/io/byte_sink.h"
#include "doctest.h"

using namespace diarium;

TEST_CASE("a string sink appends and tracks position") {
  std::string out;
  StringSink sink(&out);
  CHECK(sink.position() == 0);
  REQUIRE(sink.write("abc", 3));
  CHECK(sink.position() == 3);
  REQUIRE(sink.write("de", 2));
  CHECK(out == "abcde");
  CHECK(sink.position() == 5);
  CHECK(sink.ok());
}
