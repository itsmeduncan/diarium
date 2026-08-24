# Feed fixtures

Real feeds, fetched unmodified, kept as regression input for the parser. They
are here because synthetic XML doesn't contain the things that actually break
parsers: double-encoded entities inside CDATA, mismatched tags, legacy
encodings, `media:*` elements that look like article content, and summaries
that are secretly whole articles.

Fetched 2026-08-15. Do not reformat or prettify them — the malformations are
the point.

| File                      | Source                                  | Format  | What it exercises                                                   |
| ------------------------- | --------------------------------------- | ------- | ------------------------------------------------------------------- |
| `arstechnica.rss.xml`     | feeds.arstechnica.com/arstechnica/index | RSS 2.0 | `content:encoded` alongside `description`                           |
| `astralcodexten.rss.xml`  | astralcodexten.substack.com/feed        | RSS 2.0 | 772 KB, 47 K-character articles — the streaming/bounded-memory case |
| `bbc-news.rss.xml`        | feeds.bbci.co.uk/news/rss.xml           | RSS 2.0 | headline-and-one-line summaries; `media:thumbnail`                  |
| `craigmod.rss.xml`        | craigmod.com/index.xml                  | RSS 2.0 | deliberate short summaries, bracketed title prefixes                |
| `daringfireball.atom.xml` | daringfireball.net/feeds/main           | Atom    | full `<content>`, `★` in titles, link-post structure                |
| `guardian-world.rss.xml`  | theguardian.com/world/rss               | RSS 2.0 | `dc:creator`, multi-paragraph summaries                             |
| `hackernews.rss.xml`      | news.ycombinator.com/rss                | RSS 2.0 | title-only feed; description is a bare `Comments` link              |
| `kottke.rss.xml`          | feeds.kottke.org/main                   | Atom    | very short full-content posts (the `very-short` heuristic)          |
| `nasa.rss.xml`            | nasa.gov/feed                           | RSS 2.0 | image-heavy bodies, many block elements per item                    |
| `simonwillison.atom.xml`  | simonwillison.net/atom/everything/      | Atom    | `<summary>` carrying real content                                   |
| `theverge.atom.xml`       | theverge.com/rss/index.xml              | Atom    | `<title type="html">` + CDATA + `&#8217;` double encoding           |
| `xkcd.rss.xml`            | xkcd.com/rss.xml                        | RSS 2.0 | tiny feed, `<img>`-only bodies                                      |

Content is the property of the respective publishers and is included solely as
test input.

## Adding one

Add a feed when it breaks the parser in a way nothing here does, and say what
that way is in the table. A fixture that exercises nothing new is just a slower
test suite.
