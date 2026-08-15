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

### 5. Bounded memory everywhere

No buffer scales with document size. Names, attribute values, text chunks,
blocks per item and bytes per block all have caps in `XmlLimits` / `HtmlLimits`.
The parser's own footprint measures ~12 KB and holds there regardless of feed
size.

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

### 11. Advances are stored in 1/16 px, not whole pixels

Rounding each glyph advance to an integer accumulates up to half a pixel of
error per character. Over a 65-character measure that is enough drift to make
justified text visibly uneven and to make measured line widths disagree with
drawn ones. Positions stay in subpixel units through line breaking and are
rounded once, at blit time.

### 12. Kerning is currently absent, and it is stb's fault, not the font's

Literata ships GPOS with no legacy `kern` table. `stb_truetype.h:2522` reads
`if (lookupType != 2) continue` — it handles PairPos lookups directly but skips
Extension (type 9) lookups, which is how modern fonts wrap their kerning. All
nine baked faces produce **zero** kern pairs; AV, Ta, Wa, To and P. all measure
unkerned.

Survivable at 27 px body, not at a 66 px lead headline. The pack format already
carries a kern table, so the fix is confined to `tools/fontgen`: follow
Extension lookups and resolve the pair adjustment ourselves. Build-time only,
no runtime cost, no new dependency. Not done yet.

### 13. The front page has a banner frame

A newspaper lead headline spans the page and its story continues down column
one. Uniform columns cannot express that, so `PageTemplate` grows a
`banner_height`: a full-width frame that is simply first in the frame list, so
the normal fill-frames-in-order logic puts the lead in it. The banner is sized
by measuring the lead's own elements, not by a guessed constant, and capped at
5/8 of the page below the nameplate so teasers still fit.

### 14. A front page is one page

Flowing every section's teasers onto the front-page template produced a
five-page "front page", which is a contradiction. Overflow is dropped — and
counted in `ComposeStats::front_page_overflow`, which the simulator prints.
Silently losing stories is exactly the failure a calm reader can't detect.

### 15. Lead headlines are fitted to the measure

A long headline at 66 px ran to four lines and became the entire page.
`fit_lead_headline` drops to the 44 px display size when the title won't sit in
two lines — which is what newspapers have always done, rather than setting
every lead at one size and hoping.

### 16. Image placeholders are caption lines, not boxes

A framed box is the obvious placeholder, but NASA's feed carries twenty images
per item and twenty empty boxes make the page unreadable. Images render as a
marked caption line (▣ + alt text). Real decoding replaces one function.

### 17. Kerning, resolved

`tools/fontgen/gpos_kerning.{h,cpp}` reads GPOS directly and follows the
Extension (type 9) lookups stb declines to. Literata now yields 6,800-8,500
pairs per face. Pairs adjusting by less than an eighth of a pixel are dropped —
glyph origins are rounded to whole pixels, so they could not be drawn — and the
kern record lost its padding, 8 bytes to 6. The pack is 733 KB.

### 18. Unrenderable characters: substitute, drop, or tofu

A book face has no dingbats, but publishers use them — Daring Fireball
prefixes every link post with ★. `fallback_codepoint` picks one of three
outcomes: substitute where a character in the face means the same thing (★ to
•, ▸ to ›), drop where it is decoration with no stand-in, or tofu for anything
presumed to be a letter, because a headline in a script we cannot set should
look missing rather than silently blank.

The whole chain lives in `Face::resolve`, so measuring and drawing cannot
disagree — a dropped glyph costs zero width in both.

### 19. Ledes you browse, stories you open

An edition of 40 stories laid out linearly ran to 175 pages. It ended, but
"read it and you're done" was doing a lot of work at that length.

Pages now come in two kinds. The *browse* sequence — front page plus section
pages of ledes — is 13 pages for the same 40 stories. Selecting a lede opens
the story's own pages; going back returns to the lede. `StoryRef` carries the
tap rect and page range, and `Edition::story_at` resolves a touch. The reader
never pages into a 40-page essay by accident.

`Paginator::paginate` reports a `Placement` per element — page plus area —
which is what makes a lede tappable without laying the edition out twice.

### 20. Keep-with-next

A lede is a kicker, a headline and a summary. Split across a page break it is
unreadable and its tap target is meaningless, so `FlowElement::keep_with_next`
moves the group as a unit.

The obvious guard — only move it if the group would fit on a fresh frame —
turned out to strand a section label at the foot of a column whenever the lede
under it was tall. `c.dirty` already prevents looping (after advancing, the
frame is clean and the group is placed regardless), so the guard was removed.

### 21. Release coordinates are not part of a gesture

A released touch panel reports no points, so whatever x,y a caller passes with
`touching = false` is meaningless. `GestureRecognizer` measures from the last
position seen while the finger was down, which means a caller has to poll
during the stroke and not only at its ends.

This is worth stating because the obvious test — press here, release there —
passes for the wrong reason on a naive implementation and fails on a correct
one. The contract is in the header.

### 22. Only the reader touches the HAL

`src/core/` is portable, and `tools/check-portability.sh` enforces it in CI:
no Arduino, Inkplate, WiFi, SD or ESP-IDF headers above `src/hal/`, and no
`#include "hal/..."` inside `src/core/` except `core/ui/reader`, which is the
top of the portable stack and drives the display by design.

Everything else in the core takes data and returns data.

### 23. Hyphenation: Liang patterns, baked into a searchable table

`tools/hyphgen` turns `assets/hyphenation/hyph-en-us.tex` into ~55 KB of const
tables — pattern letters, u16 offsets, and priorities. The generated file is
checked in, so none of the three build systems needs a codegen step; the
patterns have not changed since 1990.

The values table needs no offsets of its own: each pattern contributes one
more value than it has letters, so pattern *i*'s values start at
`kOffsets[i] + i`.

Matching narrows a range over the sorted table by prefix rather than searching
for every substring of every word, and exits as soon as the range empties.
Words containing anything outside ASCII letters are left whole — the patterns
are ASCII and guessing at "naïve" or "don't" would be worse than not breaking.

The licence is the FSF all-permissive notice: copying and distribution with or
without modification, royalty-free, provided the notice is preserved. It is
preserved in the file, in the generated header, and in `LICENSE`.

### 24. Paragraphs are indented, not spaced

With no space between paragraphs, the first-line indent is the only thing
telling a reader where one ends and the next begins — and running paragraphs
together, which is what the type scale did until now, made body text a slab.
One em, and none on the paragraph that opens a story.

---

## Open questions

Tracked as issues, so there is one place to update rather than two:

- [#17](https://github.com/itsmeduncan/rsspaper/issues/17) — body weight on a
  reflective panel. Literata Regular may render thin; Medium is a one-line
  change in `faces.cpp`. Needs hardware, not a monitor.
- [#13](https://github.com/itsmeduncan/rsspaper/issues/13) — hyphenation is
  still a stub interface. Ragged-right is the default precisely because of it.
- [#12](https://github.com/itsmeduncan/rsspaper/issues/12) — `max_items` is
  spent in section order, so a low ceiling can drop a whole trailing section.
- [#7](https://github.com/itsmeduncan/rsspaper/issues/7) — the refresh policy
  exists but `partial_turns_before_full` is a guess until it can be measured.

The full list is the
[1.0.0 milestone](https://github.com/itsmeduncan/rsspaper/milestone/1).
