# Streaming edition — Stage A: the v5 format library — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** A portable, host-tested streaming edition format (v5): a writer that appends one story at a time to a byte sink (never holding the whole edition), and a reader that parses the index and loads one story's pages at a time. No device or compose changes yet — this is the foundation Stages B (streaming compose) and C (lazy read) build on.

**Architecture:** Introduce a `ByteSink` write-abstraction mirroring the existing `ByteSource`. Refactor the existing `serialize_edition` page/story encoding to write through a sink. Add `StreamingEditionWriter` (header → per-story pages, tracking offsets → index → footer) and `StreamingEditionReader` (footer → index → per-story page load). All in `src/core/`, all host-tested. `serialize_edition`/`deserialize_edition` (v4, whole-blob) stay for the desktop and as the fallback.

**Tech Stack:** Portable C++17, doctest. No HAL. The format is little-endian, matching `edition_store.cpp`'s `put_*`/`Reader` primitives.

**Spec:** `docs/superpowers/specs/2026-08-21-streaming-edition-for-large-papers-design.md`

## Global Constraints

- **`src/core/` includes no platform header.** ByteSink is a pure interface; the device/sim back it later (Stage B/C).
- **Little-endian, and reuse the existing primitives.** The per-page byte encoding must match what `edition_store.cpp` already writes for a page, so v5 story-pages and v4 pages share the same page codec — extract it, don't fork it.
- **A stale/truncated/newer file degrades, never crashes** (the invariant every reader here already holds): the reader returns false/empty, never aborts.
- **Run tests from the repo root.** `make check` is the gate.
- **Commit messages:** imperative subject; end with the `Co-Authored-By` / `Claude-Session` trailers.

## The v5 format

```
header:  u32 magic('RSPE'), u16 version(5), u16 flags(0),
         i64 date, str title, 6×u32 stats
stories: page-blocks concatenated (each = the existing per-page encoding)
index:   u32 story_count, then per story:
           u64 key, str title, str section, str source, i64 published,
           u8 truncated, u32 page_count, u32 byte_offset, u32 byte_length
footer:  u32 index_offset, u32 magic('RSPE')
```

The index carries no page text, so it is cheap to hold resident; a story's pages live at `byte_offset` and are read only when needed. `byte_offset`/`byte_length` are relative to the start of the file.

---

## Task 1: `ByteSink` and a page codec through it

Introduce the write-side abstraction and make the existing page encoding write through it, so one codec serves both v4 (string) and v5 (streaming).

**Files:**

- Create: `src/core/io/byte_sink.h`
- Modify: `src/core/edition/edition_store.cpp` (route `put_*` and page encoding through a sink; keep `serialize_edition` working via a string-backed sink)
- Test: `test/byte_sink_test.cpp` (new)

**Interfaces:**

- Produces:
  ```cpp
  // src/core/io/byte_sink.h
  namespace diarium {
  struct ByteSink {
    virtual ~ByteSink() = default;
    // Append n bytes. Returns false once the sink has failed; a failed sink
    // stays failed so callers can check once at the end.
    virtual bool write(const void* data, size_t n) = 0;
    // Bytes successfully written so far — the writer needs this for offsets.
    virtual size_t position() const = 0;
    virtual bool ok() const = 0;
  };
  // A sink that appends to a std::string: the desktop path and the test double.
  class StringSink final : public ByteSink {
   public:
    explicit StringSink(std::string* out) : out_(out) {}
    bool write(const void* data, size_t n) override {
      out_->append(static_cast<const char*>(data), n); pos_ += n; return true;
    }
    size_t position() const override { return pos_; }
    bool ok() const override { return true; }
   private:
    std::string* out_;
    size_t pos_ = 0;
  };
  }  // namespace diarium
  ```
- The `put_u8/u16/u32/i32/i64/str` helpers gain sink overloads (or become free functions taking `ByteSink&`), and the per-page encoding in `serialize_edition` moves into `void write_page(ByteSink&, const Page&)` reused by both paths.

- [ ] **Step 1: Write the failing test** (`test/byte_sink_test.cpp`)

```cpp
#include <string>
#include "core/io/byte_sink.h"
#include "doctest.h"
using namespace diarium;

TEST_CASE("a string sink appends and tracks position") {
  std::string out;
  StringSink sink(&out);
  CHECK(sink.position() == 0);
  REQUIRE(sink.write("abc", 3));
  CHECK(sink.position() == 3);
  REQUIRE(sink.write("de", 2));
  CHECK(out == "abcde");
  CHECK(sink.position() == 5);
  CHECK(sink.ok());
}
```

