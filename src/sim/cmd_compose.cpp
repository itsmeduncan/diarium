// `rsspaper-sim compose` — the whole pipeline, ending in PNGs you can look at.
//
// Feeds come from the fixture corpus until the fetcher lands; that resolution
// lives in `sim/fixtures.h` so it stays out of the core.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/base/datetime.h"
#include "core/config/feeds_config.h"
#include "core/edition/edition.h"
#include "core/render/page_renderer.h"
#include "core/text/font_pack.h"
#include "sim/commands.h"
#include "sim/fixtures.h"
#include "sim/png_writer.h"

namespace rsspaper {
namespace sim {

int cmd_compose(const std::vector<std::string>& args) {
  const std::string config_path = flag(args, "--config", "config/feeds.toml");
  const std::string fonts_path = flag(args, "--fonts", "build/literata.rfp");
  const std::string out_dir = flag(args, "--out", "out");

  FixtureComposeOptions fixture_opts;
  fixture_opts.fixtures_dir = flag(args, "--fixtures", "test/fixtures/feeds");
  fixture_opts.seen_path = flag(args, "--seen", out_dir + "/seen.txt");
  fixture_opts.fresh = has_flag(args, "--fresh");
  const std::string date_flag = flag(args, "--date", "");
  if (!date_flag.empty()) {
    fixture_opts.date_override = parse_feed_date(date_flag);
  }

  if (!ensure_output_dir(out_dir)) {
    std::fprintf(stderr, "compose: cannot create output directory %s\n",
                 out_dir.c_str());
    return 1;
  }

  FeedList config;
  std::string error;
  if (!load_feeds_toml(config_path, &config, &error)) {
    std::fprintf(stderr, "compose: %s\n", error.c_str());
    return 1;
  }

  FontPack fonts;
  if (!fonts.load_file(fonts_path, &error)) {
    std::fprintf(stderr, "compose: %s\n", error.c_str());
    return 1;
  }

  FixtureComposeReport report;
  const Edition ed =
      compose_from_fixtures(config, fonts, fixture_opts, &report);

  for (const std::string& problem : report.problems) {
    std::fprintf(stderr, "compose: %s (skipping)\n", problem.c_str());
  }
  if (ed.pages.empty()) {
    std::fprintf(stderr,
                 "compose: the edition is empty — every story was filtered "
                 "out. Try --fresh to ignore the seen-store, or raise "
                 "max_age_days.\n");
    return 1;
  }

  const PageRenderer renderer(fonts);
  MastheadInfo masthead;
  masthead.title = config.edition.title;
  masthead.date_line = format_masthead_date(report.date);
  masthead.strap = std::to_string(ed.stats.items_published) + " stories · " +
                   std::to_string(report.feeds_read) +
                   " feeds · composed on device";

  const std::string depth_flag = flag(args, "--depth", "grey8");
  Depth depth = Depth::Grey8;
  if (depth_flag == "grey3") depth = Depth::Grey3;
  else if (depth_flag == "mono1") depth = Depth::Mono1;

  // By default only the pages you flip through are written: the story text
  // behind them is 150 PNGs nobody asked for.
  const bool all_pages = has_flag(args, "--all-pages");
  size_t last = all_pages ? ed.pages.size() : ed.browse_page_count;
  const size_t limit =
      static_cast<size_t>(std::atoi(flag(args, "--pages", "0").c_str()));
  if (limit > 0 && limit < last) last = limit;

  for (size_t i = 0; i < last; ++i) {
    Framebuffer fb;
    renderer.render(ed.pages[i], &fb);
    if (ed.pages[i].is_front_page) renderer.render_masthead(masthead, &fb);

    char name[64];
    std::snprintf(name, sizeof(name), "/page-%03zu.png", i + 1);
    const std::string path = out_dir + name;
    if (!write_png(fb, depth, path)) {
      std::fprintf(stderr, "compose: cannot write %s\n", path.c_str());
      return 1;
    }
  }

  std::printf("Edition of %s\n", format_masthead_date(report.date).c_str());
  std::printf(
      "  %zu pages to flip through, %zu stories, %zu pages of story text "
      "behind them\n",
      ed.browse_page_count, ed.stats.items_published,
      ed.pages.size() - ed.browse_page_count);
  std::printf("  sections: ");
  for (size_t i = 0; i < ed.section_marks.size(); ++i) {
    std::printf("%s%s p%zu", i ? ", " : "", ed.section_marks[i].name.c_str(),
                ed.section_marks[i].first_page + 1);
  }
  std::printf("\n");
  std::printf(
      "  dropped: %zu already seen, %zu stale, %zu over budget, %zu teasers "
      "off the front page\n",
      report.dropped_seen, ed.stats.dropped_stale,
      ed.stats.dropped_over_budget, ed.stats.front_page_overflow);
  std::printf(
      "  %zu of %zu published stories are truncated by their publisher\n",
      ed.stats.truncated_published, ed.stats.items_published);
  std::printf("  wrote %zu %s to %s/ (%s)\n", last,
              all_pages ? "pages" : "browse pages", out_dir.c_str(),
              depth_flag.c_str());

  if (has_flag(args, "--index")) {
    std::printf("\n  lede page  tap region        story pages  title\n");
    for (const StoryRef& s : ed.stories) {
      std::printf("  %9zu  %4d,%-4d %4dx%-4d  %4zu-%-4zu   %.44s%s\n",
                  s.lede_page + 1, s.lede_bounds.x, s.lede_bounds.y,
                  s.lede_bounds.w, s.lede_bounds.h, s.first_page + 1,
                  s.first_page + s.page_count, s.title.c_str(),
                  s.truncated ? "  [excerpt]" : "");
    }
  }
  return 0;
}

}  // namespace sim
}  // namespace rsspaper
