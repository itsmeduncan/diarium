# Streaming edition — Stages B & C: streaming compose + lazy read — Implementation Plan

**Goal:** Make the device compose and read editions of hundreds of stories without holding the whole edition in RAM — by streaming the compose to the card one story at a time (B) and reading pages back from the card on demand (C). Builds directly on Stage A's v5 `StreamingEditionWriter`/`StreamingEditionReader`/`ByteSink` (already on this branch, PR #41).

**Architecture:** Extend `IStorage` with two primitives — a streaming write sink (`open_write`) and ranged reads (`read_range` + `size`). The composer is restructured to select on lightweight item metadata, then paginate and append one story at a time to a card-backed `ByteSink`. The reader opens an edition by reading only its footer + index (small, resident) via ranged reads, and loads a story's pages on demand. Tasks 1–4 are host-testable through the sim's file-backed storage; Task 5 is the device wiring + on-device verification (done by the human operator with hardware).

**Tech Stack:** Portable C++17 core; `src/sim` (file-backed storage for host tests) and `src/device` (SdFat) HAL impls; doctest.

**Spec:** `docs/design/specs/streaming-edition-for-large-papers.md`
**Prior stage:** `docs/design/plans/streaming-edition-stage-a.md` (the v5 format library it builds on)

## Global Constraints

- **`src/core/` includes no platform header.** The HAL additions are pure virtuals on `IStorage` (`src/hal/hal.h`); the backings live in `src/sim` and `src/device`.
- **Everything degrades, never crashes.** A missing/corrupt/newer edition file → the reader shows "compose a fresh edition", never aborts. The composer, if a story fails to paginate, drops it and continues.
- **The desktop is where the logic is proven; the device is the only place the memory is proven.** Tasks 1–4 must pass `make check` on host; Task 5's success criterion is an on-device compose of a deliberately large paper that previously aborted, now saving and reading.
- **Keep the v4 path as the fallback** until Task 5 switches the device to v5, and keep the safe `max_pages` budget as a backstop.
- Little-endian; reuse Stage A's writer/reader. Commit messages: imperative subject.

---

## Task 1: HAL streaming-write sink and ranged reads

Add the storage primitives the writer and lazy reader need, backed by the sim (host-testable) and device.

**Files:**

- Modify: `src/hal/hal.h` (`IStorage`)
- Modify: `src/sim/sim_hal.h`, `sim_hal.cpp` (implement)
- Modify: `src/device/device_storage.h`, `device_storage.cpp` (implement)
- Test: `test/storage_stream_test.cpp` (new; exercises the sim impl)

**Interfaces:**

- Produces, on `IStorage`:
  ```cpp
  // Open a path for streaming writes. Returns null on failure. Writing goes
  // through the returned ByteSink; destroying it closes the file.
  virtual std::unique_ptr<ByteSink> open_write(const std::string& path) = 0;
  // Total size of a file, or 0 if absent.
  virtual size_t size(const std::string& path) = 0;
  // Read [offset, offset+length) into out. False if the range is out of bounds
  // or the file is absent. Reads no more than the file holds.
  virtual bool read_range(const std::string& path, size_t offset,
                          size_t length, std::string* out) = 0;
  ```
- `hal.h` will need `#include <memory>` and to see `ByteSink` (include `core/io/byte_sink.h`).

- [ ] **Step 1: Failing test** (`test/storage_stream_test.cpp`) against `SimStorage` (construct one over a temp dir): `open_write` a file, write it in chunks through the sink, destroy the sink; then `size()` equals the total, `read_range(mid)` returns the right bytes, and an out-of-range `read_range` returns false. Match how existing sim tests construct `SimStorage` (see `sim_hal.h:102`).
- [ ] **Step 2: Run, fail** (methods missing).
- [ ] **Step 3: Implement.** `SimStorage`: `open_write` returns a `ByteSink` that appends to a `std::ofstream` (or buffers then writes on close); `size`/`read_range` use `std::ifstream` seek/read. `DeviceStorage`: `open_write` returns a `ByteSink` holding an `FsFile` opened `O_WRITE|O_CREAT|O_TRUNC`, writing chunks straight through (this is the whole point — no whole-file buffer); `size`/`read_range` open `O_READ`, `seekSet(offset)`, `read(len)`. Add a `FileByteSink` type where each backing lives.
- [ ] **Step 4: `make check` green.**
- [ ] **Step 5: Commit** — `git commit -m "IStorage: streaming write sink and ranged reads"`

