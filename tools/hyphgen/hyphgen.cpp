// hyphgen — turns TeX hyphenation patterns into a table the device can search.
//
// Development-only, and run by hand: the patterns have not changed since 1990
// and the generated file is checked in, so no build system needs a codegen
// step. See assets/hyphenation/README.md.
//
//   hyphgen <hyph-en-us.tex> <out.cpp>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/base/str.h"
#include "core/io/file_byte_source.h"

namespace {

struct Pattern {
  std::string letters;        // digits stripped
  std::vector<uint8_t> values;  // letters.size() + 1 of them
};

// Strips TeX comments: '%' to end of line, unless escaped.
std::string strip_comments(const std::string& in) {
  std::string out;
  bool in_comment = false;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '\n') {
      in_comment = false;
      out.push_back('\n');
      continue;
    }
    if (in_comment) continue;
    if (in[i] == '%' && (i == 0 || in[i - 1] != '\\')) {
      in_comment = true;
      continue;
    }
    out.push_back(in[i]);
  }
  return out;
}

// Contents of \name{ ... }, brace-balanced.
bool extract_block(const std::string& text, const std::string& name,
                   std::string* out) {
  const size_t at = text.find("\\" + name + "{");
  if (at == std::string::npos) return false;
  size_t i = at + name.size() + 2;
  int depth = 1;
  const size_t begin = i;
  for (; i < text.size() && depth > 0; ++i) {
    if (text[i] == '{') ++depth;
    if (text[i] == '}') --depth;
  }
  if (depth != 0) return false;
  *out = text.substr(begin, i - 1 - begin);
  return true;
}

bool parse_pattern(const std::string& token, Pattern* out) {
  out->letters.clear();
  out->values.clear();
  out->values.push_back(0);
  for (char c : token) {
    if (c >= '0' && c <= '9') {
      out->values.back() = static_cast<uint8_t>(c - '0');
      continue;
    }
    // Patterns are ASCII letters plus '.' for a word boundary.
    if (!((c >= 'a' && c <= 'z') || c == '.')) return false;
    out->letters.push_back(c);
    out->values.push_back(0);
  }
  return !out->letters.empty();
}

