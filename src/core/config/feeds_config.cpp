#include "core/config/feeds_config.h"

#include <cstdlib>

#include "core/base/str.h"
#include "core/io/file_byte_source.h"

namespace rsspaper {
namespace {

// Splits `key = value`, returning false for anything that isn't an assignment.
bool split_assignment(const std::string& line, std::string* key,
                      std::string* value) {
  const size_t eq = line.find('=');
  if (eq == std::string::npos) return false;
  *key = trim(line.substr(0, eq));
  *value = trim(line.substr(eq + 1));
  ascii_lower_inplace(*key);
  return !key->empty() && !value->empty();
}

// Unquotes a basic string; leaves bare values alone. Trailing comments after
// a value are stripped, which TOML allows and people write.
//
// Escapes are honoured inside double quotes, because the writer emits them:
// a section named `Odd "Section"` or a URL with a quote in its query string
// round-trips only if both halves agree. Literal (single-quoted) strings take
// no escapes, per TOML.
std::string unquote(const std::string& raw) {
  std::string v = raw;

  if (v.size() >= 2 && v.front() == '\'' && v.back() == '\'') {
    return v.substr(1, v.size() - 2);
  }

  if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
    const std::string body = v.substr(1, v.size() - 2);
    std::string out;
    out.reserve(body.size());
    for (size_t i = 0; i < body.size(); ++i) {
      if (body[i] != '\\' || i + 1 >= body.size()) {
        out.push_back(body[i]);
        continue;
      }
      const char esc = body[++i];
      switch (esc) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        // An escape we don't implement is passed through intact rather than
        // silently eaten, so nothing is lost that a later version could read.
        default:
          out.push_back('\\');
          out.push_back(esc);
          break;
      }
    }
    return out;
  }

  const size_t hash = v.find('#');
  if (hash != std::string::npos) v = trim(v.substr(0, hash));
  return v;
}

bool parse_size(const std::string& raw, size_t* out) {
  const std::string v = unquote(raw);
  if (v.empty()) return false;
  for (char c : v) {
    if (c < '0' || c > '9') return false;
  }
  *out = static_cast<size_t>(std::strtoull(v.c_str(), nullptr, 10));
  return true;
}

// Signed, because half the world is west of Greenwich.
bool parse_offset_minutes(const std::string& raw, int* out) {
  const std::string v = unquote(raw);
  if (v.empty()) return false;
  const bool negative = v[0] == '-';
  const std::string digits = negative ? v.substr(1) : v;
  size_t n = 0;
  if (!parse_size(digits, &n)) return false;
  if (n > 24 * 60) return false;
  *out = negative ? -static_cast<int>(n) : static_cast<int>(n);
  return true;
}

}  // namespace

std::vector<std::string> FeedList::section_order() const {
  std::vector<std::string> order;
  for (const FeedEntry& f : feeds) {
    bool seen = false;
    for (const std::string& s : order) {
      if (s == f.section) {
        seen = true;
        break;
      }
    }
    if (!seen) order.push_back(f.section);
  }
  return order;
}

bool parse_feeds_toml(const std::string& text, FeedList* out,
                      std::string* error) {
  *out = FeedList();

  enum class Table { None, Edition, Feed };
  Table table = Table::None;

  size_t start = 0;
  int line_no = 0;
  auto fail = [&](const std::string& why) {
    if (error != nullptr) {
      *error = "feeds.toml line " + std::to_string(line_no) + ": " + why;
    }
    return false;
  };

  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string line = trim(text.substr(start, end - start));
    start = end + 1;
    ++line_no;

    if (line.empty() || line[0] == '#') {
      if (start > text.size()) break;
      continue;
    }

    if (line[0] == '[') {
      std::string name = line;
      // Strip trailing comment before matching the header.
      const size_t hash = name.find('#');
      if (hash != std::string::npos) name = trim(name.substr(0, hash));

      if (name == "[[feed]]") {
        out->feeds.push_back(FeedEntry());
        table = Table::Feed;
      } else if (name == "[edition]") {
        table = Table::Edition;
      } else {
        // An unknown table is skipped rather than fatal: a config written for
        // a newer build should still boot on an older one.
        table = Table::None;
      }
      if (start > text.size()) break;
      continue;
    }

    std::string key, raw;
    if (!split_assignment(line, &key, &raw)) return fail("expected key = value");
    const std::string value = unquote(raw);

    if (table == Table::Edition) {
      if (key == "title") {
        out->edition.title = value;
      } else if (key == "wake_at") {
        out->edition.wake_at = value;
      } else if (key == "max_items") {
        if (!parse_size(raw, &out->edition.max_items)) {
          return fail("max_items must be a whole number");
        }
      } else if (key == "utc_offset_minutes") {
        if (!parse_offset_minutes(raw, &out->edition.utc_offset_minutes)) {
          return fail("utc_offset_minutes must be whole minutes east of UTC");
        }
      } else if (key == "max_age_days") {
        size_t n = 0;
        if (!parse_size(raw, &n)) return fail("max_age_days must be a whole number");
        out->edition.max_age_days = static_cast<int>(n);
      } else if (key == "front_page_columns") {
        size_t n = 0;
        if (!parse_size(raw, &n) || n < 1 || n > 4) {
          return fail("front_page_columns must be between 1 and 4");
        }
        out->edition.front_page_columns = static_cast<int>(n);
      } else if (key == "hyphenate") {
        out->edition.hyphenate = !(value == "false" || value == "no" ||
                                   value == "0" || value == "off");
      } else if (key == "body_alignment") {
        if (value == "justified" || value == "justify") {
          out->edition.body_alignment = Align::Justify;
        } else if (value == "ragged" || value == "left") {
          out->edition.body_alignment = Align::Left;
        } else {
          return fail("body_alignment must be \"justified\" or \"ragged\"");
        }
      }
      // Unknown keys are ignored, same reasoning as unknown tables.
      continue;
    }

    if (table == Table::Feed) {
      if (out->feeds.empty()) return fail("feed key outside any [[feed]] block");
      FeedEntry& f = out->feeds.back();
      if (key == "url") {
        f.url = value;
      } else if (key == "section") {
        f.section = value;
      } else if (key == "max_items") {
        if (!parse_size(raw, &f.max_items)) {
          return fail("max_items must be a whole number");
        }
      }
      continue;
    }

    if (start > text.size()) break;
  }

  for (size_t i = 0; i < out->feeds.size(); ++i) {
    if (out->feeds[i].url.empty()) {
      if (error != nullptr) {
        *error = "feeds.toml: [[feed]] #" + std::to_string(i + 1) +
                 " has no url";
      }
      return false;
    }
  }
  if (out->feeds.empty()) {
    if (error != nullptr) {
      *error = "feeds.toml has no [[feed]] entries — nothing to compose";
    }
    return false;
  }
  return true;
}

bool load_feeds_toml(const std::string& path, FeedList* out,
                     std::string* error) {
  std::string text;
  if (!read_file(path, &text)) {
    if (error != nullptr) *error = "cannot read " + path;
    return false;
  }
  return parse_feeds_toml(text, out, error);
}

}  // namespace rsspaper
