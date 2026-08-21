#pragma once

#include <string>
#include <vector>

namespace diarium {
namespace sim {

// Shared flag lookup: `--name value`, with a default when absent.
std::string flag(const std::vector<std::string>& args, const char* name,
                 const std::string& fallback);
bool has_flag(const std::vector<std::string>& args, const char* name);
// Everything that isn't a flag or a flag's value.
std::vector<std::string> positionals(const std::vector<std::string>& args);

int cmd_parse(const std::vector<std::string>& args);
int cmd_compose(const std::vector<std::string>& args);
int cmd_read(const std::vector<std::string>& args);
int cmd_opml(const std::vector<std::string>& args);
// The screens the device draws rather than the composer lays out.
int cmd_screens(const std::vector<std::string>& args);

}  // namespace sim
}  // namespace diarium
