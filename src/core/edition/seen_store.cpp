#include "core/edition/seen_store.h"

#include <cstdio>
#include <cstdlib>

#include "core/base/str.h"
#include "core/io/file_byte_source.h"

namespace rsspaper {

bool SeenStore::load(const std::string& path, Epoch now) {
  entries_.clear();
  std::string raw;
  if (!read_file(path, &raw)) return false;  // no store yet is not an error

  const Epoch cutoff =
      now == kNoDate ? kNoDate
                     : now - static_cast<Epoch>(retain_days_) * 86400;

  // One "hexkey epoch" per line. A text format so it can be inspected on the
  // SD card without a tool.
  size_t start = 0;
  while (start < raw.size()) {
    size_t end = raw.find('\n', start);
    if (end == std::string::npos) end = raw.size();
    const std::string line = trim(raw.substr(start, end - start));
    start = end + 1;
    if (line.empty() || line[0] == '#') continue;

    const size_t sp = line.find(' ');
    if (sp == std::string::npos) continue;
    const uint64_t key =
        std::strtoull(line.substr(0, sp).c_str(), nullptr, 16);
    const Epoch when =
        static_cast<Epoch>(std::strtoll(line.substr(sp + 1).c_str(), nullptr, 10));
    if (key == 0) continue;
    if (cutoff != kNoDate && when < cutoff) continue;  // expired
    entries_.push_back(Entry{key, when});
  }
  return true;
}

bool SeenStore::save(const std::string& path) const {
  std::string out =
      "# rsspaper seen-store: dedup key (hex) and first-seen epoch\n";
  for (const Entry& e : entries_) {
    out += to_hex64(e.key) + " " + std::to_string(e.when) + "\n";
  }
  return write_file(path, out);
}

bool SeenStore::has(uint64_t key) const {
  for (const Entry& e : entries_) {
    if (e.key == key) return true;
  }
  return false;
}

bool SeenStore::mark(uint64_t key, Epoch when) {
  if (has(key)) return false;
  entries_.push_back(Entry{key, when});
  return true;
}

}  // namespace rsspaper