- [ ] **Step 2: Run it, watch it fail** — `make tests` fails: `core/io/byte_sink.h` not found.
- [ ] **Step 3: Write `byte_sink.h`** as above.
- [ ] **Step 4: Refactor `edition_store.cpp`** so the byte primitives and page encoding write through a `ByteSink`. Add sink-taking `put_*` (keep the string ones or make them thin wrappers over a `StringSink`), and extract `write_page(ByteSink&, const Page&)` from the current inline page loop in `serialize_edition`. `serialize_edition` becomes: make a `std::string`, wrap in `StringSink`, write header + each page via `write_page`, return the string. Its output bytes must be **identical** to before (verify: the existing `edition_store_test` round-trip still passes).
- [ ] **Step 5: Run tests** — `make check`: the new byte-sink test passes and every existing `edition_store` test still passes (identical bytes).
- [ ] **Step 6: Commit** — `git add src/core/io/byte_sink.h src/core/edition/edition_store.cpp test/byte_sink_test.cpp && git commit -m "Add ByteSink, and route the page codec through it"`

---

## Task 2: `StreamingEditionWriter`

Write a v5 edition to a sink, one story at a time, tracking offsets and emitting the index + footer at the end.

**Files:**

- Create: `src/core/edition/edition_stream.h`, `edition_stream.cpp`
- Test: `test/edition_stream_test.cpp` (new)

**Interfaces:**

- Consumes: `ByteSink` (Task 1), `write_page` (Task 1), `Edition`/`Page`/`StoryRef` (`edition.h`).
- Produces:

  ```cpp
  // edition_stream.h
  constexpr uint16_t kStreamEditionVersion = 5;
  class StreamingEditionWriter {
   public:
    // Writes the header immediately.
    StreamingEditionWriter(ByteSink& sink, Epoch date, const std::string& title,
                           const ComposeStats& stats);
    // Appends one story's pages and records its index entry. `meta.page_count`
    // must equal pages.size(); first_page/byte fields are filled by the writer.
    void add_story(const StoryRef& meta, const std::vector<Page>& pages);
    // Writes the index and footer. After this the sink holds a complete v5
    // file. Returns false if the sink failed at any point.
    bool finish();
  };
  ```

- [ ] **Step 1: Write the failing test** — build a small `Edition` (2–3 stories, hand-made pages), write it story-by-story through a `StringSink`, and assert the bytes start with the v5 magic+version and that `position()` grew by each story. Full round-trip is Task 3; here assert the writer runs and frames correctly.

```cpp
#include "core/edition/edition_stream.h"
#include "core/io/byte_sink.h"
#include "doctest.h"
using namespace diarium;
// (helper: make a Page with one line/run; make a StoryRef with page_count=N)
TEST_CASE("the streaming writer frames a v5 file") {
  std::string out; StringSink sink(&out);
  ComposeStats stats;
  StreamingEditionWriter w(sink, 1786864000, "Diarium", stats);
  CHECK(sink.position() > 0);  // header written up front
  // add two stories, each with a couple of pages (helpers below)
  w.add_story(story_meta("A", 2), two_pages());
  const size_t after_first = sink.position();
  w.add_story(story_meta("B", 1), one_page());
  CHECK(sink.position() > after_first);
  REQUIRE(w.finish());
  // v5 magic + version at the front
  CHECK(static_cast<uint8_t>(out[0]) == 0x52);  // 'R' little-endian of 'RSPE'
  // footer magic at the very end
  CHECK(out.size() > 8);
}
```

- [ ] **Step 2: Run, watch it fail** (`edition_stream.h` missing).
- [ ] **Step 3: Implement `edition_stream.{h,cpp}`** — the writer records, per `add_story`, `byte_offset = sink.position()` before writing the story's pages, writes each page via `write_page`, records `byte_length = sink.position() - byte_offset`, and stashes the index entry (a copy of `meta` with the offsets). `finish()` records `index_offset = sink.position()`, writes `story_count` + each index entry, then the footer (`index_offset`, magic). Returns `sink.ok()`.
- [ ] **Step 4: Run tests** — the framing test passes; `make check` green.
- [ ] **Step 5: Commit** — `git commit -m "StreamingEditionWriter: append stories, then an index"`

---

## Task 3: `StreamingEditionReader` + round-trip

Parse a v5 file: read the footer, then the index (resident, cheap), then load one story's pages on request. For Stage A the source is a whole `std::string`; Stage C swaps in a seekable device source.

**Files:**

- Modify: `src/core/edition/edition_stream.h`, `edition_stream.cpp`
- Modify: `test/edition_stream_test.cpp`

