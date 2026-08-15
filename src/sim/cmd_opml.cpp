// `rsspaper-sim opml` — turn an exported subscription list into feeds.toml.
#include <cstdio>
#include <string>
#include <vector>

#include "core/config/opml.h"
#include "core/io/file_byte_source.h"
#include "sim/commands.h"

namespace rsspaper {
namespace sim {

int cmd_opml(const std::vector<std::string>& args) {
  const std::vector<std::string> files = positionals(args);
  if (files.empty()) {
    std::fprintf(stderr,
                 "opml: give me an .opml file\n"
                 "  rsspaper-sim opml subs.opml [--out config/feeds.toml]\n");
    return 2;
  }

  OpmlOptions opts;
  opts.default_section = flag(args, "--section", "News");
  opts.max_items =
      static_cast<size_t>(std::atoi(flag(args, "--max", "6").c_str()));

  // Several files merge into one list, deduplicated by URL.
  FeedList list;
  size_t imported = 0;
  for (const std::string& path : files) {
    OpmlReport report;
    if (!import_opml_file(path, &list, opts, &report)) {
      std::fprintf(stderr, "opml: no feeds found in %s\n", path.c_str());
      continue;
    }
    imported += report.feeds_imported;
    std::fprintf(stderr, "opml: %s — %zu feeds, %zu duplicates skipped\n",
                 path.c_str(), report.feeds_imported,
                 report.duplicates_skipped);
  }
  if (list.feeds.empty()) return 1;

  const std::string toml = to_feeds_toml(list);
  const std::string out_path = flag(args, "--out", "");
  if (out_path.empty()) {
    std::fputs(toml.c_str(), stdout);
  } else if (!write_file(out_path, toml)) {
    std::fprintf(stderr, "opml: cannot write %s\n", out_path.c_str());
    return 1;
  } else {
    std::fprintf(stderr, "opml: wrote %s\n", out_path.c_str());
  }

  std::fprintf(stderr, "opml: %zu feeds across %zu sections\n", imported,
               list.section_order().size());
  return 0;
}

}  // namespace sim
}  // namespace rsspaper
