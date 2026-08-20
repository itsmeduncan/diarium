# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository.

## What this is

RSSpaper is an on-device e-ink RSS reader that composes a bounded newspaper
edition. No server, no accounts, no cloud, no AI curation. Read `README.md` for
the product thesis and `CONTRIBUTING.md` for what is deliberately out of scope
— features that contradict the thesis are rejected even when they're good
ideas.

Target hardware is the Soldered Inkplate 6FLICK (ESP32, 8 MB PSRAM, 1024×758
panel at ~212 PPI). The desktop simulator runs the identical pipeline.

## Commands

```sh
make sim                  # bin/rsspaper-sim
make tests && make check  # build and run the unit tests
make fonts                # build/literata.rfp from assets/fonts/*.ttf
make edition              # compose from fixtures into out/ as PNGs
make read                 # drive the reader from the keyboard
make portability          # the no-platform-headers gate (also part of check)
make clean

make device               # build the firmware (needs PlatformIO)
make device-flash         # build it and put it on the board
make device-log           # watch what it says
make device-ls            # what is on the card
make device-put FILE=config/feeds.local.toml DEST=/feeds.toml
make device-compose       # fetch and compose now, not at wake_at
make help                 # all of the above, listed

cmake -B build && cmake --build build   # same targets; what CI runs
ctest --test-dir build --output-on-failure
```

Run a single test case (doctest):

```sh
./bin/rsspaper-tests --test-case="*truncation*"
./bin/rsspaper-tests --list-test-cases
```

An edition is composed once and read many times; `compose` saves it and `read`
loads it. `--recompose` skips the cache, `--index` prints the lede-to-story
navigation map, `--all-pages` writes the story text as well as the pages you
flip through.

Inspect the parser's view of a feed — the fastest way to diagnose a feed bug:

```sh
./bin/rsspaper-sim parse --verbose --max 5 test/fixtures/feeds/theverge.atom.xml
./bin/rsspaper-sim opml test/fixtures/opml/subscriptions.opml
```

Tests read `test/fixtures/` by relative path, so **run them from the repository
root**. CMake's `ctest` sets the working directory; the raw binary does not.

Two build systems intentionally: CMake is authoritative and runs in CI, the
Makefile exists so a clone builds with only a compiler. Both glob the same
source trees, so adding a file needs no build edit. Keep them in sync.

## Architecture

Data flows in one direction, and each stage is independently testable:

```
ByteSource → XmlPullParser → parse_feed → Item{Block…} → layout → framebuffer → HAL display
             (pull)          (streaming)   (no HTML)      (pages)   (greyscale)
```

An edition has two kinds of page. Pages `[0, browse_page_count)` are the ledes;
everything after is story text (`StoryRef`, `Edition::story_at`). Keep that
split — a linear edition of the same content ran to 175 pages.

Reading is a single pass rather than a tree. You land on a contents page —
drawn by `core/ui/contents.h`, not composed, because it is a view of what is
left rather than a fact about the edition — and swipe onward through every
unread story, oldest first (`Edition::reading_order`), until there are none.
Each is marked read on arrival; what you did not reach is still in tomorrow's
paper, because the composer dedups against what was *read* rather than what it
printed.

**`src/core/` is portable C++17 and must never include a platform header.**
That constraint is what lets the whole pipeline run on a laptop, and it is the
single most important rule in the repo. Hardware lives behind `src/hal/hal.h`;
`src/sim/` implements it with PNG output and keyboard-synthesised touch,
`src/device/` with the Inkplate library.

Key boundaries worth understanding before changing anything:

- **`core/xml/xml_pull.h`** is a _pull_ parser over a `ByteSource`. The feed
  parser drives it, which is what makes "take 3 items from a 772 KB feed and
  stop reading" work. It is deliberately tolerant: mismatched tags, unknown
  entities and mislabelled encodings recover rather than fail.
- **`core/html/html_to_blocks.h`** is a _push_ parser, because by the time an
  article body is being converted we are already inside the XML pull loop. It
  accepts chunks split anywhere, including mid-tag and mid-entity.
- **`core/html/block.h`** is where HTML dies. Nothing downstream knows what a
  `<div>` is. `StyleRun`s are byte offsets into `Block::text` and must always
  satisfy `start + length <= text.size()` — two separate off-by-one bugs have
  already been fixed here, and `corpus_test.cpp` guards it.
- **`core/feed/feed_parser.cpp`** keys on element names rather than branching
  on RSS-vs-Atom, and checks the namespace prefix so `media:content` and
  `itunes:summary` never reach the article body.
- **`core/text/faces.h`** is the type palette, shared by `tools/fontgen` and
  the runtime so a pack can't disagree with the code that reads it.
  `fallback_codepoint` decides what happens to a character the face lacks:
  substitute, drop, or tofu. Everything routes through `Face::resolve`, so
  measuring and drawing can't disagree about what a glyph costs.
- **`core/net/`** is the HTTP the device speaks: an incremental head parser, a
  body source that hides chunked from the feed parser, URL resolution for
  relative redirects, and the per-feed validator cache that makes most
  mornings a handshake instead of a download. All portable, because the things
  that break against real servers are testable on a laptop.
- **`core/html/readability.h`** finds the article inside a whole page, for the
  publishers that truncate. There is no DOM to score — `html_to_blocks` is a
  push parser that discards structure so it can run over a socket — so it
  scores the block sequence instead and takes the best contiguous run.
- **`core/ui/`** is the top of the portable stack. `gesture.h` turns touch
  points into intentions — note that release coordinates are ignored, because
  a released panel reports none — and `reader.h` is the only thing in
  `src/core/` allowed to drive the HAL.

### Invariants

- **Nothing scales with input size.** Every buffer touching feed data has a cap
  in an `XmlLimits` / `HtmlLimits` / `FeedParseOptions` struct. Add a limit
  alongside any new buffer.
- **Parsers recover, they never abort.** A malformed feed costs one story.
- **Sizes are physical.** The panel is ~212 PPI; 27 px body text is ~9.2 pt.
  Choosing sizes that look right on a monitor produces large print on device.
- **No platform headers above `src/hal/`.** `tools/check-portability.sh`
  enforces it, runs in `make check`, and gates CI.

## Conventions

- Record non-obvious decisions in `DECISIONS.md`, with the reasoning.
- **Ask before adding any third-party dependency.** The vendored set is
  `stb_truetype`, `stb_image_write` (public domain), `doctest` (MIT), and
  Literata (SIL OFL). Everything else is written here.
- Fixtures in `test/fixtures/feeds/` are real feeds kept unmodified — the
  malformations are the point. Add one only when it breaks the parser in a way
  nothing else does, and document that in the fixtures README.
- Licensed MIT.