**Interfaces:**

- Produces:
  ```cpp
  struct StreamIndexEntry {
    StoryRef ref;          // key,title,section,source,published,truncated,page_count
    uint32_t byte_offset;  // start of this story's pages in the file
    uint32_t byte_length;
  };
  class StreamingEditionReader {
   public:
    // Parses header + footer + index from the whole file. Returns false and
    // sets error on truncation/corruption/newer-version — never throws.
    bool open(const std::string& file, std::string* error);
    Epoch date() const; const std::string& title() const;
    const ComposeStats& stats() const;
    const std::vector<StreamIndexEntry>& index() const;
    // Deserialise story i's pages from its byte range. Empty on any error.
    std::vector<Page> load_story_pages(size_t i) const;
  };
  ```
- Consumes: the existing per-page decode from `edition_store.cpp` — extract `bool read_page(Reader&, Page*)` there (mirror of `write_page`) and reuse it here so v4 and v5 share the page codec on the read side too.

- [ ] **Step 1: Write the failing round-trip test** — build an `Edition`, write it via `StreamingEditionWriter` to a `StringSink`, then `StreamingEditionReader::open` the bytes and assert: index size == story count; each entry's `ref` fields match; `load_story_pages(i)` returns pages equal (line/run text) to the originals; sum of `page_count` over the index equals the total pages written. Add a truncation case: `open` on a chopped file returns false, not a crash.
- [ ] **Step 2: Run, watch fail.**
- [ ] **Step 3: Implement the reader** — extract `read_page` in `edition_store.cpp` (shared codec), then `open()`: validate size ≥ footer, read footer magic + `index_offset`, seek (index into the string) to `index_offset`, read the index; validate every `byte_offset+byte_length ≤ index_offset`. `load_story_pages(i)`: construct a `Reader` over `file.substr(byte_offset, byte_length)` and decode `page_count` pages via `read_page`.
- [ ] **Step 4: Run tests** — round-trip + truncation pass; `make check` green (v4 tests still pass — shared `read_page` unchanged in behavior).
- [ ] **Step 5: Commit** — `git commit -m "StreamingEditionReader: index, then one story's pages at a time"`

---

## Task 4: An `Edition`↔stream convenience + a memory-shape test

Prove the format serves the end goal: a helper to write a whole `Edition` via the streaming writer (what Stage B's compose will call per-story instead), and a test asserting the reader never needs the whole file's page data at once.

**Files:**

- Modify: `edition_stream.h`, `edition_stream.cpp`, `test/edition_stream_test.cpp`

**Interfaces:**

- Produces: `bool write_edition_streaming(ByteSink&, const Edition&);` — iterates `edition.stories`, and for each writes `edition.pages[first_page .. first_page+page_count)` via `add_story`. (Stage B replaces this with per-story pagination; here it bridges the existing `Edition`.)

- [ ] **Step 1: Failing test** — `write_edition_streaming` a fixture `Edition` (reuse `edition_test.cpp`'s `two_sections()` composed edition) to a `StringSink`; open with the reader; assert every story round-trips and `index().size() == edition.stories.size()`. Assert `load_story_pages(0)` returns only story 0's pages (size == stories[0].page_count), demonstrating one-story access.
- [ ] **Step 2: Run, fail. Step 3: Implement `write_edition_streaming`. Step 4: `make check` green. Step 5: Commit** — `git commit -m "Bridge an Edition to the streaming writer; prove one-story reads"`

---

## Self-review

**Spec coverage (Stage A slice):** v5 format ✓ (Tasks 2–3); streaming writer ✓ (Task 2); index-then-lazy story read ✓ (Task 3); shared page codec with v4 ✓ (Tasks 1, 3). Out of Stage A: the streaming HAL primitive, streaming compose (Stage B), device lazy read (Stage C) — each its own plan.

**Placeholder scan:** none — signatures and test shapes are concrete; the page-codec reuse points at the exact functions to extract (`serialize_edition`'s page loop → `write_page`; the decode → `read_page`).

**Type consistency:** `ByteSink` (Task 1) is consumed by the writer (Task 2); `StreamIndexEntry`/`StreamingEditionReader` (Task 3) consume the writer's output; `write_edition_streaming` (Task 4) uses both. `kStreamEditionVersion = 5` is distinct from `kEditionVersion = 4`.

**Note for the executor:** keep `serialize_edition`/`deserialize_edition` (v4) working throughout — they are the desktop path and the device's current format until Stage B/C switch the device over. Task 1's refactor must produce byte-identical v4 output (guarded by the existing `edition_store_test` round-trip).
