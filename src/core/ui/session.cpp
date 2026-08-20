#include "core/ui/session.h"

namespace rsspaper {

SessionIntent Session::intent(uint32_t now_ms) const {
  // Unsigned subtraction wraps, which is exactly what is wanted: millis()
  // rolls over every 49 days, and a reader must not be stranded asleep
  // because the clock went round.
  const uint32_t idle = now_ms - last_;
  if (idle >= t_.sleep_after_ms) return SessionIntent::Sleep;
  if (idle >= t_.doze_after_ms) return SessionIntent::Doze;
  return SessionIntent::Stay;
}

}  // namespace rsspaper
