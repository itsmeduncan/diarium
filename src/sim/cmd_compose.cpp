// `diarium-sim compose` — the whole pipeline, ending in PNGs you can look at.
//
// Feeds come from the fixture corpus until the fetcher lands; that resolution
// lives in `sim/fixtures.h` so it stays out of the core.
//
// Composing streams straight to the card (v5) the same way the device will,
// rather than building a resident Edition and serialising it afterwards —
// this is the desktop's exercise of that path. `--save -` skips saving
// entirely, and since streaming has nowhere to stream *to* in that case,
// that one mode falls back to the whole-Edition v4 path instead.
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "core/base/datetime.h"
#include "core/config/feeds_config.h"
#include "core/edition/edition.h"
#include "core/edition/edition_stream.h"
#include "core/io/file_byte_source.h"
#include "core/render/page_renderer.h"
#include "core/text/font_pack.h"
#include "sim/commands.h"
#include "sim/fixtures.h"
#include "sim/png_writer.h"
#include "sim/sim_storage.h"

namespace diarium {
namespace sim {
namespace {

// Renders up to `last` pages, in edition order, from a lazily-opened v5
// reader — one story's pages resident at a time, never the whole paper.
bool render_pages(const StreamingEditionReader& reader, size_t last,
                  const PageRenderer& renderer, Depth depth,
                  const std::string& out_dir, size_t* rendered) {
  size_t done = 0;
  for (size_t si = 0; si < reader.index().size() && done < last; ++si) {
    const std::vector<Page> pages = reader.load_story_pages(si);
    for (size_t p = 0; p < pages.size() && done < last; ++p, ++done) {
      Framebuffer fb;
      renderer.render(pages[p], &fb);
      char name[64];
      std::snprintf(name, sizeof(name), "/page-%03zu.png", done + 1);
      const std::string path = out_dir + name;
      if (!write_png(fb, depth, path)) {
        std::fprintf(stderr, "compose: cannot write %s\n", path.c_str());
        return false;
      }
    }
  }
  *rendered = done;
  return true;
}

}  // namespace

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

  // Before anything is laid out: the page geometry every frame is measured
  // against is chosen here, and the composed edition bakes it in.
  set_orientation(config.edition.orientation);

  FontPack fonts;
  if (!fonts.load_file(fonts_path, &error)) {
    std::fprintf(stderr, "compose: %s\n", error.c_str());
    return 1;
  }

  const PageRenderer renderer(fonts);
  const std::string depth_flag = flag(args, "--depth", "grey8");
  Depth depth = Depth::Grey8;
  if (depth_flag == "grey3") depth = Depth::Grey3;
  else if (depth_flag == "mono1") depth = Depth::Mono1;

  const size_t limit =
      static_cast<size_t>(std::atoi(flag(args, "--pages", "0").c_str()));
  const std::string save_path = flag(args, "--save", out_dir + "/edition.rspe");
  const bool save = save_path != "-";

  FixtureComposeReport report;
  ComposeStats stats;
  size_t total_pages = 0;
  size_t rendered = 0;

