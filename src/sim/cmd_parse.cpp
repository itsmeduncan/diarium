// `diarium-sim parse` — run the feed parser over files and report what came
// out. This is the tool for answering "does this feed survive our parser, and
// how much of the article does the publisher actually give us?"
#include <cstdio>
#include <string>
#include <vector>

#include "core/base/datetime.h"
#include "core/base/str.h"
#include "core/feed/feed_parser.h"
#include "core/io/file_byte_source.h"
#include "sim/commands.h"

namespace diarium {
namespace sim {
namespace {

class ReportSink final : public ItemSink {
 public:
  explicit ReportSink(bool verbose) : verbose_(verbose) {}

  bool on_item(Item&& item) override {
    items.push_back(std::move(item));
    return true;
  }

  void print(const FeedParseStats& stats) const {
    for (size_t i = 0; i < items.size(); ++i) {
      const Item& it = items[i];
      std::printf("  %2zu. %-58.58s\n", i + 1,
                  it.title.empty() ? "(untitled)" : it.title.c_str());
      std::printf("      %-11s %-14s %5zu chars  %2zu blocks  %s\n",
                  it.published == kNoDate ? "no date"
                                          : format_short_date(it.published).c_str(),
                  it.content_source == ContentSource::FullContent ? "full-content"
                  : it.content_source == ContentSource::Summary   ? "summary"
                                                                  : "empty",
                  it.text_bytes, it.blocks.size(),
                  truncation_reason_name(it.truncation));
      if (verbose_ && !it.summary_text.empty()) {
        std::printf("      \"%.100s\"\n", it.summary_text.c_str());
      }
    }
    (void)stats;
  }

  std::vector<Item> items;

 private:
  bool verbose_;
};

}  // namespace

int cmd_parse(const std::vector<std::string>& args) {
  const std::vector<std::string> files = positionals(args);
  if (files.empty()) {
    std::fprintf(stderr, "parse: give me at least one feed file\n");
    return 2;
  }
  const bool verbose = has_flag(args, "--verbose");
  const size_t max_items =
      static_cast<size_t>(std::atoi(flag(args, "--max", "8").c_str()));

  size_t total_items = 0, truncated = 0, failed_files = 0;
  for (const std::string& path : files) {
    FileByteSource src(path);
    if (!src.ok()) {
      std::fprintf(stderr, "parse: cannot open %s\n", path.c_str());
      ++failed_files;
      continue;
    }
    FeedParseOptions opts;
    opts.max_items = max_items;

    ReportSink sink(verbose);
    const FeedParseStats stats = parse_feed(src, sink, opts);

    std::printf("\n%s\n", path.c_str());
    std::printf("  format=%s  title=\"%s\"  items=%zu/%zu  read=%zuKB%s\n",
                feed_format_name(stats.format), stats.feed_title.c_str(),
                stats.items_emitted, stats.items_seen,
                stats.bytes_consumed / 1024,
                stats.recovered_errors ? "  [recovered errors]" : "");
    sink.print(stats);

    total_items += sink.items.size();
    for (const Item& it : sink.items) {
      if (it.looks_truncated()) ++truncated;
    }
  }

  std::printf(
      "\n%zu items across %zu feeds; %zu (%.0f%%) look truncated by the "
      "publisher\n",
      total_items, files.size() - failed_files, truncated,
      total_items > 0 ? 100.0 * static_cast<double>(truncated) /
                            static_cast<double>(total_items)
                      : 0.0);
  return failed_files == 0 ? 0 : 1;
}

}  // namespace sim
}  // namespace diarium
