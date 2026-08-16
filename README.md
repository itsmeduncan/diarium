# RSSpaper

**A newspaper, not a feed.**

RSSpaper is an e-ink RSS reader that behaves like a morning paper. It wakes up,
fetches your feeds, composes a finite edition with a front page and sections,
and then it is done. You read it, you reach the end, you put it down.

You flip through ledes — a dozen pages for forty stories — and open the ones
you want in full, then come back. The paper has a last page.

There is no infinite scroll. There are no unread counts, no badges, no
notifications, no engagement mechanics, and no algorithmic curation. Stories
run in reverse-chronological order within their section, which is the whole of
the ranking logic and always will be.

**Everything runs on the device.** There is no server, no account, and no
companion app. The only network requests RSSpaper ever makes are to the feed
URLs you configured. Nothing phones home, because there is no home to phone.

## Status

Early. The desktop simulator and the parsing/layout core are being built first,
because the page is the product and the page needs to be looked at. Hardware
support follows — tracked on the
[1.0.0 milestone](https://github.com/itsmeduncan/rsspaper/milestone/1).

| Piece                             | State                                             |
| --------------------------------- | ------------------------------------------------- |
| Streaming XML parser              | working                                           |
| RSS 2.0 / RSS 1.0 / Atom parser   | working, exercised against 12 real feeds          |
| HTML → block model                | working                                           |
| Type + layout engine              | working — widow/orphan control, two optical sizes |
| Edition composer                  | working — front page, sections, dedup across days |
| Desktop simulator (PNG output)    | working — 8-bit, 3-bit and 1-bit dithered         |
| HAL (six interfaces)              | working — simulator implements all six            |
| Reader UI (gestures, page turns)  | working — open a story, come back, jump sections  |
| Hyphenation (Liang/TeX patterns)  | working — justified text is now viable            |
| OPML import                       | working                                           |
| Edition persistence               | working — compose once, read from storage         |
| Clippings                         | working — fold a corner, it survives the edition  |
| HTTP fetcher with conditional GET | not started                                       |
| Inkplate 6FLICK target            | not started                                       |

## Hardware

The first target is the [Soldered Inkplate
6FLICK](https://soldered.com/product/inkplate-6flick/): ESP32, 8 MB PSRAM, a 6"
1024×758 panel with ~225 ms 1-bit partial refresh and ~1.26 s 3-bit greyscale
full refresh, capacitive touch, a 64-step frontlight, and ~23 µA in deep sleep.

Nothing above the HAL includes an Inkplate header. Porting to another
ESP32-class e-ink board means implementing `src/hal/hal.h` — display, touch,
clock, power, storage, network — and nothing else.

## Building

The desktop simulator runs the same pipeline the device runs, renders pages to
PNG at the panel's exact 1024×758 geometry, and fakes touch from the keyboard.

```sh
make            # simulator, tests and the font pack
make check      # run the unit tests
make edition    # compose an edition from the fixture feeds into out/
```

`make` needs nothing but a C++17 compiler. If you have CMake, `cmake -B build
&& cmake --build build` builds the same targets and is what CI uses.

Inspect what the parser makes of a feed:

```sh
./bin/rsspaper-sim parse --verbose test/fixtures/feeds/*.xml
```

See the page as the panel will actually show it, rather than as an 8-bit
render — thin serifs and hairline rules survive one reduction and not the
other:

```sh
./bin/rsspaper-sim compose --depth mono1 --pages 4   # 1-bit, Atkinson dithered
./bin/rsspaper-sim compose --depth grey3             # the full-refresh mode
```

`compose` saves the edition it built, and `read` loads it rather than
composing again — which is the whole reason a page turn is cheap on a battery:

```sh
./bin/rsspaper-sim compose --fresh        # writes out/edition.rspe
./bin/rsspaper-sim read                   # "no re-parse, no re-layout"
./bin/rsspaper-sim read --recompose       # ignore it and build a fresh one
```

By default `compose` writes only the pages you flip through. `--all-pages`
writes the story text behind them too, and `--index` prints the navigation
map — every lede's page, its tap region, and the pages its story occupies:

```sh
./bin/rsspaper-sim compose --fresh --index --all-pages
```

Read the paper. The keyboard stands in for the panel — a keystroke becomes a
synthesised touch, through the same gesture recogniser and the same `Reader`
the device will run — and every refresh writes a frame:

```sh
make read
#  n  turn the page      1-9  open the Nth story here
#  b  back               s    section list
#  h  fold this corner   c    clippings
```

It reports what the panel would have spent: partial refreshes are ~225 ms,
full ones ~1.26 s, and the ratio is a design decision you can see.

## Configuration

Feeds live in a single `config/feeds.toml` on device storage — a URL, a section
name, and how many items to take:

```toml
[edition]
title = "RSSpaper"      # the nameplate across the top of page one
max_items = 40          # a paper that never ends is a feed

[[feed]]
url     = "https://daringfireball.net/feeds/main"
section = "Technology"
max_items = 6
```

Already have a subscription list? Import it. Folders become sections, and
nesting collapses to the outermost folder — a reader's sub-folders are
refinements, not sections:

```sh
./bin/rsspaper-sim opml subscriptions.opml --out config/feeds.toml
```

## Non-goals

No server component. No EPUB or OPDS. No read-later integrations. No AI. No
full-text extraction of truncated feeds in v1 — RSSpaper renders what the feed
publishes, and records per item whether the publisher looked like they held
something back, so the value of a v2 readability extractor can be measured
rather than guessed.

## Repository layout

```
src/core/               portable C++17 — no Arduino, compiles on desktop
  base/                 strings, UTF-8, dates
  io/                   ByteSource: the one input abstraction
  xml/                  streaming pull parser, entities
  feed/                 RSS/Atom → items
  html/                 HTML → block model
  text/                 font pack, glyph metrics, fallbacks
  layout/               line breaking, pagination, the type scale
  render/               framebuffer and page drawing
  edition/              composer, seen-store dedup
  config/               feeds.toml
  ui/                   gesture recognition, the reader
src/hal/                hardware abstraction — the only portability seam
src/sim/                desktop harness: PNG output, keyboard "touch"
tools/fontgen/          TTF → runtime font pack, including GPOS kerning
tools/hyphgen/          TeX hyphenation patterns → a searchable table
tools/check-portability.sh   CI gate on the seam above
assets/fonts/           Literata, and the pack is built from it
assets/hyphenation/     Liang patterns for American English
test/                   unit tests and a corpus of real feeds
```

`src/device/` — the Inkplate target — does not exist yet; see the milestone
below. Dithering to the panel's bit depths currently lives in `src/sim/`,
because so far only the simulator needs it; it moves into `src/core/render/`
when the device does.

## Licence

MIT. See [LICENSE](LICENSE).

Bundled third-party material keeps its own terms:
[Literata](assets/fonts/) (SIL Open Font License 1.1),
[hyphenation patterns](assets/hyphenation/) (all-permissive, notice
preserved), [stb](third_party/stb/) (public domain) and
[doctest](third_party/doctest/) (MIT).
