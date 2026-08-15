// Small string / UTF-8 helpers. Portable C++17, no Arduino, no allocations
// beyond what the caller asks for.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rsspaper {

// --- UTF-8 -----------------------------------------------------------------

// Decodes one codepoint starting at `s[i]`, advancing `i`. Invalid sequences
// consume one byte and yield U+FFFD so malformed feeds can never stall a loop.
uint32_t utf8_next(const std::string& s, size_t& i);

// Appends `cp` to `out` as UTF-8. Codepoints above U+10FFFF become U+FFFD.
void utf8_append(std::string& out, uint32_t cp);

// Number of codepoints in `s` (lossy input counts each bad byte as one).
size_t utf8_length(const std::string& s);

// --- ASCII case / compare --------------------------------------------------

char ascii_lower(char c);
void ascii_lower_inplace(std::string& s);
bool iequals(const std::string& a, const char* b);
bool starts_with(const std::string& s, const char* prefix);
bool ends_with(const std::string& s, const char* suffix);
bool icontains(const std::string& haystack, const char* needle);

// --- Whitespace ------------------------------------------------------------

bool is_space(char c);
void trim_inplace(std::string& s);
std::string trim(const std::string& s);

// Collapses every run of whitespace to a single 0x20 and trims the ends.
// This is the normalisation every block of feed text goes through.
std::string collapse_ws(const std::string& s);

// --- Misc ------------------------------------------------------------------

// FNV-1a. Used for item dedup keys and ETag cache filenames; stable across
// runs and platforms, which is the only property we need.
uint64_t fnv1a(const std::string& s);
std::string to_hex64(uint64_t v);

}  // namespace rsspaper
