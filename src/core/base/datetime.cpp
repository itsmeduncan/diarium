#include "core/base/datetime.h"

#include <cctype>
#include <cstdlib>

#include "core/base/str.h"

namespace rsspaper {
namespace {

const char* const kMonthAbbr[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kMonthFull[12] = {
    "January", "February", "March",     "April",   "May",      "June",
    "July",    "August",   "September", "October", "November", "December"};
const char* const kDayFull[7] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                                 "Thursday", "Friday", "Saturday"};

int month_from_abbr(const std::string& s) {
  for (int i = 0; i < 12; ++i) {
    if (s.size() >= 3 && ascii_lower(s[0]) == ascii_lower(kMonthAbbr[i][0]) &&
        ascii_lower(s[1]) == ascii_lower(kMonthAbbr[i][1]) &&
        ascii_lower(s[2]) == ascii_lower(kMonthAbbr[i][2])) {
      return i + 1;
    }
  }
  return 0;
}

// Named zones still show up in RSS despite being deprecated since 1982.
bool named_zone_offset(const std::string& z, int* out_minutes) {
  struct Zone {
    const char* name;
    int minutes;
  };
  static const Zone kZones[] = {
      {"UT", 0},     {"UTC", 0},    {"GMT", 0},    {"Z", 0},
      {"EST", -300}, {"EDT", -240}, {"CST", -360}, {"CDT", -300},
      {"MST", -420}, {"MDT", -360}, {"PST", -480}, {"PDT", -420},
  };
  for (const Zone& zone : kZones) {
    if (iequals(z, zone.name)) {
      *out_minutes = zone.minutes;
      return true;
    }
  }
  return false;
}

// Days from 1970-01-01 to y-m-d. Howard Hinnant's civil_from_days, inverted.
int64_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;                               // [0, 399]
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;       // [0, 146096]
  return era * 146097 + doe - 719468;
}

void civil_from_days(int64_t z, int* y, unsigned* m, unsigned* d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe =
      (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t yy = yoe + era * 400;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  const int64_t dd = doy - (153 * mp + 2) / 5 + 1;
  const int64_t mm = mp + (mp < 10 ? 3 : -9);
  *y = static_cast<int>(yy + (mm <= 2));
  *m = static_cast<unsigned>(mm);
  *d = static_cast<unsigned>(dd);
}

// Splits on whitespace, commas and the RFC 3339 separators, so one tokenizer
// serves both formats.
void tokenize(const std::string& s, std::string* out, size_t max, size_t* n) {
  *n = 0;
  std::string cur;
  for (size_t i = 0; i <= s.size(); ++i) {
    const char c = i < s.size() ? s[i] : ' ';
    if (is_space(c) || c == ',') {
      if (!cur.empty() && *n < max) out[(*n)++] = cur;
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
}

bool parse_hms(const std::string& t, int* h, int* mi, int* sec) {
  *h = *mi = *sec = 0;
  int part = 0, value = 0, digits = 0;
  for (size_t i = 0; i <= t.size(); ++i) {
    const char c = i < t.size() ? t[i] : ':';
    if (c >= '0' && c <= '9') {
      value = value * 10 + (c - '0');
      ++digits;
    } else if (c == ':') {
      if (digits == 0) return false;
      if (part == 0) *h = value;
      else if (part == 1) *mi = value;
      else if (part == 2) *sec = value;
      ++part;
      value = 0;
      digits = 0;
      if (part > 2) break;
    } else {
      return false;
    }
  }
  return part >= 2;
}

Epoch parse_rfc3339(const std::string& s) {
  // YYYY-MM-DD[T ]HH:MM[:SS[.fff]][Z|±HH:MM]
  if (s.size() < 10) return kNoDate;
  for (int i = 0; i < 10; ++i) {
    const bool want_digit = (i != 4 && i != 7);
    if (want_digit && !std::isdigit(static_cast<unsigned char>(s[i])))
      return kNoDate;
    if (!want_digit && s[i] != '-') return kNoDate;
  }
  const int year = std::atoi(s.substr(0, 4).c_str());
  const int month = std::atoi(s.substr(5, 2).c_str());
  const int day = std::atoi(s.substr(8, 2).c_str());
  if (month < 1 || month > 12 || day < 1 || day > 31) return kNoDate;

  int hour = 0, minute = 0, second = 0, off_min = 0;
  if (s.size() > 11) {
    const std::string rest = s.substr(11);
    size_t zpos = rest.size();
    for (size_t i = 0; i < rest.size(); ++i) {
      const char c = rest[i];
      if (c == 'Z' || c == 'z' || c == '+' ||
          (c == '-' && i >= 5)) {  // '-' only after HH:MM
        zpos = i;
        break;
      }
    }
    std::string clock = rest.substr(0, zpos);
    const size_t dot = clock.find('.');
    if (dot != std::string::npos) clock = clock.substr(0, dot);
    if (!clock.empty()) {
      int hh = 0, mm = 0, ss = 0;
      if (parse_hms(clock, &hh, &mm, &ss)) {
        hour = hh;
        minute = mm;
        second = ss;
      }
    }
    if (zpos < rest.size() && (rest[zpos] == '+' || rest[zpos] == '-')) {
      const int sign = rest[zpos] == '-' ? -1 : 1;
      const std::string off = rest.substr(zpos + 1);
      int oh = 0, om = 0;
      if (off.size() >= 4 && off[2] == ':') {
        oh = std::atoi(off.substr(0, 2).c_str());
        om = std::atoi(off.substr(3, 2).c_str());
      } else if (off.size() >= 4) {
        oh = std::atoi(off.substr(0, 2).c_str());
        om = std::atoi(off.substr(2, 2).c_str());
      } else if (off.size() >= 2) {
        oh = std::atoi(off.substr(0, 2).c_str());
      }
      off_min = sign * (oh * 60 + om);
    }
  }
  const int64_t days = days_from_civil(year, month, day);
  return days * 86400 + hour * 3600 + minute * 60 + second - off_min * 60;
}

Epoch parse_rfc822(const std::string& s) {
  // [Day,] DD Mon YYYY HH:MM[:SS] ZONE
  std::string tok[8];
  size_t n = 0;
  tokenize(s, tok, 8, &n);
  size_t i = 0;
  if (n > 0 && month_from_abbr(tok[0]) == 0 &&
      !std::isdigit(static_cast<unsigned char>(tok[0][0]))) {
    ++i;  // leading weekday name
  }
  if (i + 3 >= n) return kNoDate;

  int day = 0, month = 0, year = 0;
  if (std::isdigit(static_cast<unsigned char>(tok[i][0]))) {
    day = std::atoi(tok[i].c_str());
    month = month_from_abbr(tok[i + 1]);
    year = std::atoi(tok[i + 2].c_str());
  } else {  // "Mon DD YYYY" ordering seen in a few generators
    month = month_from_abbr(tok[i]);
    day = std::atoi(tok[i + 1].c_str());
    year = std::atoi(tok[i + 2].c_str());
  }
  if (month == 0 || day < 1 || day > 31) return kNoDate;
  if (year < 100) year += year < 70 ? 2000 : 1900;
  i += 3;

  int hour = 0, minute = 0, second = 0;
  if (i < n && parse_hms(tok[i], &hour, &minute, &second)) ++i;

  int off_min = 0;
  if (i < n) {
    const std::string& z = tok[i];
    if (z.size() >= 3 && (z[0] == '+' || z[0] == '-')) {
      const int sign = z[0] == '-' ? -1 : 1;
      const std::string digits = z.substr(1);
      if (digits.size() >= 4) {
        off_min = sign * (std::atoi(digits.substr(0, 2).c_str()) * 60 +
                          std::atoi(digits.substr(2, 2).c_str()));
      }
    } else {
      named_zone_offset(z, &off_min);
    }
  }
  const int64_t days = days_from_civil(year, month, day);
  return days * 86400 + hour * 3600 + minute * 60 + second - off_min * 60;
}

}  // namespace

Epoch parse_feed_date(const std::string& raw) {
  const std::string s = trim(raw);
  if (s.empty()) return kNoDate;
  // A leading 4-digit year with a dash is unambiguous RFC 3339.
  if (s.size() >= 5 && std::isdigit(static_cast<unsigned char>(s[0])) &&
      std::isdigit(static_cast<unsigned char>(s[3])) && s[4] == '-') {
    const Epoch t = parse_rfc3339(s);
    if (t != kNoDate) return t;
  }
  const Epoch t = parse_rfc822(s);
  if (t != kNoDate) return t;
  return parse_rfc3339(s);
}

CivilTime civil_from_epoch(Epoch t) {
  CivilTime c;
  int64_t days = t / 86400;
  int64_t secs = t % 86400;
  if (secs < 0) {
    secs += 86400;
    --days;
  }
  int y;
  unsigned m, d;
  civil_from_days(days, &y, &m, &d);
  c.year = y;
  c.month = static_cast<int>(m);
  c.day = static_cast<int>(d);
  c.hour = static_cast<int>(secs / 3600);
  c.minute = static_cast<int>((secs / 60) % 60);
  c.second = static_cast<int>(secs % 60);
  c.weekday = static_cast<int>(((days % 7) + 11) % 7);  // 1970-01-01 = Thu
  return c;
}

Epoch epoch_from_civil(const CivilTime& c) {
  return days_from_civil(c.year, c.month, c.day) * 86400 + c.hour * 3600 +
         c.minute * 60 + c.second;
}

namespace {
std::string pad2(int v) {
  std::string s = std::to_string(v);
  return s.size() < 2 ? "0" + s : s;
}
}  // namespace

std::string format_masthead_date(Epoch t) {
  if (t == kNoDate) return "";
  const CivilTime c = civil_from_epoch(t);
  return std::string(kDayFull[c.weekday % 7]) + ", " + std::to_string(c.day) +
         " " + kMonthFull[(c.month - 1) % 12] + " " + std::to_string(c.year);
}

std::string format_short_date(Epoch t) {
  if (t == kNoDate) return "";
  const CivilTime c = civil_from_epoch(t);
  return std::to_string(c.day) + " " + kMonthAbbr[(c.month - 1) % 12] + " " +
         std::to_string(c.year);
}

std::string format_clock(Epoch t) {
  if (t == kNoDate) return "";
  const CivilTime c = civil_from_epoch(t);
  return pad2(c.hour) + ":" + pad2(c.minute);
}

uint32_t seconds_until_local_time(const std::string& hh_mm, Epoch local_now) {
  constexpr uint32_t kDay = 24 * 60 * 60;

  // "05:30". Anything else is not worth guessing at: a device that never
  // wakes is worse than one that wakes at the wrong time.
  if (hh_mm.size() != 5 || hh_mm[2] != ':') return kDay;
  for (size_t i : {0u, 1u, 3u, 4u}) {
    if (hh_mm[i] < '0' || hh_mm[i] > '9') return kDay;
  }
  const int hour = (hh_mm[0] - '0') * 10 + (hh_mm[1] - '0');
  const int minute = (hh_mm[3] - '0') * 10 + (hh_mm[4] - '0');
  if (hour > 23 || minute > 59) return kDay;

  const CivilTime now = civil_from_epoch(local_now);
  CivilTime target = now;
  target.hour = hour;
  target.minute = minute;
  target.second = 0;

  Epoch when = epoch_from_civil(target);
  // On the dot counts as gone: otherwise a wake that lands exactly on the
  // hour would schedule a zero-second sleep and spin.
  if (when <= local_now) when += static_cast<Epoch>(kDay);
  return static_cast<uint32_t>(when - local_now);
}

}  // namespace rsspaper
