// How long the reader stays resident after the last touch. This is the one
// piece of the device's power behaviour that can be tested on a laptop, which
// is exactly why it lives in src/core/ rather than src/device/.
#include "core/ui/session.h"

#include "doctest.h"

using namespace rsspaper;

TEST_CASE("session stays awake immediately after a touch") {
  Session s{SessionThresholds{}};
  s.touched(1000);
  CHECK(s.intent(1000) == SessionIntent::Stay);
  CHECK(s.intent(29000) == SessionIntent::Stay);
}

TEST_CASE("session dozes once the doze threshold passes") {
  Session s{SessionThresholds{}};
  s.touched(1000);
  CHECK(s.intent(31001) == SessionIntent::Doze);
}

TEST_CASE("session sleeps once the sleep threshold passes") {
  Session s{SessionThresholds{}};
  s.touched(1000);
  CHECK(s.intent(301001) == SessionIntent::Sleep);
}

TEST_CASE("a touch resets the idle clock") {
  Session s{SessionThresholds{}};
  s.touched(1000);
  CHECK(s.intent(31001) == SessionIntent::Doze);
  s.touched(31001);
  CHECK(s.intent(31002) == SessionIntent::Stay);
}

TEST_CASE("session millis rollover does not strand the reader asleep") {
  Session s{SessionThresholds{}};
  s.touched(0xFFFFF000u);
  // 0x0FFF ms later, having wrapped past zero. Unsigned subtraction wraps
  // with it, so the idle time is still small.
  CHECK(s.intent(0x00000FFFu) == SessionIntent::Stay);
}

TEST_CASE("session thresholds are configurable") {
  Session s{SessionThresholds{10, 20}};
  s.touched(0);
  CHECK(s.intent(9) == SessionIntent::Stay);
  CHECK(s.intent(10) == SessionIntent::Doze);
  CHECK(s.intent(20) == SessionIntent::Sleep);
}
