# Contributing

## The thesis is not up for negotiation

RSSpaper is a newspaper, not a feed. Before proposing a feature, check it
against these:

- **Bounded editions.** A refresh produces a front page, sections, and an end.
  Nothing that makes the reading session open-ended.
- **On-device only.** No server, no accounts, no cloud, no telemetry. The only
  network requests are to the user's own feed URLs.
- **Calm.** No notifications, no badges, no unread counts, no streaks, no
  recommendations, no AI curation.
- **Reverse-chronological within sections.** That is the ranking.

Features that are good ideas for a different product are still no.

## Working on it

```sh
make check                       # unit tests + the portability gate
make edition                     # render pages from the fixture feeds
make read                        # read the paper from the keyboard
./bin/rsspaper-sim parse f.xml   # see what the parser makes of a feed
```

Iterate in the simulator. It runs the same code as the device for everything
above the HAL, and it writes PNGs you can actually look at.

## House rules

- **Nothing above the HAL includes a platform header.** If a change to
  `src/core/` needs `Arduino.h`, the design is wrong — the capability belongs
  behind an interface in `src/hal/hal.h`. `tools/check-portability.sh` enforces
  this and runs in CI; `core/ui/reader` is the single deliberate exception,
  since driving the display is its job.
- **Nothing scales with input size.** Every buffer that touches feed data has a
  cap. If you add one, add its limit to the relevant `*Limits` struct.
- **Parsers recover, they don't fail.** A malformed feed costs one story, never
  the edition.
- **Ask before adding a dependency.** The vendored set is deliberately tiny:
  `stb_truetype` and `stb_image_write` (public domain), `doctest` (MIT), and
  Literata (SIL OFL). Everything else is written here.
- **Log non-obvious choices in `DECISIONS.md`.** Especially the ones you'd
  otherwise have to re-derive in six months.

## Tests

Parser and layout changes need tests. The corpus in `test/fixtures/feeds/` is
real feeds with real breakage in them — add to it when you hit a feed that
misbehaves in a new way, and note its provenance in the fixtures README.

Tests use [doctest](https://github.com/doctest/doctest), vendored. Run a single
case with:

```sh
./bin/rsspaper-tests --test-case="*entities*"
```
