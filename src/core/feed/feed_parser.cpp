#include "core/feed/feed_parser.h"

#include "core/base/str.h"
#include "core/xml/xml_pull.h"

namespace diarium {
namespace {

enum class Field : uint8_t {
  None,
  Title,
  Link,
  Guid,
  Author,
  AuthorName,
  Published,
  Updated,
  Summary,  // streams into the summary block builder
  Content,  // streams into the main block builder
};

bool is_item_element(const std::string& name, const std::string& prefix) {
  return prefix.empty() && (name == "item" || name == "entry");
}

// RSS author fields are mail addresses: "ed@example.com (Ed Bloggs)".
std::string tidy_author(const std::string& raw) {
  const std::string s = collapse_ws(raw);
  const size_t open = s.find('(');
  const size_t close = s.rfind(')');
  if (open != std::string::npos && close != std::string::npos && close > open) {
    const std::string inner = trim(s.substr(open + 1, close - open - 1));
    if (!inner.empty()) return inner;
  }
  if (s.find('@') != std::string::npos && s.find(' ') == std::string::npos) {
    return "";  // a bare address is noise in a byline
  }
  return s;
}

// Trims to a byte budget without splitting a UTF-8 sequence or a word.
std::string clip_text(const std::string& s, size_t max_bytes) {
  if (s.size() <= max_bytes) return s;
  size_t cut = max_bytes;
  while (cut > 0 && (static_cast<uint8_t>(s[cut]) & 0xC0) == 0x80) --cut;
  const size_t space = s.rfind(' ', cut);
  if (space != std::string::npos && space > max_bytes / 2) cut = space;
  std::string out = s.substr(0, cut);
  trim_inplace(out);
  out += "\xE2\x80\xA6";  // U+2026
  return out;
}

std::string plain_text_of(const std::vector<Block>& blocks, size_t max_bytes) {
  std::string out;
  for (const Block& b : blocks) {
    if (b.type == BlockType::Image || b.type == BlockType::Rule) continue;
    if (!out.empty()) out += " ";
    out += b.text;
    if (out.size() >= max_bytes) break;
  }
  for (char& c : out) {
    if (c == '\n') c = ' ';
  }
  return clip_text(collapse_ws(out), max_bytes);
}

// Titles are the one field where publishers reliably nest markup or
// double-encode entities — `<title type="html">` wrapping CDATA that contains
// `&#8217;` is standard practice at several large sites. Running the title
// through the HTML path strips tags and resolves the second encoding layer in
// one pass; plain titles are left alone.
std::string tidy_title(const std::string& raw) {
  const std::string s = collapse_ws(raw);
  if (s.find('<') == std::string::npos && s.find('&') == std::string::npos) {
    return s;
  }
  HtmlLimits limits;
  limits.max_blocks = 4;
  limits.max_block_bytes = 512;
  limits.max_total_bytes = 1024;
  std::string out;
  for (const Block& b : html_to_blocks(s, limits)) {
    if (b.text.empty()) continue;
    if (!out.empty()) out += " ";
    out += b.text;
  }
  for (char& c : out) {
    if (c == '\n') c = ' ';
  }
  out = collapse_ws(out);
  return out.empty() ? s : out;
}

TruncationReason detect_truncation(const Item& item, size_t short_threshold) {
  if (item.blocks.empty()) return TruncationReason::SummaryOnly;

  // A "summary" that runs to several full paragraphs is the whole article;
  // plenty of blogs simply never populate content:encoded.
  const bool summary_only = item.content_source == ContentSource::Summary;
  if (summary_only && (item.text_bytes < 1500 || item.blocks.size() < 3)) {
    return TruncationReason::SummaryOnly;
  }

  const Block* last = nullptr;
  for (size_t i = item.blocks.size(); i-- > 0;) {
    if (!item.blocks[i].text.empty()) {
      last = &item.blocks[i];
      break;
    }
  }
  if (last != nullptr) {
    const std::string& t = last->text;
    if (ends_with(t, "\xE2\x80\xA6") || ends_with(t, "...")) {
      return TruncationReason::EllipsisTail;
    }
    const std::string tail = t.size() > 120 ? t.substr(t.size() - 120) : t;
    if (icontains(tail, "read more") || icontains(tail, "continue reading") ||
        icontains(tail, "read the rest") || icontains(tail, "read on") ||
        icontains(tail, "[…]") || icontains(tail, "[...]")) {
      return TruncationReason::ReadMoreLink;
    }
  }
  if (item.text_bytes < short_threshold && !item.link.empty()) {
    return TruncationReason::VeryShort;
  }
  return TruncationReason::None;
}

// Rebuilds a start tag for the block converter. Atom content with
// type="xhtml" arrives as real child elements rather than escaped text, so we
// replay them as markup and the HTML path handles both spellings.
std::string synth_start_tag(const XmlPullParser& xp) {
  std::string tag = "<" + xp.name();
  if (const std::string* src = xp.attr("src")) tag += " src=\"" + *src + "\"";
  if (const std::string* alt = xp.attr("alt")) tag += " alt=\"" + *alt + "\"";
  return tag + ">";
}

}  // namespace

const char* feed_format_name(FeedFormat f) {
  switch (f) {
    case FeedFormat::Rss: return "rss";
    case FeedFormat::Rdf: return "rdf";
    case FeedFormat::Atom: return "atom";
    case FeedFormat::Unknown: return "unknown";
  }
  return "unknown";
}

FeedParseStats parse_feed(ByteSource& src, ItemSink& sink,
                          const FeedParseOptions& opts) {
  FeedParseStats stats;
  XmlPullParser xp(src);

  HtmlLimits summary_limits;
  summary_limits.max_blocks = 12;
  summary_limits.max_total_bytes = 6144;
  summary_limits.max_block_bytes = 2048;

  BlockCollector content_blocks;
  BlockCollector summary_blocks;
  HtmlToBlocks content_conv(content_blocks, opts.html_limits);
  HtmlToBlocks summary_conv(summary_blocks, summary_limits);

  Item item;
  bool in_item = false;
  int item_depth = 0;

  Field field = Field::None;
  int field_depth = 0;
  std::string field_buf;

  bool in_author = false;
  int author_depth = 0;
  Epoch published = kNoDate;
  Epoch updated = kNoDate;
  bool want_feed_title = true;

  auto reset_item = [&]() {
    item = Item();
    content_blocks.blocks.clear();
    summary_blocks.blocks.clear();
    content_conv.reset();
    summary_conv.reset();
    published = kNoDate;
    updated = kNoDate;
    in_author = false;
    field = Field::None;
    field_buf.clear();
  };

  auto close_field = [&]() {
    switch (field) {
      case Field::Title:
        if (!in_item) {
          if (stats.feed_title.empty()) {
            stats.feed_title = tidy_title(field_buf);
            if (!stats.feed_title.empty()) sink.on_feed_title(stats.feed_title);
          }
        } else if (item.title.empty()) {
          item.title = tidy_title(field_buf);
        }
        break;
      case Field::Link:
        if (item.link.empty()) item.link = trim(field_buf);
        break;
      case Field::Guid:
        if (item.guid.empty()) item.guid = trim(field_buf);
        break;
      case Field::Author:
      case Field::AuthorName: {
        const std::string a = tidy_author(field_buf);
        if (!a.empty() && (item.author.empty() || field == Field::AuthorName)) {
          item.author = a;
        }
        break;
      }
      case Field::Published:
        published = parse_feed_date(field_buf);
        break;
      case Field::Updated:
        updated = parse_feed_date(field_buf);
        break;
      case Field::Summary:
        summary_conv.finish();
        break;
      case Field::Content:
        content_conv.finish();
        break;
      case Field::None:
        break;
    }
    field = Field::None;
    field_buf.clear();
  };

  auto finish_item = [&]() -> bool {
    ++stats.items_seen;

    if (!content_blocks.blocks.empty()) {
      item.blocks = std::move(content_blocks.blocks);
      item.content_source = ContentSource::FullContent;
    } else if (!summary_blocks.blocks.empty()) {
      item.blocks = summary_blocks.blocks;
      item.content_source = ContentSource::Summary;
    } else {
      item.content_source = ContentSource::None;
    }

    item.text_bytes = 0;
    for (const Block& b : item.blocks) item.text_bytes += b.text.size();

    if (!summary_blocks.blocks.empty()) {
      item.summary_text = plain_text_of(summary_blocks.blocks,
                                        opts.summary_bytes);
    } else {
      item.summary_text = plain_text_of(item.blocks, opts.summary_bytes);
    }

    item.published = published != kNoDate ? published : updated;
    item.source_name = stats.feed_title;
    item.truncation = detect_truncation(item, opts.short_body_threshold);

    // A story with neither a title nor a body isn't a story.
    if (item.title.empty() && item.blocks.empty()) return true;

    ++stats.items_emitted;
    if (!sink.on_item(std::move(item))) {
      stats.stopped_early = true;
      return false;
    }
    return stats.items_emitted < opts.max_items;
  };

  for (;;) {
    const XmlEvent ev = xp.next();
    if (ev == XmlEvent::EndOfDocument) break;

    if (ev == XmlEvent::StartElement) {
      const std::string& name = xp.name();
      const std::string& prefix = xp.prefix();

      if (stats.format == FeedFormat::Unknown) {
        if (name == "rss") stats.format = FeedFormat::Rss;
        else if (name == "rdf") stats.format = FeedFormat::Rdf;
        else if (name == "feed") stats.format = FeedFormat::Atom;
      }

      // Inside a streaming content element, child elements are markup.
      if ((field == Field::Content || field == Field::Summary) &&
          xp.depth() > field_depth) {
        const std::string tag = synth_start_tag(xp);
        if (field == Field::Content) content_conv.feed(tag);
        else summary_conv.feed(tag);
        continue;
      }

      if (!in_item) {
        if (is_item_element(name, prefix)) {
          reset_item();
          in_item = true;
          item_depth = xp.depth();
          want_feed_title = false;
          continue;
        }
        if (want_feed_title && prefix.empty() && name == "title" &&
            xp.depth() <= 3) {
          field = Field::Title;
          field_depth = xp.depth();
          field_buf.clear();
        }
        continue;
      }

      // --- inside an item ---
      if (field != Field::None) continue;  // ignore nesting we don't model

      if (prefix.empty() && name == "author") {
        in_author = true;
        author_depth = xp.depth();
        field = Field::Author;
        field_depth = xp.depth();
        field_buf.clear();
        continue;
      }
      if (in_author && prefix.empty() && name == "name") {
        field = Field::AuthorName;
        field_depth = xp.depth();
        field_buf.clear();
        continue;
      }
      if (prefix == "dc" && name == "creator") {
        field = Field::Author;
        field_depth = xp.depth();
        field_buf.clear();
        continue;
      }
      if (prefix == "dc" && name == "date") {
        field = Field::Published;
        field_depth = xp.depth();
        field_buf.clear();
        continue;
      }

      // Everything else we care about lives in the default namespace, except
      // content:encoded. This is what keeps media:content and itunes:summary
      // out of the article body.
      const bool default_ns = prefix.empty();

      if (default_ns && name == "title") {
        field = Field::Title;
        field_depth = xp.depth();
        field_buf.clear();
      } else if (default_ns && name == "link") {
        const std::string* href = xp.attr("href");
        if (href != nullptr) {  // Atom
          const std::string rel = xp.attr_or("rel", "alternate");
          const std::string type = xp.attr_or("type", "text/html");
          if ((rel == "alternate" || rel.empty()) &&
              (type.empty() || icontains(type, "html")) && item.link.empty()) {
            item.link = trim(*href);
          }
        } else {  // RSS
          field = Field::Link;
          field_depth = xp.depth();
          field_buf.clear();
        }
      } else if (default_ns && (name == "guid" || name == "id")) {
        field = Field::Guid;
        field_depth = xp.depth();
        field_buf.clear();
      } else if (default_ns && (name == "pubdate" || name == "published" ||
                                name == "issued")) {
        field = Field::Published;
        field_depth = xp.depth();
        field_buf.clear();
      } else if (default_ns && (name == "updated" || name == "modified")) {
        field = Field::Updated;
        field_depth = xp.depth();
        field_buf.clear();
      } else if (default_ns && (name == "description" || name == "summary")) {
        field = Field::Summary;
        field_depth = xp.depth();
      } else if ((default_ns && name == "content") ||
                 (prefix == "content" && name == "encoded")) {
        field = Field::Content;
        field_depth = xp.depth();
      }
      continue;
    }

    if (ev == XmlEvent::Text) {
      if (field == Field::Content) {
        content_conv.feed(xp.text());
      } else if (field == Field::Summary) {
        summary_conv.feed(xp.text());
      } else if (field != Field::None) {
        if (field_buf.size() < 4096) field_buf += xp.text();
      }
      continue;
    }

    if (ev == XmlEvent::EndElement) {
      if ((field == Field::Content || field == Field::Summary) &&
          xp.depth() >= field_depth) {
        const std::string tag = "</" + xp.name() + ">";
        if (field == Field::Content) content_conv.feed(tag);
        else summary_conv.feed(tag);
        continue;
      }
      if (field != Field::None && xp.depth() < field_depth) close_field();

      if (in_author && xp.depth() < author_depth) in_author = false;

      if (in_item && is_item_element(xp.name(), xp.prefix()) &&
          xp.depth() < item_depth) {
        in_item = false;
        if (!finish_item()) {
          stats.stopped_early = true;
          break;
        }
        reset_item();
      }
      continue;
    }
  }

  // A feed whose last item element never closed still deserves its story.
  if (in_item) {
    if (field != Field::None) close_field();
    finish_item();
  }

  stats.bytes_consumed = xp.bytes_consumed();
  stats.recovered_errors = xp.saw_recoverable_error();
  return stats;
}

}  // namespace diarium
