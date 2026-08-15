// `rsspaper-sim read` — interactive reader over a composed edition. Lands with
// the gesture/HAL work; compose is the milestone that had to come first.
#include <cstdio>

#include "sim/commands.h"

namespace rsspaper {
namespace sim {

int cmd_read(const std::vector<std::string>&) {
  std::fprintf(stderr,
               "read: not implemented yet — the reader UI lands with the HAL "
               "and gesture work. Use `compose` to render pages.\n");
  return 1;
}

}  // namespace sim
}  // namespace rsspaper
