# Decisions

Running log of choices that were not obvious, and why. Newest last.

---

### 1. Portability seam is a HAL, not `#ifdef`

Everything above `src/hal/hal.h` is plain C++17 with no Arduino, no ESP-IDF and
no Inkplate headers, so the whole pipeline compiles and runs on a laptop. A
port to another ESP32-class board implements six interfaces (display, input,
clock, power, storage, network) and touches nothing else.

The cost is indirection on the display path. It's worth it: layout is the
product, and layout you can only see by flashing a board is layout you iterate
on ten times a day instead of a hundred.

### 2. Two build systems

`CMakeLists.txt` is the real one and is what CI runs. The `Makefile` exists
because a fresh clone should build with nothing but a compiler, and cmake was
not installed on the machine this was written on. They must stay in sync;
both glob the same source trees so adding a file doesn't require touching
either.

### 3. Streaming pull parser for XML, push parser for HTML

The XML parser pulls from a `ByteSource` — the feed parser drives it and
decides when to stop, which is what makes "parse 12 items out of a 772 KB
Substack feed and stop reading the socket" work.

HTML can't use the same shape, because by the time we're converting an
article body we are already inside the XML parser's pull loop. So the HTML
converter is push-mode: it takes arbitrary chunks and emits blocks. Two
tokenizers, but no coroutines and no second thread.

### 4. Tolerance over conformance

Both parsers recover rather than fail: mismatched end tags pop to the nearest
match, unknown entities pass through as literal text, oversized fields
truncate, and a document declaring a legacy encoding is transcoded from
Windows-1252. A malformed feed should cost one story, never the edition.
`XmlPullParser::saw_recoverable_error()` reports when a tolerance rule fired so
the fixture corpus can be audited.

### 5. Bounded memory everywhere, enforced by construction

No buffer scales with document size. Names, attribute values, text chunks,
blocks per item and bytes per block all have caps in `XmlLimits` / `HtmlLimits`.
The parser's own footprint is roughly 16 KB regardless of feed size; the item
store is separately bounded by `max_items`. Budget from the brief was <64 KB
above the item store for a 2 MB feed.

### 6. Titles get a second decoding pass

`<title type="html">` wrapping CDATA that contains `&#8217;` is standard
practice (The Verge, among others), which means a correct single-pass XML
parser produces literally `Don&#8217;t`. Titles containing `<` or `&` are run
through the HTML path, which strips tags and resolves the second layer. Plain
titles are untouched.

### 7. Publisher truncation is measured, not fixed

v1 renders what the feed publishes. Every item records a `TruncationReason`
(`summary-only`, `ellipsis-tail`, `read-more-link`, `very-short`, or none) so
the question "how much would an on-device readability extractor actually buy
us?" can be answered with numbers from a real feed list. Across the current
12-feed corpus roughly half of items look truncated.

A "summary" of 1500+ characters across 3+ blocks is _not_ counted as truncated
— plenty of blogs put the whole article in `<description>` and never populate
`content:encoded`.

### 8. Literata, at two optical sizes

Body text uses the 7 pt optical cut (open, sturdy, tall x-height); display type
uses the 36 pt cut (tighter, finer). Using the right optical size for the
physical size is most of why a page reads as a newspaper rather than a website.
Literata is also SIL OFL, which permits embedding, and was designed for
e-readers.

Sizes are chosen in _physical_ terms: the panel is ~212 PPI, so 27 px body text
is about 9.2 pt — a book size. Picking sizes that "look right" on a monitor
produces large-print on the device.

### 9. No dedicated masthead face

A 104 px nameplate face would have cost roughly 200 KB of flash for about 70
glyphs. The nameplate is instead set in the 66 px `Lead` face, letterspaced
across the measure, which is closer to how broadsheet nameplates actually work
and costs nothing.

### 10. Glyphs are baked at build time, 4 bits of alpha

Rasterising on device means shipping a TTF interpreter and paying for it on
every page turn. `tools/fontgen` bakes exactly the faces, sizes and codepoints
declared in `faces.h` into a pack that loads once into PSRAM. Coverage becomes
a build-time decision, which is where it belongs.

4-bit alpha is not a compromise — the panel renders 3-bit greyscale, so the
fourth bit is already more than the hardware can show. It halves the pack.

---

## Open questions

- **Licence**: MIT or AGPL. Unresolved; `LICENSE` is a placeholder.
- **Body weight on e-ink**: Regular may render thin on a reflective panel.
  Literata Medium as the body face is a one-line change in `faces.cpp` if the
  rendered pages look weak.
- **Hyphenation**: currently a stub interface. Justified text at ~34 characters
  per column on the two-column front page will want real hyphenation
  (Liang/TeX patterns are ~25 KB compressed for English).
