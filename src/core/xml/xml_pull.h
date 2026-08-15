// A tolerant, streaming XML pull parser.
//
// Two properties matter more than standards conformance here:
//   1. Bounded memory. Nothing scales with document size — names, attribute
//      values and text chunks all have hard caps, and text is delivered in
//      pieces. A 2 MB feed parses in the same footprint as a 2 KB one.
//   2. It never gives up. Real feeds ship mismatched tags, bare ampersands,
//      undeclared entities and Windows-1252 bytes labelled as UTF-8. A parse
//      error loses one element, never the edition.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/io/byte_source.h"

namespace rsspaper {

enum class XmlEvent : uint8_t {
  None,
  StartElement,
  EndElement,
  Text,
  EndOfDocument,
};

struct XmlLimits {
  size_t name_bytes = 96;
  size_t attr_name_bytes = 64;
  size_t attr_value_bytes = 512;
  size_t max_attrs = 16;
  size_t text_chunk_bytes = 1024;
  size_t max_depth = 32;
};

class XmlPullParser {
 public:
  explicit XmlPullParser(ByteSource& src, XmlLimits limits = XmlLimits());

  // Advances to the next event. Returns EndOfDocument once the source is
  // drained; calling again keeps returning EndOfDocument.
  XmlEvent next();

  XmlEvent event() const { return event_; }

  // Lowercased local name (namespace prefix stripped). Valid for
  // StartElement/EndElement.
  const std::string& name() const { return name_; }
  // Lowercased namespace prefix, empty when the element had none. Feeds use
  // this to tell `content:encoded` from a plain `encoded`.
  const std::string& prefix() const { return prefix_; }
  // "prefix:name" when a prefix was present, otherwise just the name.
  std::string qname() const {
    return prefix_.empty() ? name_ : prefix_ + ":" + name_;
  }

  // Entity-decoded text chunk. Valid for Text.
  const std::string& text() const { return text_; }
  bool text_was_cdata() const { return text_cdata_; }

  // Attribute lookup by lowercased local name. Valid for StartElement.
  const std::string* attr(const char* attr_name) const;
  std::string attr_or(const char* attr_name, const char* fallback = "") const;

  // Nesting depth of the current element: 1 for the root.
  int depth() const { return static_cast<int>(stack_.size()); }

  size_t bytes_consumed() const { return consumed_; }
  bool source_failed() const { return src_.failed(); }
  // True if any tolerance rule fired (stray end tag, unknown entity, oversize
  // field). Useful for the fixture corpus report, never fatal.
  bool saw_recoverable_error() const { return recovered_; }

 private:
  // -- byte plumbing --
  int get();          // next byte, or -1 at EOF
  void unget(int c);  // one byte of pushback
  bool refill();
  // Appends one source byte to `dst`, transcoding if the document declared a
  // legacy 8-bit encoding.
  void append_byte(std::string& dst, int c);

  // -- scanners --
  void scan_declaration_encoding(const std::string& decl);
  bool scan_name(std::string* out_prefix, std::string* out_name, size_t cap);
  void scan_attributes();
  void skip_until(const char* terminator);
  void read_entity_into(std::string& dst);
  // Reads CDATA body up to the chunk cap or the closing "]]>", leaving
  // `in_cdata_` set if more remains.
  void read_cdata_chunk();

  ByteSource& src_;
  XmlLimits limits_;

  char buf_[2048];
  size_t buf_len_ = 0;
  size_t buf_pos_ = 0;
  int pushback_ = -1;
  bool eof_ = false;
  size_t consumed_ = 0;

  bool started_ = false;
  bool latin1_ = false;   // document declared a Windows-1252/Latin-1 encoding
  bool recovered_ = false;
  // A CDATA section longer than one text chunk stays open across calls.
  bool in_cdata_ = false;

  XmlEvent event_ = XmlEvent::None;
  std::string name_;
  std::string prefix_;
  std::string text_;
  bool text_cdata_ = false;

  struct Attr {
    std::string name;
    std::string value;
  };
  std::vector<Attr> attrs_;

  std::vector<std::string> stack_;
  // Set when a self-closing tag emitted StartElement; the next call emits the
  // matching EndElement.
  bool pending_self_close_ = false;
  std::string self_close_name_;
  std::string self_close_prefix_;
};

}  // namespace rsspaper
