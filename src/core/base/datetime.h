// Date handling for feeds. Feeds in the wild use RFC 822 (RSS), RFC 3339
// (Atom), and a long tail of near-misses; `parse_feed_date` tries all of them
// and reports failure rather than guessing a wrong instant.
#pragma once

#include <cstdint>
#include <string>

namespace rsspaper {

// Seconds since the Unix epoch, UTC. `kNoDate` means "the feed didn't say".
using Epoch = int64_t;
constexpr Epoch kNoDate = INT64_MIN;

// Accepts RFC 822/1123 ("Tue, 12 Aug 2025 09:31:00 -0400", "GMT", "EST", …)
// and RFC 3339/ISO 8601 ("2025-08-12T09:31:00Z", "+01:00", fractional seconds).
// Returns kNoDate if nothing parses.
Epoch parse_feed_date(const std::string& s);

struct CivilTime {
  int year = 1970;
  int month = 1;  // 1-12
  int day = 1;    // 1-31
  int hour = 0;
  int minute = 0;
  int second = 0;
  int weekday = 4;  // 0 = Sunday; 1970-01-01 was a Thursday
};

CivilTime civil_from_epoch(Epoch t);
Epoch epoch_from_civil(const CivilTime& c);

// "Friday, 15 August 2026" — the masthead date line.
std::string format_masthead_date(Epoch t);
// "15 Aug 2026" — compact, for bylines and section indexes.
std::string format_short_date(Epoch t);
// "09:31" — 24h, used in the status furniture.
std::string format_clock(Epoch t);

}  // namespace rsspaper
