#include "sim/fixtures.h"

#include <sys/stat.h>

#include <cstdio>

#include "core/base/str.h"
#include "core/edition/seen_store.h"
#include "core/feed/feed_parser.h"
#include "core/io/file_byte_source.h"

namespace diarium {
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

}  // namespace

bool ensure_output_dir(const std::string& path) {
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


namespace {

// Everything compose_from_fixtures and compose_streaming_from_fixtures share:
// resolving each configured feed to a fixture, parsing it, and sorting items
// into sections — all of it item metadata, none of it needing a FontPack or
// caring whether the result gets laid out into a resident Edition or
// streamed to a sink one story at a time. `report` must not be null.
std::vector<Section> assemble_fixture_sections(const FeedList& config,
                                               const FixtureComposeOptions& opts,
                                               ComposeOptions* compose_opts,
                                               FixtureComposeReport* report) {
  const std::vector<std::string> fixtures = list_fixtures(opts.fixtures_dir);
  const std::vector<std::string> order = config.section_order();
  std::vector<Section> sections;
  for (const std::string& name : order) sections.push_back(Section{name, {}});

  Epoch newest = kNoDate;
  for (const FeedEntry& feed : config.feeds) {
    const std::string file = fixture_for(feed.url, fixtures);
    if (file.empty()) {
      ++report->unresolved_feeds;
      report->problems.push_back("no fixture for " + feed.url);
      report->feed_problems.push_back(FeedProblem{feed.url, "no fixture"});
      continue;
    }
    FileByteSource src(opts.fixtures_dir + "/" + file);
    if (!src.ok()) {
      ++report->unresolved_feeds;
      report->problems.push_back("cannot open " + file);
      report->feed_problems.push_back(FeedProblem{feed.url, "could not be read"});
      continue;
    }
    ++report->feeds_read;

    Collect sink;
    FeedParseOptions parse_opts;
    parse_opts.max_items = feed.max_items;
    parse_feed(src, sink, parse_opts);

    for (Item& it : sink.items) {
      it.section = feed.section;
      if (it.published != kNoDate && (newest == kNoDate || it.published > newest)) {
        newest = it.published;
      }
      for (Section& s : sections) {
        if (s.name == feed.section) {
          s.items.push_back(std::move(it));
          break;
        }
      }
    }
  }

  Epoch now = opts.date_override != kNoDate ? opts.date_override : newest;
  if (now == kNoDate) now = 0;
  report->date = now;

  if (!opts.fresh && !opts.seen_path.empty()) {
    SeenStore seen;
    seen.load(opts.seen_path, now);
    for (Section& s : sections) {
      std::vector<Item> kept;
      for (Item& it : s.items) {
        if (!seen.mark(it.dedup_key(), now)) {
          ++report->dropped_seen;
          continue;
        }
        kept.push_back(std::move(it));
      }
      s.items = std::move(kept);
    }
    seen.save(opts.seen_path);
  }

  compose_opts->now = now;
  compose_opts->title = config.edition.title;
  compose_opts->max_items = config.edition.max_items;
  compose_opts->max_age_days = config.edition.max_age_days;
  compose_opts->body_alignment = config.edition.body_alignment;
  compose_opts->hyphenate = config.edition.hyphenate;
  compose_opts->feeds_configured = config.feeds.size();
  compose_opts->feed_problems = report->feed_problems;

  return sections;
}

}  // namespace

Edition compose_from_fixtures(const FeedList& config, const FontPack& fonts,
                              const FixtureComposeOptions& opts,
                              FixtureComposeReport* report) {
  FixtureComposeReport local;
  ComposeOptions compose_opts;
  std::vector<Section> sections =
      assemble_fixture_sections(config, opts, &compose_opts, &local);

  Edition ed = compose_edition(std::move(sections), fonts, compose_opts);
  if (report != nullptr) *report = local;
  return ed;
}

bool compose_streaming_from_fixtures(const FeedList& config,
                                     const FontPack& fonts,
                                     const FixtureComposeOptions& opts,
                                     ByteSink& sink,
                                     FixtureComposeReport* report,
                                     ComposeStats* stats) {
  FixtureComposeReport local;
  ComposeOptions compose_opts;
  std::vector<Section> sections =
      assemble_fixture_sections(config, opts, &compose_opts, &local);

  const bool ok =
      compose_streaming(std::move(sections), fonts, compose_opts, sink, stats);
  if (report != nullptr) *report = local;
  return ok;
}

}  // namespace sim
}  // namespace diarium