void emit_bytes(std::string& out, const std::vector<uint8_t>& data,
                int per_line) {
  char buf[8];
  for (size_t i = 0; i < data.size(); ++i) {
    if (i % static_cast<size_t>(per_line) == 0) out += "\n   ";
    std::snprintf(buf, sizeof(buf), " %3u,", data[i]);
    out += buf;
  }
  out += "\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: hyphgen <patterns.tex> <out.cpp>\n");
    return 2;
  }

  std::string raw;
  if (!rsspaper::read_file(argv[1], &raw)) {
    std::fprintf(stderr, "hyphgen: cannot read %s\n", argv[1]);
    return 1;
  }
  const std::string text = strip_comments(raw);

  std::string patterns_block, exceptions_block;
  if (!extract_block(text, "patterns", &patterns_block)) {
    std::fprintf(stderr, "hyphgen: no \\patterns{} block in %s\n", argv[1]);
    return 1;
  }
  extract_block(text, "hyphenation", &exceptions_block);

  std::vector<Pattern> patterns;
  {
    std::string token;
    const std::string src = patterns_block + " ";
    for (char c : src) {
      if (rsspaper::is_space(c)) {
        if (!token.empty()) {
          Pattern p;
          if (parse_pattern(token, &p)) patterns.push_back(std::move(p));
          token.clear();
        }
        continue;
      }
      token.push_back(c);
    }
  }
  if (patterns.empty()) {
    std::fprintf(stderr, "hyphgen: parsed no patterns\n");
    return 1;
  }

  // Sorted by letters so the runtime can narrow a range by prefix instead of
  // searching for every substring of every word.
  std::sort(patterns.begin(), patterns.end(),
            [](const Pattern& a, const Pattern& b) {
              return a.letters < b.letters;
            });

  std::vector<std::string> exceptions;
  {
    std::string token;
    const std::string src = exceptions_block + " ";
    for (char c : src) {
      if (rsspaper::is_space(c)) {
        if (!token.empty()) exceptions.push_back(token);
        token.clear();
        continue;
      }
      token.push_back(c);
    }
  }

  std::vector<uint8_t> letters, values;
  std::vector<uint16_t> offsets;
  for (const Pattern& p : patterns) {
    offsets.push_back(static_cast<uint16_t>(letters.size()));
    for (char c : p.letters) letters.push_back(static_cast<uint8_t>(c));
    for (uint8_t v : p.values) values.push_back(v);
  }
  offsets.push_back(static_cast<uint16_t>(letters.size()));

  if (letters.size() > 0xFFFF) {
    std::fprintf(stderr,
                 "hyphgen: %zu letter bytes exceeds the u16 offset table\n",
                 letters.size());
    return 1;
  }

  std::string out;
  out +=
      "// GENERATED FILE — do not edit.\n"
      "//\n"
      "// Produced by tools/hyphgen from assets/hyphenation/hyph-en-us.tex:\n"
      "//\n"
      "//   ./bin/hyphgen assets/hyphenation/hyph-en-us.tex \\\n"
      "//       src/core/layout/hyphen_patterns_en.cpp\n"
      "//\n"
      "// Liang hyphenation patterns for American English.\n"
      "// Copyright (C) 1990, 2004, 2005 Gerard D.C. Kuiken\n"
      "//\n"
      "// Copying and distribution of this file, with or without\n"
      "// modification, are permitted in any medium without royalty provided\n"
      "// the copyright notice and this notice are preserved.\n"
      "\n#include \"core/layout/hyphenator.h\"\n"
      "\nnamespace rsspaper {\nnamespace hyphen_en {\n\n";

  char buf[128];
  std::snprintf(buf, sizeof(buf), "const size_t kPatternCount = %zu;\n\n",
                patterns.size());
  out += buf;

  out += "// Pattern letters, concatenated and sorted.\nconst uint8_t kLetters[] = {";
  emit_bytes(out, letters, 16);
  out += "};\n\n";

  out += "// Offset of each pattern into kLetters; the last entry is the total,\n"
         "// so a pattern's length is the difference between neighbours.\n"
         "const uint16_t kOffsets[] = {";
  {
    std::vector<uint8_t> flat;
    for (uint16_t v : offsets) {
      flat.push_back(static_cast<uint8_t>(v & 0xFF));
      flat.push_back(static_cast<uint8_t>(v >> 8));
    }
    char line[16];
    for (size_t i = 0; i < offsets.size(); ++i) {
      if (i % 12 == 0) out += "\n   ";
      std::snprintf(line, sizeof(line), " %5u,", offsets[i]);
      out += line;
    }
    out += "\n";
  }
  out += "};\n\n";

  out += "// Priorities, length+1 per pattern. A pattern's values begin at\n"
         "// kOffsets[i] + i, since each contributes one more value than it\n"
         "// has letters — which is why there is no second offset table.\n"
         "const uint8_t kValues[] = {";
  emit_bytes(out, values, 24);
  out += "};\n\n";

  out += "// Words the patterns get wrong, with their correct breaks.\n"
         "const char* const kExceptions[] = {\n";
  for (const std::string& e : exceptions) out += "    \"" + e + "\",\n";
  out += "};\n";
  std::snprintf(buf, sizeof(buf),
                "const size_t kExceptionCount = %zu;\n\n", exceptions.size());
  out += buf;

  out += "}  // namespace hyphen_en\n}  // namespace rsspaper\n";

  if (!rsspaper::write_file(argv[2], out)) {
    std::fprintf(stderr, "hyphgen: cannot write %s\n", argv[2]);
    return 1;
  }
  std::printf(
      "hyphgen: %zu patterns, %zu exceptions -> %s (%zu KB source, "
      "%zu KB of table)\n",
      patterns.size(), exceptions.size(), argv[2], out.size() / 1024,
      (letters.size() + values.size() + offsets.size() * 2) / 1024);
  return 0;
}
