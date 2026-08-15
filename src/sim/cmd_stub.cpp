// Temporary: compose/read land in the next commit.
#include <cstdio>
#include "sim/commands.h"
namespace rsspaper { namespace sim {
int cmd_compose(const std::vector<std::string>&) { std::fprintf(stderr, "compose: not wired up yet\n"); return 1; }
int cmd_read(const std::vector<std::string>&) { std::fprintf(stderr, "read: not wired up yet\n"); return 1; }
}}
