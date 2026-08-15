#include "core/config/opml.h"

#include <vector>

#include "core/base/str.h"
#include "core/io/file_byte_source.h"
#include "core/xml/xml_pull.h"

namespace rsspaper {
namespace {

// OPML readers disagree about which attribute holds the display name; most
// write both, some only one.
std::string outline_name(const XmlPullParser& xp) {
  const std::string* text = xp.attr("text");
  if (text != nullptr && !trim(*text).empty()) return collapse_ws(*text);
  const std::string* title = xp.attr("title");
  if (title != nullptr) return collapse_ws(*title);
  return "";
}

// A section name has to be usable as a folio and a heading.
std::string tidy_section(const std::string& raw, const std::string& fallback) {
  std::string s = collapse_ws(raw);
  if (s.size() > 40) s.resize(40);
  trim_inplace(s);
  return s.empty() ? fallback : s;
}

std::string quote_toml(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out.push_back('\\');
    if (static_cast<unsigned char>(c) < 0x20) continue;  // no control chars
    out.push_back(c);
  }
  return out + "\"";
}

}  // namespace

bool parse_opml(ByteSource& src, FeedList* out, OpmlOptions opts,
                OpmlReport* report) {
  OpmlReport local;
  XmlPullParser xp(src);

  // Folder names of the outlines currently open, and — parallel to the open
  // outline elements — whether each one pushed a folder. Self-closing feed
  // outlines emit a matching EndElement, so the two stacks stay aligned.
  std::vector<std::string> folders;
  std::vector<bool> pushed;

  bool in_head_title = false;
  const size_t before = out->feeds.size();

  for (;;) {
    const XmlEvent ev = xp.next();
    if (ev == XmlEvent::EndOfDocument) break;

    if (ev == XmlEvent::StartElement) {
      if (xp.name() == "title" && local.title.empty()) {
        in_head_title = true;
        continue;
      }
      if (xp.name() != "outline") continue;

      const std::string name = outline_name(xp);
      // Attribute lookup is case-insensitive, so this catches xmlUrl,
      // xmlurl and XMLURL — all of which occur in exported files.
      const std::string url = trim(xp.attr_or("xmlurl"));

      if (url.empty()) {
        // No feed URL: a folder, or an outline we can't use. Either way it
        // may contain feeds, so it becomes the current section.
        ++local.entries_without_url;
        if (!name.empty() && folders.size() < opts.max_folder_depth) {
          folders.push_back(name);
          pushed.push_back(true);
        } else {
          pushed.push_back(false);
        }
        continue;
      }

      pushed.push_back(false);

      bool duplicate = false;
      for (const FeedEntry& existing : out->feeds) {
        if (existing.url == url) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) {
        ++local.duplicates_skipped;
        continue;
      }

      FeedEntry feed;
      feed.url = url;
      feed.max_items = opts.max_items;
      // The *outermost* folder, not the innermost. OPML nests arbitrarily
      // deep and a reader's sub-folders are refinements, not sections; taking
      // the innermost turns "Technology > Nested deeper" into a section of
      // its own and shatters the paper into a dozen one-story sections.
      feed.section = tidy_section(folders.empty() ? "" : folders.front(),
                                  opts.default_section);
      out->feeds.push_back(std::move(feed));
      ++local.feeds_imported;
      continue;
    }

    if (ev == XmlEvent::Text && in_head_title) {
      local.title += xp.text();
      continue;
    }

    if (ev == XmlEvent::EndElement) {
      if (xp.name() == "title") {
        in_head_title = false;
        local.title = collapse_ws(local.title);
        continue;
      }
      if (xp.name() != "outline") continue;
      if (pushed.empty()) continue;  // stray close; the parser tolerates it
      const bool was_folder = pushed.back();
      pushed.pop_back();
      if (was_folder && !folders.empty()) folders.pop_back();
    }
  }

  if (!local.title.empty() && out->edition.title == "RSSpaper") {
    // Only adopt the OPML's title if the caller hasn't set one; an imported
    // "Subscriptions" is a worse masthead than the default.
    if (!iequals(local.title, "subscriptions")) {
      out->edition.title = local.title;
    }
  }

  if (report != nullptr) *report = local;
  return out->feeds.size() > before;
}

bool import_opml_file(const std::string& path, FeedList* out, OpmlOptions opts,
                      OpmlReport* report) {
  FileByteSource src(path);
  if (!src.ok()) return false;
  return parse_opml(src, out, opts, report);
}

std::string to_feeds_toml(const FeedList& list) {
  std::string out =
      "# RSSpaper edition configuration.\n"
      "#\n"
      "# Section order here is section order in the paper.\n"
      "\n[edition]\n";
  out += "title = " + quote_toml(list.edition.title) + "\n";
  out += "wake_at = " + quote_toml(list.edition.wake_at) + "\n";
  out += "max_items = " + std::to_string(list.edition.max_items) + "\n";
  out += "max_age_days = " + std::to_string(list.edition.max_age_days) + "\n";
  out += "front_page_columns = " +
         std::to_string(list.edition.front_page_columns) + "\n";
  out += std::string("body_alignment = ") +
         (list.edition.body_alignment == Align::Justify ? "\"justified\""
                                                        : "\"ragged\"") +
         "\n";

  // Grouped by section, in section order, so the file reads the way the paper
  // does rather than the way the OPML happened to be nested.
  for (const std::string& section : list.section_order()) {
    out += "\n# " + section + "\n";
    for (const FeedEntry& feed : list.feeds) {
      if (feed.section != section) continue;
      out += "\n[[feed]]\n";
      out += "url = " + quote_toml(feed.url) + "\n";
      out += "section = " + quote_toml(feed.section) + "\n";
      out += "max_items = " + std::to_string(feed.max_items) + "\n";
    }
  }
  return out;
}

}  // namespace rsspaper
