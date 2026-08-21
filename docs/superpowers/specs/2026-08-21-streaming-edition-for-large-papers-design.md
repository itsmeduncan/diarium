# Streaming the edition: composing and reading papers of hundreds of stories

Written 2026-08-21, after a spike established what the ceiling actually is.
The reader wants editions of many more stories than the ~40 the device
reliably composes today. This is the design for getting there.

## What the spike established

The obvious cheap fix does not work. The ESP32's heap sends allocations of
4096 bytes or more to the 4 MB PSRAM and everything smaller to the ~320 KB of
internal RAM (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL 4096`). The edition is
thousands of small allocations, so it piles into internal RAM. Lowering the
threshold at runtime with `heap_caps_malloc_extmem_enable(128)` — after the
radio is down, so mbedTLS keeps its internal RAM during the fetch — was
tried on the board. It did **not** lift the ceiling to hundreds:

- 150-story cap → `abort()` **during compose**, before the "composed N pages"
  line, i.e. while the pipeline was holding every article's text at once.
- 80-story cap → same abort during compose.

The reason is structural, not a threshold to tune: **the whole edition, and
before it every fetched article's full text, is held in RAM at once.** Even
the 4 MB PSRAM cannot hold a few hundred full articles (tens of KB each). The
measured safe point with everything resident is ~145 pages / ~40 stories,
which is what the shipped `max_pages = 120` budget keeps the device under so
it degrades instead of aborting.

To go past that the edition can no longer be resident in full — not while
composing, not while reading. It has to stream.

## The three places that hold everything today

1. **Fetch → collect** (`device/compose.cpp` `compose_from_card`): every feed
   is parsed and every item, with its full article blocks, is collected into
   `sections[].items` before anything is laid out.
2. **Paginate** (`core/edition/edition.cpp` `compose_edition`): every story is
   paginated into one resident `Edition::pages`.
3. **Serialize / read** (`core/edition/edition_store.cpp`): serialize builds
   one contiguous blob; the reader deserializes the whole file into one
   resident `Edition` and holds it for the session.

## The design: one story at a time, to and from the card

The edition becomes a file that is written incrementally as it is composed and
read a story at a time, so nothing bigger than a single story is ever resident.

### 1. A streaming edition file (format v5)

```
header:  magic, version(5), flags, date, title, stats
index:   story_count, then per story:
           key, title, section, source, published, truncated,
           page_count, byte_offset  (where this story's pages begin)
stories: each story's pages, concatenated, in composed order
```

The index is small — metadata and an offset per story, no page text — so the
whole index is cheap to hold resident. The page data for a story lives at its
`byte_offset` and is read only when needed.

### 2. A streaming writer, behind a new HAL primitive

`IStorage` gains an open-for-write / append primitive (today it only takes a
whole `std::string`; `device_storage` already has an `FsFile` under it that
can back this, and the sim backs it with a file). A `StreamingEditionWriter`:
opens the file, reserves the header + index region, then for each story appends
its pages and fills in that story's index entry, and on close writes the index.

### 3. Streaming compose

`compose_from_card` and `compose_edition` are restructured so selection works
on lightweight **item metadata** (title, section, date — no blocks), and only a
chosen story loads its article blocks and is paginated:

1. Parse every feed for item metadata only; drop stale, dedup against read
   state, sort, and apply `max_items` — all on metadata, cheaply.
2. For each selected story, in order: load its article blocks from the card,
   paginate just that story, append its pages to the writer, record its index
   entry, and free its blocks and pages before the next.

Nothing but one story's blocks and pages is ever resident. `max_pages` stays as
a sanity bound but stops being the thing that protects RAM.

### 4. Lazy read

The reader opens the edition by reading the header and index only (small,
always resident). `reading_order()` and the pass work on the index. Rendering a
page loads that story's page data from the card on demand — a `StoryRef` knows
its `byte_offset` and `page_count` — and keeps at most the current story (and
perhaps a one-story lookahead) resident. Page turns cost an SD read of a few
KB, which is negligible against the e-ink refresh.

## What changes

- **New:** `core/edition/edition_stream.{h,cpp}` (writer + reader over the v5
  format), a streaming-write primitive on `hal.h` `IStorage` + `device_storage`
  - sim storage.
- **Restructured:** `compose_edition` / `compose_from_card` into the
  metadata-select-then-per-story-paginate shape; the reader's page access into
  a lazy load; `edition_store` either subsumed by the streaming reader or kept
  for the desktop.
- **Format:** v5, and the read path degrades a v4 file the same safe way it
  degrades any older one (recompose).
- **Tests:** streaming writer/reader round-trip on host; a compose that would
  overflow RAM if resident but streams within a fixed budget; the reader
  rendering from a lazily-loaded story.

## Cost and risk

This is the largest change to the pipeline so far: a new file format, a new HAL
primitive, and a restructuring of both compose and read. It is worth staging —
writer + format first, then streaming compose, then lazy read — each verified
on the device, because the failure mode is a crash on real hardware that the
desktop cannot reproduce (as the orientation and memory bugs already showed).
The desktop stays the place to prove the format and the logic; the board is the
only place to prove the memory.

## Out of scope

Wake time (#2) and images (#4). Streaming compose does touch the fetch path, so
the TLS-reuse work already begun (freeing the feed's session before article
fetches) composes naturally with it.