  if (save) {
    // save_path is a host path, not a card path: root SimStorage at its
    // directory and write the basename, so full() reconstructs save_path
    // exactly. Rooting at "." instead would prepend "./" and mangle an
    // absolute --out (e.g. CI's $RUNNER_TEMP) into a bogus cwd-relative path.
    const std::string::size_type slash = save_path.find_last_of('/');
    const std::string save_dir =
        slash == std::string::npos ? "." : save_path.substr(0, slash);
    const std::string save_name =
        slash == std::string::npos ? save_path : save_path.substr(slash + 1);
    SimStorage storage(save_dir);
    std::unique_ptr<ByteSink> sink = storage.open_write(save_name);
    if (sink == nullptr) {
      std::fprintf(stderr, "compose: cannot open %s for writing\n",
                   save_path.c_str());
      return 1;
    }
    const bool composed = compose_streaming_from_fixtures(
        config, fonts, fixture_opts, *sink, &report, &stats);
    sink.reset();  // closes the file; nothing after this can still be writing
    if (!composed) {
      std::fprintf(stderr, "compose: could not write %s\n", save_path.c_str());
      return 1;
    }

    for (const std::string& problem : report.problems) {
      std::fprintf(stderr, "compose: %s (skipping)\n", problem.c_str());
    }
    if (stats.items_published == 0) {
      std::fprintf(stderr,
                   "compose: no stories made the edition — try --fresh to "
                   "ignore the seen-store, or raise max_age_days.\n");
    }

    StreamingEditionReader reader;
    std::string blob;
    if (!read_file(save_path, &blob) || !reader.open(blob, &error)) {
      std::fprintf(stderr, "compose: cannot read back %s (%s)\n",
                   save_path.c_str(), error.c_str());
      return 1;
    }
    for (const StreamIndexEntry& e : reader.index()) total_pages += e.ref.page_count;

    size_t last = total_pages;
    if (limit > 0 && limit < last) last = limit;
    if (!render_pages(reader, last, renderer, depth, out_dir, &rendered)) return 1;

    std::printf("Edition of %s\n", format_masthead_date(report.date).c_str());
    std::printf("  %zu stories, %zu pages of story text\n",
                stats.items_published, total_pages);
    std::printf(
        "  dropped: %zu already seen, %zu stale, %zu over budget\n",
        report.dropped_seen, stats.dropped_stale, stats.dropped_over_budget);
    std::printf(
        "  %zu of %zu published stories are truncated by their publisher\n",
        stats.truncated_published, stats.items_published);
    std::printf("  wrote %zu pages to %s/ (%s)\n", rendered, out_dir.c_str(),
                depth_flag.c_str());
    std::printf("  saved the edition to %s (streamed, one story at a time)\n",
                save_path.c_str());

    if (has_flag(args, "--index")) {
      std::printf("\n  story pages  title\n");
      size_t running = 0;
      for (const StreamIndexEntry& e : reader.index()) {
        std::printf("  %4zu-%-4zu   %.44s%s\n", running + 1,
                    running + e.ref.page_count, e.ref.title.c_str(),
                    e.ref.truncated ? "  [excerpt]" : "");
        running += e.ref.page_count;
      }
    }
    return 0;
  }

  // --save -: nothing to stream to, so fall back to the whole-Edition path
  // purely to render a preview and print the report.
  const Edition ed = compose_from_fixtures(config, fonts, fixture_opts, &report);
  for (const std::string& problem : report.problems) {
    std::fprintf(stderr, "compose: %s (skipping)\n", problem.c_str());
  }
  if (ed.stats.items_published == 0) {
    std::fprintf(stderr,
                 "compose: no stories made the edition — try --fresh to ignore "
                 "the seen-store, or raise max_age_days.\n");
  }

  size_t last = ed.pages.size();
  if (limit > 0 && limit < last) last = limit;
  for (size_t i = 0; i < last; ++i) {
    Framebuffer fb;
    renderer.render(ed.pages[i], &fb);
    char name[64];
    std::snprintf(name, sizeof(name), "/page-%03zu.png", i + 1);
    const std::string path = out_dir + name;
    if (!write_png(fb, depth, path)) {
      std::fprintf(stderr, "compose: cannot write %s\n", path.c_str());
      return 1;
    }
  }

  std::printf("Edition of %s\n", format_masthead_date(report.date).c_str());
  std::printf("  %zu stories, %zu pages of story text\n",
              ed.stats.items_published, ed.pages.size());
  std::printf(
      "  dropped: %zu already seen, %zu stale, %zu over budget\n",
      report.dropped_seen, ed.stats.dropped_stale,
      ed.stats.dropped_over_budget);
  std::printf(
      "  %zu of %zu published stories are truncated by their publisher\n",
      ed.stats.truncated_published, ed.stats.items_published);
  std::printf("  wrote %zu pages to %s/ (%s)\n", last, out_dir.c_str(),
              depth_flag.c_str());
  std::printf("  --save - given: not saved\n");

  if (has_flag(args, "--index")) {
    std::printf("\n  story pages  title\n");
    for (const StoryRef& s : ed.stories) {
      std::printf("  %4zu-%-4zu   %.44s%s\n", s.first_page + 1,
                  s.first_page + s.page_count, s.title.c_str(),
                  s.truncated ? "  [excerpt]" : "");
    }
  }
  return 0;
}

}  // namespace sim
}  // namespace diarium
