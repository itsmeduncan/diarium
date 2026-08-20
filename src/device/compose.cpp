#include "device/compose.h"

#include <Arduino.h>

#include <memory>
#include <vector>

#include "core/base/datetime.h"
#include "core/base/str.h"
#include "core/feed/feed_parser.h"
#include "core/html/html_to_blocks.h"
#include "core/html/readability.h"

namespace rsspaper {
namespace device {
namespace {

class Collect final : public ItemSink {
 public:
  bool on_item(Item&& item) override {
    items.push_back(std::move(item));
    return true;
  }
  std::vector<Item> items;
};

// A stable, short filename per feed. The URL is not usable as one; the names
// are flat rather than in a directory so nothing has to mkdir.
std::string cache_path_for(const std::string& url) {
  uint64_t h = 1469598103934665603ull;  // FNV-1a
  for (char c : url) {
    h ^= static_cast<uint8_t>(c);
    h *= 1099511628211ull;
  }
  return "/f" + to_hex64(h) + ".xml";
}

// Where a truncated story's own page is kept, keyed on the item rather than
// the feed: a feed's stories change, and each one's full text is its own.
std::string article_path_for(uint64_t key) {
  return "/a" + to_hex64(key) + ".htm";
}

// Streams the response onto the card, so a body far larger than RAM is fine
// and so a 304 next time still has something to parse.
bool spill_to_card(ByteSource* body, IStorage* storage, const std::string& path,
                   size_t* bytes) {
  std::string blob;
  char buf[1024];
  for (;;) {
    const size_t n = body->read(buf, sizeof(buf));
    if (n == 0) break;
    blob.append(buf, n);
  }
  if (body->failed() || blob.empty()) return false;
  *bytes = blob.size();
  return storage->write(path, blob);
}

}  // namespace

void fetch_all(const FeedList& config, IHttpClient* http, IStorage* storage,
               FeedCache* cache, DeviceClock* clock, ComposeReport* report) {
  ComposeReport local;
  const uint32_t started = millis();
  bool clock_set = false;

  for (const FeedEntry& feed : config.feeds) {
    const std::string path = cache_path_for(feed.url);

    HttpRequest req;
    req.url = feed.url;
    if (cache != nullptr) {
      const FeedValidators v = cache->get(feed.url);
      req.etag = v.etag;
      req.last_modified = v.last_modified;
    }

    HttpResponse res;
    std::unique_ptr<ByteSource> body = http->get(req, &res);

    // Any answer at all carries the day, including a 304 — and most mornings
    // most feeds answer 304, so waiting for a 200 would drift the clock on
    // exactly the days nothing changed.
    if (!clock_set && clock != nullptr && !res.server_date.empty()) {
      const Epoch when = parse_feed_date(res.server_date);
      if (when != kNoDate) {
        clock->set_now(when);
        clock_set = true;
      }
    }

    if (body != nullptr) {
      size_t bytes = 0;
      if (spill_to_card(body.get(), storage, path, &bytes)) {
        local.bytes_downloaded += bytes;
        ++local.feeds_fetched;
        if (cache != nullptr) cache->put(feed.url, {res.etag, res.last_modified});
      } else {
        ++local.feeds_failed;
        local.problems.push_back(FeedProblem{feed.url, "could not be saved"});
      }
      // With the feed in hand, pull the pages behind anything the publisher
      // cut short. Parsing here costs a handful of items, not an edition, and
      // it is the only moment the radio is up.
      std::string raw;
      if (storage->read(path, &raw)) {
        MemoryByteSource src(std::move(raw));
        Collect sink;
        FeedParseOptions parse_opts;
        parse_opts.max_items = feed.max_items;
        parse_feed(src, sink, parse_opts);
        for (const Item& it : sink.items) {
          if (!it.looks_truncated() || it.link.empty()) continue;
          const std::string apath = article_path_for(it.dedup_key());
          if (storage->exists(apath)) continue;  // already have it

          HttpRequest areq;
          areq.url = it.link;
          areq.max_bytes = 512u * 1024;
          HttpResponse ares;
          std::unique_ptr<ByteSource> abody = http->get(areq, &ares);
          if (abody == nullptr) continue;  // costs this story, nothing else
          size_t abytes = 0;
          if (spill_to_card(abody.get(), storage, apath, &abytes)) {
            local.bytes_downloaded += abytes;
            ++local.articles_fetched;
          }
        }
      }
    } else if (res.status == 304) {
      // Unchanged: the stories are still the ones already on the card.
      ++local.feeds_unchanged;
    } else {
      ++local.feeds_failed;
      local.problems.push_back(
          FeedProblem{feed.url, res.error.empty() ? "failed" : res.error});
    }
  }

  local.elapsed_ms = millis() - started;
  if (report != nullptr) *report = local;
}

Edition compose_from_card(const FeedList& config, const FontPack& fonts,
                          IStorage* storage, const SeenStore* already_read,
                          Epoch now, const ComposeReport& fetched) {
  std::vector<Section> sections;
  for (const std::string& name : config.section_order()) {
    sections.push_back(Section{name, {}});
  }

  std::vector<FeedProblem> problems = fetched.problems;
  Epoch newest = kNoDate;

  for (const FeedEntry& feed : config.feeds) {
    std::string raw;
    if (!storage->read(cache_path_for(feed.url), &raw)) {
      // Only worth reporting if the fetch did not already explain itself.
      bool known = false;
      for (const FeedProblem& p : problems) {
        if (p.source == feed.url) known = true;
      }
      if (!known) problems.push_back(FeedProblem{feed.url, "nothing cached yet"});
      continue;
    }

    MemoryByteSource src(std::move(raw));
    Collect sink;
    FeedParseOptions parse_opts;
    parse_opts.max_items = feed.max_items;
    parse_feed(src, sink, parse_opts);

    for (Item& it : sink.items) {
      it.section = feed.section;

      // A publisher that truncates gave us two paragraphs and an ellipsis. If
      // its page was fetched, the story replaces the excerpt.
      if (it.looks_truncated()) {
        std::string html;
        if (storage->read(article_path_for(it.dedup_key()), &html)) {
          BlockCollector page_blocks;
          HtmlToBlocks conv(page_blocks);
          conv.feed(html);
          conv.finish();
          std::vector<Block> article = extract_article(page_blocks.blocks);
          // Empty means the page read as all furniture; keeping the feed's
          // own excerpt is better than printing a navigation bar.
          if (!article.empty()) {
            it.blocks = std::move(article);
            it.truncation = TruncationReason::None;
          }
        }
      }

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

  Epoch when = now != kNoDate ? now : newest;
  if (when == kNoDate) when = 0;

  if (already_read != nullptr) {
    for (Section& s : sections) {
      std::vector<Item> kept;
      for (Item& it : s.items) {
        if (already_read->has(it.dedup_key())) continue;  // finished with
        kept.push_back(std::move(it));
      }
      s.items = std::move(kept);
    }
  }

  ComposeOptions opts;
  opts.now = when;
  opts.title = config.edition.title;
  opts.max_items = config.edition.max_items;
  opts.max_age_days = config.edition.max_age_days;
  opts.front_page_columns = config.edition.front_page_columns;
  opts.body_alignment = config.edition.body_alignment;
  opts.hyphenate = config.edition.hyphenate;
  opts.feeds_configured = config.feeds.size();
  opts.feed_problems = problems;

  return compose_edition(std::move(sections), fonts, opts);
}

}  // namespace device
}  // namespace rsspaper
