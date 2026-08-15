// `rsspaper-sim compose` — the whole pipeline, ending in PNGs you can look at.
//
// There is no fetcher yet, so feeds come from the fixture corpus. The
// resolution from a configured URL to a fixture file is deliberately confined
// to this file: it is a simulator concern and must never leak into the config
// format or the core.
#include <sys/stat.h>

#include <cstdio>
#include <string>
#include <vector>

#include "core/base/datetime.h"
#include "core/base/str.h"
#include "core/config/feeds_config.h"
#include "core/edition/edition.h"
#include "core/edition/seen_store.h"
#include "core/feed/feed_parser.h"
#include "core/io/file_byte_source.h"
#include "core/render/page_renderer.h"
#include "core/text/font_pack.h"
#include "sim/commands.h"
#include "sim/png_writer.h"

namespace rsspaper {
namespace sim {
namespace {

class Collect final : public ItemSink {
 public:
  bool on_item(Item&& item) override {
    items.push_back(std::move(item));
    return true;
  }
  std::vector<Item> items;
};

std::string host_of(const std::string& url) {
  size_t at = url.find("://");
  at = (at == std::string::npos) ? 0 : at + 3;
  const size_t end = url.find('/', at);
  return url.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

// Maps a feed URL to a fixture filename. Host labels are matched against the
// fixture's stem in both directions, which covers feeds.arstechnica.com ->
// arstechnica.rss.xml and www.theguardian.com -> guardian-world.rss.xml.
// Hosts that share nothing with their fixture name get an explicit alias.
std::string fixture_for(const std::string& url,
                        const std::vector<std::string>& fixtures) {
  struct Alias {
    const char* host;
    const char* stem;
  };
  static const Alias kAliases[] = {
      {"news.ycombinator.com", "hackernews"},
      {"feeds.bbci.co.uk", "bbc-news"},
  };

  const std::string host = host_of(url);
  for (const Alias& a : kAliases) {
    if (host == a.host) {
      for (const std::string& f : fixtures) {
        if (starts_with(f, a.stem)) return f;
      }
    }
  }

  // Split the host into labels and look for one that shares a name with a
  // fixture stem.
  std::vector<std::string> labels;
  std::string cur;
  for (char c : host + ".") {
    if (c == '.') {
      if (!cur.empty()) labels.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }

  for (const std::string& f : fixtures) {
    const size_t dot = f.find('.');
    const std::string stem = dot == std::string::npos ? f : f.substr(0, dot);
    // The stem's first hyphen-separated token, so "guardian-world" -> "guardian".
    const size_t dash = stem.find('-');
    const std::string token = dash == std::string::npos ? stem : stem.substr(0, dash);
    if (token.size() < 4) continue;
    for (const std::string& label : labels) {
      if (label.size() < 4) continue;
      if (icontains(label, token.c_str()) || icontains(token, label.c_str())) {
        return f;
      }
    }
  }
  return "";
}

// POSIX mkdir rather than <filesystem>: this is simulator-only code, and the
// output directory not existing should not be the user's problem.
bool ensure_dir(const std::string& path) {
  if (path.empty()) return true;
  if (::mkdir(path.c_str(), 0755) == 0) return true;
  struct stat st;
  return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::vector<std::string> list_fixtures(const std::string& dir) {
  // No <filesystem>: the device build has no such thing, and the simulator
  // should not need more of the standard library than the device can offer.
  static const char* kKnown[] = {
      "arstechnica.rss.xml",     "astralcodexten.rss.xml",
      "bbc-news.rss.xml",        "craigmod.rss.xml",
      "daringfireball.atom.xml", "guardian-world.rss.xml",
      "hackernews.rss.xml",      "kottke.rss.xml",
      "nasa.rss.xml",            "simonwillison.atom.xml",
      "theverge.atom.xml",       "xkcd.rss.xml",
  };
  std::vector<std::string> found;
  for (const char* name : kKnown) {
    std::string probe;
    if (read_file(dir + "/" + name, &probe)) found.push_back(name);
  }
  return found;
}

}  // namespace

int cmd_compose(const std::vector<std::string>& args) {
  const std::string config_path = flag(args, "--config", "config/feeds.toml");
  const std::string fonts_path = flag(args, "--fonts", "build/literata.rfp");
  const std::string out_dir = flag(args, "--out", "out");
  const std::string fixtures_dir =
      flag(args, "--fixtures", "test/fixtures/feeds");
  const std::string seen_path = flag(args, "--seen", out_dir + "/seen.txt");
  const bool fresh = has_flag(args, "--fresh");

  if (!ensure_dir(out_dir)) {
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

  // No RTC in the simulator, and no wall-clock reads in the core: the date is
  // an input. Default to the newest story so fixtures don't all look stale.
  Epoch now = kNoDate;
  const std::string date_flag = flag(args, "--date", "");

  const std::vector<std::string> fixtures = list_fixtures(fixtures_dir);
  if (fixtures.empty()) {
    std::fprintf(stderr, "compose: no fixtures under %s\n",
                 fixtures_dir.c_str());
    return 1;
  }

  // --- fetch (from fixtures) and parse -------------------------------------
  const std::vector<std::string> order = config.section_order();
  std::vector<Section> sections;
  for (const std::string& name : order) sections.push_back(Section{name, {}});

  size_t unresolved = 0;
  for (const FeedEntry& feed : config.feeds) {
    const std::string file = fixture_for(feed.url, fixtures);
    if (file.empty()) {
      std::fprintf(stderr, "compose: no fixture for %s (skipping)\n",
                   feed.url.c_str());
      ++unresolved;
      continue;
    }
    FileByteSource src(fixtures_dir + "/" + file);
    if (!src.ok()) continue;

    Collect sink;
    FeedParseOptions opts;
    opts.max_items = feed.max_items;
    const FeedParseStats stats = parse_feed(src, sink, opts);
    (void)stats;

    for (Item& it : sink.items) {
      it.section = feed.section;
      if (it.published != kNoDate && (now == kNoDate || it.published > now)) {
        now = it.published;
      }
      for (Section& s : sections) {
        if (s.name == feed.section) {
          s.items.push_back(std::move(it));
          break;
        }
      }
    }
  }

  if (!date_flag.empty()) now = parse_feed_date(date_flag);
  if (now == kNoDate) now = 0;

  // --- dedup against previous editions -------------------------------------
  SeenStore seen;
  if (!fresh) seen.load(seen_path, now);
  size_t dropped_seen = 0;
  for (Section& s : sections) {
    std::vector<Item> kept;
    for (Item& it : s.items) {
      if (!seen.mark(it.dedup_key(), now)) {
        ++dropped_seen;
        continue;
      }
      kept.push_back(std::move(it));
    }
    s.items = std::move(kept);
  }

  // --- compose --------------------------------------------------------------
  ComposeOptions opts;
  opts.now = now;
  opts.title = config.edition.title;
  opts.max_items = config.edition.max_items;
  opts.max_age_days = config.edition.max_age_days;
  opts.front_page_columns = config.edition.front_page_columns;
  opts.body_alignment = config.edition.body_alignment;

  const Edition ed = compose_edition(std::move(sections), fonts, opts);
  if (ed.pages.empty()) {
    std::fprintf(stderr,
                 "compose: the edition is empty — every story was filtered "
                 "out. Try --fresh to ignore the seen-store, or raise "
                 "max_age_days.\n");
    return 1;
  }

  // --- render ---------------------------------------------------------------
  const PageRenderer renderer(fonts);
  MastheadInfo masthead;
  masthead.title = config.edition.title;
  masthead.date_line = format_masthead_date(now);
  masthead.strap = std::to_string(ed.stats.items_published) + " stories · " +
                   std::to_string(config.feeds.size() - unresolved) +
                   " feeds · composed on device";

  const std::string depth_flag = flag(args, "--depth", "grey8");
  Depth depth = Depth::Grey8;
  if (depth_flag == "grey3") depth = Depth::Grey3;
  else if (depth_flag == "mono1") depth = Depth::Mono1;

  const size_t limit =
      static_cast<size_t>(std::atoi(flag(args, "--pages", "0").c_str()));

  for (size_t i = 0; i < ed.pages.size(); ++i) {
    if (limit > 0 && i >= limit) break;
    Framebuffer fb;
    renderer.render(ed.pages[i], &fb);
    if (ed.pages[i].is_front_page) renderer.render_masthead(masthead, &fb);

    char name[64];
    std::snprintf(name, sizeof(name), "/page-%03zu.png", i + 1);
    const std::string path = out_dir + name;
    if (!write_png(fb, depth, path)) {
      std::fprintf(stderr,
                   "compose: cannot write %s — does the directory exist?\n",
                   path.c_str());
      return 1;
    }
  }

  if (!fresh) seen.save(seen_path);

  std::printf("Edition of %s — %zu pages, %zu stories\n",
              format_masthead_date(now).c_str(), ed.pages.size(),
              ed.stats.items_published);
  std::printf("  sections: ");
  for (size_t i = 0; i < ed.section_marks.size(); ++i) {
    std::printf("%s%s p%zu", i ? ", " : "", ed.section_marks[i].name.c_str(),
                ed.section_marks[i].first_page + 1);
  }
  std::printf("\n");
  std::printf("  dropped: %zu already seen, %zu stale, %zu over budget, "
              "%zu teasers off the front page\n",
              dropped_seen, ed.stats.dropped_stale,
              ed.stats.dropped_over_budget, ed.stats.front_page_overflow);
  std::printf("  %zu of %zu published stories are truncated by their "
              "publisher\n",
              ed.stats.truncated_published, ed.stats.items_published);
  std::printf("  wrote %s/page-*.png (%s)\n", out_dir.c_str(),
              depth_flag.c_str());
  return 0;
}

}  // namespace sim
}  // namespace rsspaper