---

## Task 2: Streaming compose (Stage B)

Restructure the compose so nothing bigger than one story is resident, writing v5 straight to the card.

**Files:**

- Modify: `src/core/edition/edition.h`/`edition.cpp` (a compose that emits stories to a callback/sink instead of building `Edition::pages`)
- Modify: `src/core/edition/edition_stream.{h,cpp}` if the writer needs a stats-finalize tweak
- Modify: `src/device/compose.cpp` (`compose_from_card` → stream to `storage->open_write("/edition.rspe")`)
- Modify: `src/sim/cmd_compose.cpp` (compose to a v5 file so the path is host-exercised)
- Test: `test/edition_test.cpp` (a streaming-compose test)

**Design:** Introduce `compose_streaming(sections, fonts, opts, StreamingEditionWriter& writer, ComposeStats* stats)`:

1. Do the existing select/stale/sort/`max_items` budget on item **metadata** (this already operates on `Item`s without needing their pages laid out — keep it).
2. For each selected story in order: build its `FlowElement`s, paginate **just that story** into a local `std::vector<Page>`, fill a `StoryRef`, `writer.add_story(ref, pages)`, and let the local pages free before the next.
3. The existing `compose_edition` (whole-`Edition`) stays for the desktop/tests and for the v4 fallback, ideally re-expressed in terms of the same per-story helper to avoid two layout paths.

