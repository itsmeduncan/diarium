#include "core/xml/entities.h"

#include <cstdlib>

#include "core/base/str.h"

namespace rsspaper {
namespace {

struct Entity {
  const char* name;
  uint32_t cp;
};

// Not the full HTML5 set (2231 names, ~40KB) — the ones feeds actually emit.
// Anything missing falls through as literal text, which is the safe failure.
const Entity kEntities[] = {
    {"amp", 38},      {"lt", 60},        {"gt", 62},       {"quot", 34},
    {"apos", 39},     {"nbsp", 160},     {"iexcl", 161},   {"cent", 162},
    {"pound", 163},   {"curren", 164},   {"yen", 165},     {"brvbar", 166},
    {"sect", 167},    {"uml", 168},      {"copy", 169},    {"ordf", 170},
    {"laquo", 171},   {"not", 172},      {"shy", 173},     {"reg", 174},
    {"macr", 175},    {"deg", 176},      {"plusmn", 177},  {"sup2", 178},
    {"sup3", 179},    {"acute", 180},    {"micro", 181},   {"para", 182},
    {"middot", 183},  {"cedil", 184},    {"sup1", 185},    {"ordm", 186},
    {"raquo", 187},   {"frac14", 188},   {"frac12", 189},  {"frac34", 190},
    {"iquest", 191},  {"Agrave", 192},   {"Aacute", 193},  {"Acirc", 194},
    {"Atilde", 195},  {"Auml", 196},     {"Aring", 197},   {"AElig", 198},
    {"Ccedil", 199},  {"Egrave", 200},   {"Eacute", 201},  {"Ecirc", 202},
    {"Euml", 203},    {"Igrave", 204},   {"Iacute", 205},  {"Icirc", 206},
    {"Iuml", 207},    {"ETH", 208},      {"Ntilde", 209},  {"Ograve", 210},
    {"Oacute", 211},  {"Ocirc", 212},    {"Otilde", 213},  {"Ouml", 214},
    {"times", 215},   {"Oslash", 216},   {"Ugrave", 217},  {"Uacute", 218},
    {"Ucirc", 219},   {"Uuml", 220},     {"Yacute", 221},  {"THORN", 222},
    {"szlig", 223},   {"agrave", 224},   {"aacute", 225},  {"acirc", 226},
    {"atilde", 227},  {"auml", 228},     {"aring", 229},   {"aelig", 230},
    {"ccedil", 231},  {"egrave", 232},   {"eacute", 233},  {"ecirc", 234},
    {"euml", 235},    {"igrave", 236},   {"iacute", 237},  {"icirc", 238},
    {"iuml", 239},    {"eth", 240},      {"ntilde", 241},  {"ograve", 242},
    {"oacute", 243},  {"ocirc", 244},    {"otilde", 245},  {"ouml", 246},
    {"divide", 247},  {"oslash", 248},   {"ugrave", 249},  {"uacute", 250},
    {"ucirc", 251},   {"uuml", 252},     {"yacute", 253},  {"thorn", 254},
    {"yuml", 255},    {"OElig", 338},    {"oelig", 339},   {"Scaron", 352},
    {"scaron", 353},  {"Yuml", 376},     {"fnof", 402},    {"circ", 710},
    {"tilde", 732},   {"ensp", 8194},    {"emsp", 8195},   {"thinsp", 8201},
    {"zwnj", 8204},   {"zwj", 8205},     {"lrm", 8206},    {"rlm", 8207},
    {"ndash", 8211},  {"mdash", 8212},   {"lsquo", 8216},  {"rsquo", 8217},
    {"sbquo", 8218},  {"ldquo", 8220},   {"rdquo", 8221},  {"bdquo", 8222},
    {"dagger", 8224}, {"Dagger", 8225},  {"bull", 8226},   {"hellip", 8230},
    {"permil", 8240}, {"prime", 8242},   {"Prime", 8243},  {"lsaquo", 8249},
    {"rsaquo", 8250}, {"oline", 8254},   {"frasl", 8260},  {"euro", 8364},
    {"trade", 8482},  {"larr", 8592},    {"uarr", 8593},   {"rarr", 8594},
    {"darr", 8595},   {"harr", 8596},    {"minus", 8722},  {"lowast", 8727},
    {"ne", 8800},     {"le", 8804},      {"ge", 8805},     {"loz", 9674},
    {"spades", 9824}, {"clubs", 9827},   {"hearts", 9829}, {"diams", 9830},
};

// Windows-1252 characters that authors write as numeric refs 128-159. HTML5
// says to remap them; feeds rely on it for curly quotes.
uint32_t remap_cp1252(uint32_t cp) {
  static const uint32_t kMap[32] = {
      0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
      0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
  return (cp >= 128 && cp <= 159) ? kMap[cp - 128] : cp;
}

}  // namespace

bool decode_entity(const std::string& body, uint32_t* out_cp) {
  if (body.empty() || body.size() > 32) return false;

  if (body[0] == '#') {
    uint32_t cp = 0;
    if (body.size() > 2 && (body[1] == 'x' || body[1] == 'X')) {
      for (size_t i = 2; i < body.size(); ++i) {
        const char c = ascii_lower(body[i]);
        const uint32_t d = (c >= '0' && c <= '9')   ? uint32_t(c - '0')
                           : (c >= 'a' && c <= 'f') ? uint32_t(c - 'a' + 10)
                                                    : 0xFFFFFFFFu;
        if (d == 0xFFFFFFFFu) return false;
        cp = cp * 16 + d;
        if (cp > 0x10FFFF) return false;
      }
    } else {
      for (size_t i = 1; i < body.size(); ++i) {
        if (body[i] < '0' || body[i] > '9') return false;
        cp = cp * 10 + uint32_t(body[i] - '0');
        if (cp > 0x10FFFF) return false;
      }
      if (body.size() == 1) return false;
    }
    if (cp == 0) return false;
    *out_cp = remap_cp1252(cp);
    return true;
  }

  for (const Entity& e : kEntities) {
    if (body == e.name) {
      *out_cp = e.cp;
      return true;
    }
  }
  // Case-insensitive second pass: "&AMP;" and "&Nbsp;" both occur.
  for (const Entity& e : kEntities) {
    if (iequals(body, e.name)) {
      *out_cp = e.cp;
      return true;
    }
  }
  return false;
}

std::string decode_entities(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') {
      out.push_back(s[i]);
      continue;
    }
    const size_t semi = s.find(';', i + 1);
    if (semi == std::string::npos || semi - i > 34) {
      out.push_back('&');
      continue;
    }
    uint32_t cp = 0;
    if (decode_entity(s.substr(i + 1, semi - i - 1), &cp)) {
      utf8_append(out, cp);
      i = semi;
    } else {
      out.push_back('&');
    }
  }
  return out;
}

}  // namespace rsspaper
