# Diarium

**A newspaper, not a feed.**

Diarium is an e-ink RSS reader that behaves like a morning paper. It wakes up,
fetches your feeds, composes a finite edition with a front page and sections,
and then it is done. You read it, you reach the end, you put it down.

You flip through ledes — a dozen pages for forty stories — and open the ones
you want in full, then come back. The paper has a last page.

There is no infinite scroll. There are no badges, no notifications, no
engagement mechanics, and no algorithmic curation. Stories run in
reverse-chronological order within their section, which is the whole of the
ranking logic and always will be.

The paper does remember what you have read, so it does not reprint it and so
that something you did not get to is still there tomorrow. That is the only
use it makes of your reading history: there is no count of what is
outstanding, nothing that accumulates, and nothing that asks to be cleared.

**Everything runs on the device.** There is no server, no account, and no
companion app. The only network requests Diarium ever makes are to the feed
URLs you configured. Nothing phones home, because there is no home to phone.

## Status

Early. The desktop simulator and the parsing/layout core are being built first,
because the page is the product and the page needs to be looked at. Hardware
support follows — tracked on the
[1.0.0 milestone](https://github.com/itsmeduncan/diarium/milestone/1).

| Piece                             | State                                               |
| --------------------------------- | --------------------------------------------------- |
| Streaming XML parser              | working                                             |
| RSS 2.0 / RSS 1.0 / Atom parser   | working, exercised against 12 real feeds            |
| HTML → block model                | working                                             |
| Type + layout engine              | working — widow/orphan control, two optical sizes   |
| Edition composer                  | working — front page, sections, dedup on read state |
| Desktop simulator (PNG output)    | working — 8-bit, 3-bit and 1-bit dithered           |
| HAL (six interfaces)              | working — simulator and device implement all six    |
| Reader UI (gestures, page turns)  | working — swipe through the news, scroll, go back   |
| Hyphenation (Liang/TeX patterns)  | working — justified text is now viable              |
| OPML import                       | working                                             |
| Edition persistence               | working — compose once, read from storage           |
| HTTP fetcher with conditional GET | working — verified live, most feeds answer 304      |
| Inkplate 6FLICK target            | working — fetches, composes and is read by touch    |
| Read state and carry-over         | working — unread stories keep until you read them   |
| Reading model                     | working — one pass, oldest first, ends when it ends |
| Full text behind truncated feeds  | working — the article is pulled from its own page   |
| Device commands                   | working — flash, log, put, compose from make        |
| Frontlight, battery mark          | working — threshold is a guess until measured       |
| Power loop (wake, compose, sleep) | working — draw over a day is not yet measured       |

## Hardware

The first target is the [Soldered Inkplate
6FLICK](https://soldered.com/product/inkplate-6flick/): ESP32, 8 MB PSRAM of
which 4 MB is addressable — plain ESP32 maps no more — 4 MB of flash, a 6"
1024×758 panel, capacitive touch, a 64-step frontlight, and ~23 µA in deep
sleep — a figure from the panel's own specification, taken before a microSD
card was fitted. Actual draw in this configuration has not been measured, and
no claim about how long a charge lasts is made here until it has been.

Measured on the board rather than taken from the datasheet: a 3-bit greyscale
full refresh is ~1.74 s and a 1-bit partial refresh ~0.46 s. Storage is the
microSD card, so the scarce flash stays free.

Nothing above the HAL includes an Inkplate header. Porting to another
ESP32-class e-ink board means implementing `src/hal/hal.h` — display, touch,
clock, power, storage, network — and nothing else.

## Building

The desktop simulator runs the same pipeline the device runs, renders pages to
PNG at the panel's exact geometry — 758×1024 in the default portrait, 1024×758
in landscape — and fakes touch from the keyboard.

```sh
make            # simulator, tests and the font pack
make check      # run the unit tests
make edition    # compose an edition from the fixture feeds into out/
```

`make` needs nothing but a C++17 compiler. If you have CMake, `cmake -B build
&& cmake --build build` builds the same targets and is what CI uses.

## On the device

The firmware needs PlatformIO. Everything else goes over the serial console,
because the device has no keyboard and its card does not come out:

```sh
make device            # build the firmware
make device-flash      # build it and put it on the board
make device-log        # watch what it says

make device-ls                                      # what is on the card
make device-put FILE=config/feeds.local.toml DEST=/feeds.toml
make device-rm PATH_ON_CARD=/read.dat               # forget what you have read
make device-compose    # fetch and compose now, rather than waiting for wake_at
```

The board is found automatically; `DIARIUM_PORT` picks one if a machine has
more than one attached. `make help` lists everything.

The console the device runs is one line at a time, so it is usable by hand
over any terminal and not only by these commands:

```
PUT <path> <bytes>   followed by exactly that many bytes
LS                   list the card
RM <path>            remove one file
COMPOSE              take the compose path on this boot
GO                   stop listening
```

Every command answers with a line starting `OK` or `ERR`. The console is open
for a moment after boot and closes on `GO`, so it costs a reset to reach and
nothing at all while reading. A file's bytes go straight from
disk to the port and are never printed, which is what makes it safe to send a
`feeds.toml` with wifi credentials in it — keep that copy in
`config/feeds.local.toml`, which is gitignored, and never in the committed
sample.

`make device-compose` is the one to reach for while working on the fetcher: a
compose takes a couple of minutes, and it prints what it fetched, what
answered 304, and when the next edition is due.

Inspect what the parser makes of a feed:

```sh
./bin/diarium-sim parse --verbose test/fixtures/feeds/*.xml
```

See the page as the panel will actually show it, rather than as an 8-bit
render — thin serifs and hairline rules survive one reduction and not the
other:

```sh
./bin/diarium-sim compose --depth mono1 --pages 4   # 1-bit, Atkinson dithered
./bin/diarium-sim compose --depth grey3             # the full-refresh mode
```

`compose` saves the edition it built, and `read` loads it rather than
composing again — which is the whole reason a page turn is cheap on a battery:

```sh
./bin/diarium-sim compose --fresh        # writes out/edition.rspe
./bin/diarium-sim read                   # "no re-parse, no re-layout"
./bin/diarium-sim read --recompose       # ignore it and build a fresh one
```

By default `compose` writes only the pages you flip through. `--all-pages`
writes the story text behind them too, and `--index` prints the navigation
map — every lede's page, its tap region, and the pages its story occupies:

```sh
./bin/diarium-sim compose --fresh --index --all-pages
```

Read the paper. You land on a contents page and swipe onward through every
story you have not read, oldest first, until the news runs out; each one is
marked read as you reach it, and what you did not get to is still there
tomorrow. The keyboard stands in for the panel — a keystroke becomes a
synthesised touch, through the same gesture recogniser and the same `Reader`
the device will run — and every refresh writes a frame:

```sh
make read
#  n  the next unread article    j/k  scroll within it
#  p  the previous article       1-9  open the Nth story here
#  b  back                       s    section list
#  g  home: the contents page (a long press in the bottom-left corner)
```

It reports what the panel would have spent: partial refreshes are ~0.46 s,
full ones ~1.74 s, and the ratio is a design decision you can see.

### Navigation

The keys above stand in for these. Reading is a line rather than a tree, so
"onward" is rightwards everywhere and there is no stack to pop:

| Gesture     | Reading the pass       | Contents page and ledes          | A story from a lede           |
| ----------- | ---------------------- | -------------------------------- | ----------------------------- |
| Swipe right | the next unread story  | start reading                    | the previous page             |
| Swipe left  | the previous story     | the next page                    | onward, then back to the lede |
| Swipe up    | further into the story | —                                | —                             |
| Swipe down  | back up the story      | the section list                 | the section list              |
| Tap         | —                      | open the story under your finger | —                             |

Two corners mean the same thing wherever you are. The top right is the light:
a tap switches it, a long press steps the brightness round. The bottom left is
home: a long press returns to the contents page, from an article, a lede page
or the end of the news alike. Going home is not a back — it does not restore
where you were, and nothing becomes unread by leaving, so swiping onward
resumes at the oldest story still outstanding.

In the section list, a tap picks a section and a swipe up goes back. Its last
row is **Mark everything read**, which takes two taps — the first arms it and
says so.

## Configuration

Feeds live in a single `feeds.toml` on the card — a URL, a section name, and
how many items to take. The card is also how wifi credentials reach a device
with no keyboard, which is the whole reason storage lives there rather than in
flash:

```toml
[wifi]
ssid = "your-network"   # omit the section and the device never fetches
password = "..."        # never commit this; see config/feeds.local.toml

[edition]
title = "Diarium"       # the nameplate across the top of page one
wake_at = "05:30"        # local time the next edition is composed
# max_items = 40         # optional ceiling; there is none by default
max_age_days = 3         # older than this is not news
utc_offset_minutes = 0   # no network to ask, no keyboard to be asked
front_page_columns = 2   # 1 to 4
body_alignment = "ragged"  # or "justified", which wants hyphenation
hyphenate = true

[[feed]]
url     = "https://daringfireball.net/feeds/main"
section = "Technology"
max_items = 6
```

Already have a subscription list? Import it. Folders become sections, and
nesting collapses to the outermost folder — a reader's sub-folders are
refinements, not sections:

```sh
./bin/diarium-sim opml subscriptions.opml --out config/feeds.toml
```

## Non-goals

No server component. No EPUB or OPDS. No read-later integrations. No AI. No
images: a feed gives one usable one per item at best, and twenty empty boxes
make a page unreadable, so an image becomes a caption line.

Full-text extraction was a non-goal until the reading model changed. Skimming
ledes and picking made a truncated story cheap; swiping through every story in
turn made it a dead end, and 21 of 40 stories in a typical edition are cut
short by their publisher. So a truncated story now has its own page fetched
and the article pulled out of it — and when that fails, the feed's excerpt is
printed rather than a navigation bar.

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
  edition/              composer, read-state dedup
  net/                  HTTP heads, body framing, URLs, the validator cache
  config/               feeds.toml
  ui/                   gesture recognition, the reader
src/hal/                hardware abstraction — the only portability seam
src/sim/                desktop harness: PNG output, keyboard "touch"
src/device/             the Inkplate: six HAL implementations and a console
tools/device.py         put files on the card, force a compose, watch the log
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
