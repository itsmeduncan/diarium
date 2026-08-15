#include <algorithm>
#include <string>

#include "core/io/byte_source.h"
#include "core/xml/xml_pull.h"
#include "doctest.h"

using namespace rsspaper;

namespace {

// Flattens a document to a compact event trace: `<name`, `>name`, and `"text`.
// Adjacent text events are merged so chunk boundaries don't change the trace.
std::string trace(const std::string& xml, bool* recovered = nullptr) {
  MemoryByteSource src(xml);
  XmlPullParser xp(src);
  std::string out;
  std::string pending_text;
  auto flush = [&]() {
    if (!pending_text.empty()) {
      out += "\"" + pending_text + " ";
      pending_text.clear();
    }
  };
  for (;;) {
    const XmlEvent ev = xp.next();
    if (ev == XmlEvent::EndOfDocument) break;
    if (ev == XmlEvent::Text) {
      pending_text += xp.text();
    } else if (ev == XmlEvent::StartElement) {
      flush();
      out += "<" + xp.qname() + " ";
    } else if (ev == XmlEvent::EndElement) {
      flush();
      out += ">" + xp.qname() + " ";
    }
  }
  flush();
  if (recovered != nullptr) *recovered = xp.saw_recoverable_error();
  return out;
}

}  // namespace

TEST_CASE("elements, text and nesting") {
  CHECK(trace("<a><b>hi</b></a>") == "<a <b \"hi >b >a ");
}

TEST_CASE("attributes are read by local name, case-insensitively") {
  MemoryByteSource src(R"(<link REL="alternate" href='http://x/y?a=1&amp;b=2'/>)");
  XmlPullParser xp(src);
  REQUIRE(xp.next() == XmlEvent::StartElement);
  CHECK(xp.name() == "link");
  CHECK(xp.attr_or("rel") == "alternate");
  CHECK(xp.attr_or("href") == "http://x/y?a=1&b=2");
  CHECK(xp.attr("missing") == nullptr);
  CHECK(xp.attr_or("missing", "fallback") == "fallback");
}

TEST_CASE("self-closing elements have the same depth profile as open/close") {
  MemoryByteSource a("<root><x/></root>");
  MemoryByteSource b("<root><x></x></root>");
  XmlPullParser pa(a), pb(b);
  for (int i = 0; i < 4; ++i) {
    const XmlEvent ea = pa.next();
    const XmlEvent eb = pb.next();
    CHECK(ea == eb);
    CHECK(pa.name() == pb.name());
    CHECK(pa.depth() == pb.depth());
  }
}

TEST_CASE("namespace prefixes are exposed separately from local names") {
  MemoryByteSource src("<content:encoded>x</content:encoded>");
  XmlPullParser xp(src);
  REQUIRE(xp.next() == XmlEvent::StartElement);
  CHECK(xp.prefix() == "content");
  CHECK(xp.name() == "encoded");
  CHECK(xp.qname() == "content:encoded");
}

TEST_CASE("CDATA is delivered as text without entity decoding") {
  MemoryByteSource src("<p><![CDATA[a & b <not-a-tag> &#8217;]]></p>");
  XmlPullParser xp(src);
  REQUIRE(xp.next() == XmlEvent::StartElement);
  REQUIRE(xp.next() == XmlEvent::Text);
  CHECK(xp.text_was_cdata());
  CHECK(xp.text() == "a & b <not-a-tag> &#8217;");
}

TEST_CASE("a CDATA body longer than a chunk arrives in pieces") {
  const std::string body(5000, 'x');
  MemoryByteSource src("<p><![CDATA[" + body + "]]></p>");
  XmlPullParser xp(src);
  REQUIRE(xp.next() == XmlEvent::StartElement);
  std::string got;
  int chunks = 0;
  for (;;) {
    const XmlEvent ev = xp.next();
    if (ev != XmlEvent::Text) break;
    got += xp.text();
    ++chunks;
  }
  CHECK(got == body);
  CHECK(chunks > 1);  // the point: it was not buffered whole
}

TEST_CASE("entities in text, including ones we don't know") {
  CHECK(trace("<p>a &amp; b &#8217;c&#x2014;d</p>") ==
        "<p \"a & b ’c—d >p ");
  bool recovered = false;
  CHECK(trace("<p>Tom & Jerry</p>", &recovered) == "<p \"Tom & Jerry >p ");
  CHECK(recovered);
}

TEST_CASE("comments, declarations and processing instructions are skipped") {
  CHECK(trace("<?xml version=\"1.0\"?><!DOCTYPE x><a><!-- c --xx -->t</a>") ==
        "<a \"t >a ");
}

TEST_CASE("tolerance: a stray end tag is noise, not a parse failure") {
  bool recovered = false;
  CHECK(trace("<a>one</b>two</a>", &recovered) == "<a \"onetwo >a ");
  CHECK(recovered);
}

TEST_CASE("tolerance: unclosed inner elements pop to the matching parent") {
  bool recovered = false;
  const std::string t = trace("<a><b><c>x</a>", &recovered);
  CHECK(t == "<a <b <c \"x >a ");
  CHECK(recovered);
}

TEST_CASE("tolerance: a bare '<' in prose is text") {
  bool recovered = false;
  CHECK(trace("<p>3 < 4</p>", &recovered) == "<p \"3 < 4 >p ");
  CHECK(recovered);
}

TEST_CASE("a document declaring windows-1252 is transcoded to UTF-8") {
  // 0x92 is a curly apostrophe in cp1252 and invalid on its own in UTF-8.
  std::string xml = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><p>it";
  xml.push_back(static_cast<char>(0x92));
  xml += "s</p>";
  CHECK(trace(xml) == "<p \"it’s >p ");
}

TEST_CASE("a UTF-8 BOM is skipped") {
  CHECK(trace("\xEF\xBB\xBF<a>x</a>") == "<a \"x >a ");
}

TEST_CASE("parsing an empty or truncated document terminates") {
  CHECK(trace("") == "");
  CHECK(trace("<a><b>unterminated") == "<a <b \"unterminated ");
  CHECK(trace("<") == "\"< ");
}

TEST_CASE("oversize names and attribute values are capped, not fatal") {
  const std::string long_name(500, 'n');
  bool recovered = false;
  const std::string t = trace("<" + long_name + ">x</" + long_name + ">",
                              &recovered);
  CHECK(recovered);
  CHECK(t.find("\"x") != std::string::npos);
}

TEST_CASE("memory stays bounded on a large document") {
  // 200 KB of text through a parser whose buffers are all fixed size.
  std::string xml = "<root>";
  for (int i = 0; i < 2000; ++i) xml += "<p>" + std::string(100, 'a') + "</p>";
  xml += "</root>";

  MemoryByteSource src(xml);
  XmlPullParser xp(src);
  size_t text_total = 0, max_chunk = 0;
  int elements = 0;
  for (;;) {
    const XmlEvent ev = xp.next();
    if (ev == XmlEvent::EndOfDocument) break;
    if (ev == XmlEvent::Text) {
      text_total += xp.text().size();
      max_chunk = std::max(max_chunk, xp.text().size());
    } else if (ev == XmlEvent::StartElement) {
      ++elements;
    }
  }
  CHECK(elements == 2001);
  CHECK(text_total == 200000);
  CHECK(max_chunk <= 1100);  // the chunk cap, not the document size
  CHECK(xp.depth() == 0);
}
