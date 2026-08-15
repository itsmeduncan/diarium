#include "sim/commands.h"

namespace rsspaper {
namespace sim {
namespace {

bool is_flag(const std::string& s) {
  return s.size() > 2 && s[0] == '-' && s[1] == '-';
}

// Flags that stand alone rather than taking a following value.
bool is_boolean_flag(const std::string& s) {
  return s == "--verbose" || s == "--help" || s == "--quiet" ||
         s == "--no-dither" || s == "--index";
}

}  // namespace

std::string flag(const std::vector<std::string>& args, const char* name,
                 const std::string& fallback) {
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == name) return args[i + 1];
  }
  return fallback;
}

bool has_flag(const std::vector<std::string>& args, const char* name) {
  for (const std::string& a : args) {
    if (a == name) return true;
  }
  return false;
}

std::vector<std::string> positionals(const std::vector<std::string>& args) {
  std::vector<std::string> out;
  for (size_t i = 0; i < args.size(); ++i) {
    if (is_flag(args[i])) {
      if (!is_boolean_flag(args[i])) ++i;  // skip the flag's value
      continue;
    }
    out.push_back(args[i]);
  }
  return out;
}

}  // namespace sim
}  // namespace rsspaper