- [ ] **Step 1: Failing host test** — compose `two_sections()` (from `edition_test.cpp`) with `compose_streaming` into a `StringSink` writer; open the bytes with `StreamingEditionReader`; assert the same stories/pages as `compose_edition` would produce (same count, same first story's page text). Assert `stats.items_published` matches.
- [ ] **Step 2: Run, fail. Step 3: Implement `compose_streaming`** (extract the per-story pagination shared with `compose_edition`). Keep `max_pages` as a page-budget backstop on the streamed count.
- [ ] **Step 4:** Wire `src/sim/cmd_compose.cpp` to compose via the streaming path to `out/edition.rspe` (v5), and `make edition` still produces a readable edition (the sim reader in the next task, or a temporary check).
- [ ] **Step 5:** `make check` green. **Commit** — `git commit -m "Compose one story at a time, straight to the card"`

---

## Task 3: A lazy StreamingEditionReader over ranged reads (Stage C, core)

Give the Stage-A reader a mode that never holds the whole file: it reads the footer and index via ranged reads and loads a story's pages on demand.

**Files:**

- Modify: `src/core/edition/edition_stream.{h,cpp}`
- Test: `test/edition_stream_test.cpp`

**Interfaces:**

- Produces: a reader that opens from an `IStorage`-like ranged source rather than a whole string. To keep core HAL-free, define a tiny source interface it reads through:

  ```cpp
  struct RangedSource {
    virtual ~RangedSource() = default;
    virtual size_t size() const = 0;
    virtual bool read(size_t offset, size_t length, std::string* out) const = 0;
  };
  bool StreamingEditionReader::open(const RangedSource& src, std::string* error);
  ```

  `open` reads the last `kFooterSize` bytes (footer → index_offset), then the index range, then validates — holding only header+index resident. `load_story_pages(i)` issues one `src.read(byte_offset, byte_length)` and decodes. The existing whole-string `open` can wrap a string in a `StringRangedSource`, so both the string and ranged paths share one implementation.

- [ ] **Step 1: Failing test** — write an edition to a `std::string`, wrap it in a `StringRangedSource`, `open` via the ranged path; assert index + `load_story_pages(i)` match, and (the point) that `open` issued only footer + index reads, not a whole-file read. (Instrument `StringRangedSource` to count bytes read, and assert it read far less than the file size.)
- [ ] **Step 2: fail. Step 3: implement** the ranged `open` + `RangedSource`; refactor the string `open` to go through it. Step 4: `make check` green. Step 5: **Commit** — `git commit -m "Lazy edition reader: footer + index, then one story at a time"`

---

## Task 4: Lazy reader in the pass (Stage C, core)

Make `Reader` hold the index and one story's pages, loading the current story from a `StreamingEditionReader` as the pass moves, instead of a whole resident `Edition`.

**Files:**

- Modify: `src/core/ui/reader.h`/`reader.cpp`
- Modify: `src/sim/cmd_read.cpp` (drive the reader from a v5 file via a `RangedSource` over the sim storage)
- Test: `test/reader_test.cpp`

**Design:** The reader gains a `StreamingEditionReader*` (or owns one over a `RangedSource`). It keeps the story index (`reading_order()` works on it), `read_` state, and the current story's pages (`std::vector<Page> current_pages_`). `show_article_at` loads that story's pages via `load_story_pages(order_[pos])` and renders `current_pages_[article_page_]`. `render` for `Home` still draws the dashboard from the index (title/section/unread per story — all in the index). Keep a same-signature constructor path for the existing whole-`Edition` tests, or migrate them.

- [ ] **Step 1: Failing test** — compose a v5 edition (Task 2) to a string, open it lazily (Task 3), drive a `Reader` over it: wake on Home; swipe-right into the oldest unread; swipe-down advances within the story; swipe-right moves to the next; assert positions and that only the current story's pages are resident (via the read-counter). Reuse `reader_test.cpp` fixtures, adapted to the streaming source.
- [ ] **Step 2: fail. Step 3: implement** the lazy page access. Step 4: `make check` green (adapt/replace the whole-`Edition` reader tests). Step 5: **Commit** — `git commit -m "Reader holds the index and one story, not the whole paper"`

---

## Task 5: Device wiring + on-device verification (human-operated)

Switch the device to the streaming write and lazy read, and prove the memory on hardware. **This task needs the physical board and must be run by hand, not automated.**

**Files:**

- Modify: `src/device/main.cpp` (`compose_wake` → `compose_streaming` to `storage.open_write`; `load_paper`/read path → lazy `StreamingEditionReader` over a `DeviceStorage` `RangedSource`)
- Modify: `src/device/compose.cpp` if the fetch/collect stage still holds all items (it collects item metadata + full article blocks; ensure only metadata is held across stories, and each story's blocks are loaded and freed within the per-story loop — this is the other half of the memory win)

- [ ] **Step 1:** Wire `compose_wake` to stream via `open_write`, and the read path to the lazy reader. Bump the on-wire format handling so a v4 file degrades to recompose. Raise/remove the device `max_pages` backstop once streaming removes the resident-edition wall (keep a generous sanity cap).
- [ ] **Step 2:** `make device` builds.
- [ ] **Step 3 (device):** flash; set a large cap (e.g. edition 150, per-feed 10); `make device-compose`. Success = **"composed" with a story count well past 44, then "saved"**, no `abort`, no `Memory alloc`. Then a read-wake: **"loaded" and "reading — home"**, no crash, with the internal-RAM headroom that lazy read should now leave.
- [ ] **Step 4:** iterate the cap upward to find the new ceiling; set the user's `feeds.toml` to a good high value.
- [ ] **Step 5:** Commit the device wiring; open the PR for B+C.

---

## Self-review

**Spec coverage:** streaming HAL primitive (Task 1) ✓; streaming compose / one-story-resident (Task 2) ✓; lazy read via footer+index+ranged loads (Tasks 3–4) ✓; device verification that the memory ceiling actually lifts (Task 5) ✓. The per-story-metadata selection reuses the existing `compose_edition` select logic; the per-story pagination is extracted so there is one layout path.

**Placeholder scan:** interfaces and test shapes are concrete; the reused pieces (`compose_edition` select/sort/budget, Stage A writer/reader) are named. Task 5's numeric ceiling is discovered on hardware, not guessed — that is correct, not a placeholder.

**Type consistency:** `ByteSink` (Stage A) is returned by `open_write` (Task 1) and consumed by `StreamingEditionWriter` (Task 2). `RangedSource` (Task 3) is consumed by the lazy `open` and backed in the sim/device by ranged `IStorage` reads (Task 1). `StreamingEditionReader` (Stage A + Task 3) is held by `Reader` (Task 4).

**Note:** Tasks 1–4 are host-testable and can be automated. Task 5 must be run by hand on the physical device — automation cannot flash or measure the board, and this is exactly where "works in the sim, breaks on hardware" has bitten this project before.
