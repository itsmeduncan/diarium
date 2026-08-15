#include "core/xml/xml_pull.h"

#include "core/base/str.h"
#include "core/xml/entities.h"

namespace rsspaper {
namespace {

bool is_name_start(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
         c == ':' || c >= 0x80;
}

bool is_name_char(int c) {
  return is_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

bool is_ws(int c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

uint32_t cp1252_to_unicode(uint8_t b) {
  static const uint32_t kMap[32] = {
      0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
      0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
  return (b >= 0x80 && b <= 0x9F) ? kMap[b - 0x80] : b;
}

}  // namespace

XmlPullParser::XmlPullParser(ByteSource& src, XmlLimits limits)
    : src_(src), limits_(limits) {
  attrs_.reserve(8);
  stack_.reserve(16);
}

bool XmlPullParser::refill() {
  if (eof_) return false;
  buf_len_ = src_.read(buf_, sizeof(buf_));
  buf_pos_ = 0;
  if (buf_len_ == 0) {
    eof_ = true;
    return false;
  }
  return true;
}

int XmlPullParser::get() {
  if (pushback_ >= 0) {
    const int c = pushback_;
    pushback_ = -1;
    return c;
  }
  if (buf_pos_ >= buf_len_ && !refill()) return -1;
  ++consumed_;
  return static_cast<uint8_t>(buf_[buf_pos_++]);
}

void XmlPullParser::unget(int c) {
  if (c >= 0) pushback_ = c;
}

void XmlPullParser::append_byte(std::string& dst, int c) {
  if (c < 0) return;
  if (latin1_ && c >= 0x80) {
    utf8_append(dst, cp1252_to_unicode(static_cast<uint8_t>(c)));
  } else {
    dst.push_back(static_cast<char>(c));
  }
}

void XmlPullParser::skip_until(const char* terminator) {
  const std::string term(terminator);
  std::string tail;
  tail.reserve(term.size());
  int c;
  while ((c = get()) >= 0) {
    tail.push_back(static_cast<char>(c));
    if (tail.size() > term.size()) tail.erase(0, 1);
    if (tail == term) return;
  }
}

void XmlPullParser::scan_declaration_encoding(const std::string& decl) {
  const size_t at = decl.find("encoding");
  if (at == std::string::npos) return;
  const size_t eq = decl.find('=', at);
  if (eq == std::string::npos) return;
  size_t i = eq + 1;
  while (i < decl.size() && is_ws(decl[i])) ++i;
  if (i >= decl.size() || (decl[i] != '"' && decl[i] != '\'')) return;
  const char quote = decl[i++];
  std::string enc;
  while (i < decl.size() && decl[i] != quote) enc.push_back(decl[i++]);
  ascii_lower_inplace(enc);
  // Anything that isn't UTF-8 is treated as Windows-1252 — a superset of
  // Latin-1, and what mislabelled feeds almost always turn out to be.
  latin1_ = !(enc.empty() || enc == "utf-8" || enc == "utf8" ||
              enc == "us-ascii" || enc == "ascii");
}

bool XmlPullParser::scan_name(std::string* out_prefix, std::string* out_name,
                              size_t cap) {
  out_prefix->clear();
  out_name->clear();
  std::string acc;
  int c;
  while ((c = get()) >= 0) {
    if (!is_name_char(c)) {
      unget(c);
      break;
    }
    if (c == ':' && out_prefix->empty() && !acc.empty()) {
      *out_prefix = acc;
      acc.clear();
      continue;
    }
    if (acc.size() < cap) {
      append_byte(acc, c);
    } else {
      recovered_ = true;
    }
  }
  ascii_lower_inplace(acc);
  ascii_lower_inplace(*out_prefix);
  *out_name = acc;
  return !acc.empty();
}

void XmlPullParser::scan_attributes() {
  attrs_.clear();
  for (;;) {
    int c = get();
    while (is_ws(c)) c = get();
    if (c < 0 || c == '>' || c == '/') {
      unget(c);
      return;
    }
    if (!is_name_start(c)) {  // junk between attributes; drop the byte
      recovered_ = true;
      continue;
    }
    unget(c);

    std::string aprefix, aname;
    if (!scan_name(&aprefix, &aname, limits_.attr_name_bytes)) {
      recovered_ = true;
      get();  // guarantee forward progress
      continue;
    }

    c = get();
    while (is_ws(c)) c = get();
    std::string value;
    if (c == '=') {
      c = get();
      while (is_ws(c)) c = get();
      if (c == '"' || c == '\'') {
        const int quote = c;
        while ((c = get()) >= 0 && c != quote) {
          if (c == '&') {
            read_entity_into(value);
          } else if (value.size() < limits_.attr_value_bytes) {
            append_byte(value, c);
          } else {
            recovered_ = true;
          }
        }
      } else {  // unquoted value: tolerated, terminated by whitespace or '>'
        while (c >= 0 && !is_ws(c) && c != '>') {
          if (value.size() < limits_.attr_value_bytes) append_byte(value, c);
          c = get();
        }
        unget(c);
      }
    } else {
      unget(c);  // valueless attribute (an HTML-ism, but it shows up)
    }

    if (attrs_.size() < limits_.max_attrs) {
      attrs_.push_back(Attr{aname, value});
    } else {
      recovered_ = true;
    }
  }
}

void XmlPullParser::read_entity_into(std::string& dst) {
  // The caller has consumed '&'. Gather to ';'; anything unterminated or
  // unrecognised is emitted literally, because a bare '&' in a feed is text.
  std::string body;
  int c;
  while ((c = get()) >= 0 && c != ';') {
    if (is_ws(c) || c == '<' || c == '&' || body.size() >= 34) {
      unget(c);
      break;
    }
    body.push_back(static_cast<char>(c));
  }
  uint32_t cp = 0;
  if (c == ';' && decode_entity(body, &cp)) {
    utf8_append(dst, cp);
    return;
  }
  recovered_ = true;
  dst.push_back('&');
  dst += body;
  if (c == ';') dst.push_back(';');
}

void XmlPullParser::read_cdata_chunk() {
  // Inside a CDATA body. Stop at "]]>" or at the chunk cap, whichever comes
  // first; the cap keeps a 700 KB Substack post from becoming a 700 KB string.
  int closing = 0;  // count of consecutive ']' seen
  int c;
  while ((c = get()) >= 0) {
    if (c == ']') {
      ++closing;
      continue;
    }
    if (c == '>' && closing >= 2) {
      in_cdata_ = false;
      return;
    }
    for (int k = 0; k < closing; ++k) text_.push_back(']');
    closing = 0;
    append_byte(text_, c);
    if (text_.size() >= limits_.text_chunk_bytes) {
      in_cdata_ = true;
      return;
    }
  }
  for (int k = 0; k < closing; ++k) text_.push_back(']');
  in_cdata_ = false;  // EOF inside CDATA
}

const std::string* XmlPullParser::attr(const char* attr_name) const {
  for (const Attr& a : attrs_) {
    if (iequals(a.name, attr_name)) return &a.value;
  }
  return nullptr;
}

std::string XmlPullParser::attr_or(const char* attr_name,
                                   const char* fallback) const {
  const std::string* v = attr(attr_name);
  return v ? *v : std::string(fallback);
}

XmlEvent XmlPullParser::next() {
  if (pending_self_close_) {
    pending_self_close_ = false;
    // A self-closing element was pushed like any other so that depth() means
    // the same thing for `<link/>` and `<link></link>`; pop it now.
    if (!stack_.empty() && stack_.back() == self_close_name_) stack_.pop_back();
    name_ = self_close_name_;
    prefix_ = self_close_prefix_;
    event_ = XmlEvent::EndElement;
    return event_;
  }

  if (!started_) {
    started_ = true;
    const int b0 = get();  // skip a UTF-8 BOM if there is one
    if (b0 == 0xEF) {
      const int b1 = get();
      const int b2 = get();
      if (!(b1 == 0xBB && b2 == 0xBF)) unget(b2);
    } else {
      unget(b0);
    }
  }

  text_.clear();
  text_cdata_ = false;

  if (in_cdata_) {
    read_cdata_chunk();
    text_cdata_ = true;
    event_ = XmlEvent::Text;
    return event_;
  }

  for (;;) {
    int c = get();
    if (c < 0) {
      if (!text_.empty()) {
        event_ = XmlEvent::Text;
        return event_;
      }
      event_ = XmlEvent::EndOfDocument;
      return event_;
    }

    if (c != '<') {
      if (c == '&') {
        read_entity_into(text_);
      } else {
        append_byte(text_, c);
      }
      if (text_.size() >= limits_.text_chunk_bytes) {
        event_ = XmlEvent::Text;
        return event_;
      }
      continue;
    }

    // A tag starts here. Any text we've accumulated is a complete run, so
    // hand it over first and re-read the '<' on the next call.
    if (!text_.empty()) {
      unget('<');
      event_ = XmlEvent::Text;
      return event_;
    }

    const int c1 = get();

    if (c1 == '!') {
      const int c2 = get();
      if (c2 == '-') {
        get();  // the second '-'
        skip_until("-->");
        continue;
      }
      if (c2 == '[') {
        std::string marker;
        for (int i = 0; i < 6; ++i) {
          const int ch = get();
          if (ch < 0) break;
          marker.push_back(static_cast<char>(ch));
        }
        if (iequals(marker, "CDATA[")) {
          read_cdata_chunk();
          text_cdata_ = true;
          event_ = XmlEvent::Text;
          return event_;
        }
        skip_until(">");
        continue;
      }
      skip_until(">");  // DOCTYPE or another declaration
      continue;
    }

    if (c1 == '?') {
      std::string decl;
      int ch, prev = 0;
      while ((ch = get()) >= 0) {
        if (prev == '?' && ch == '>') break;
        if (decl.size() < 256) decl.push_back(static_cast<char>(ch));
        prev = ch;
      }
      if (!decl.empty() && ascii_lower(decl[0]) == 'x') {
        scan_declaration_encoding(decl);
      }
      continue;
    }

    if (c1 == '/') {
      std::string p, n;
      scan_name(&p, &n, limits_.name_bytes);
      skip_until(">");
      if (n.empty()) {
        recovered_ = true;
        continue;
      }
      // Reconcile with the stack: pop to the matching element if it is open,
      // otherwise treat the tag as noise rather than corrupting the depth.
      size_t found = stack_.size();
      for (size_t i = stack_.size(); i-- > 0;) {
        if (stack_[i] == n) {
          found = i;
          break;
        }
      }
      if (found == stack_.size()) {
        recovered_ = true;
        continue;
      }
      if (found + 1 != stack_.size()) recovered_ = true;
      stack_.resize(found);
      name_ = n;
      prefix_ = p;
      event_ = XmlEvent::EndElement;
      return event_;
    }

    if (!is_name_start(c1)) {  // a bare '<' sitting in text
      recovered_ = true;
      append_byte(text_, '<');
      unget(c1);
      continue;
    }

    unget(c1);
    std::string p, n;
    if (!scan_name(&p, &n, limits_.name_bytes)) {
      recovered_ = true;
      continue;
    }
    scan_attributes();

    bool self_closing = false;
    int ch = get();
    while (is_ws(ch)) ch = get();
    if (ch == '/') {
      self_closing = true;
      ch = get();
    }
    if (ch != '>') {
      skip_until(">");
      recovered_ = true;
    }

    if (stack_.size() < limits_.max_depth) {
      stack_.push_back(n);
    } else {
      recovered_ = true;
    }
    if (self_closing) {
      pending_self_close_ = true;
      self_close_name_ = n;
      self_close_prefix_ = p;
    }

    name_ = n;
    prefix_ = p;
    event_ = XmlEvent::StartElement;
    return event_;
  }
}

}  // namespace rsspaper
